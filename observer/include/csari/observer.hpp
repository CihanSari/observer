#pragma once
#include <algorithm>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace csari {
// Subscription can be copied and shared around. Callbacks will continue until
// last copy is removed.
using Subscription = std::shared_ptr<void>;
namespace ob_internal {

template <typename T>
struct ObserverMemoryType {
  // Memory keeps list of content that should be sent to each observer
  using MemoryType = std::deque<T>;
};
template <>
struct ObserverMemoryType<void> {
  // Memory with void is number of calls that should be sent to each observer
  // https://en.cppreference.com/w/cpp/language/zero_initialization
  using MemoryType = std::size_t;
};

template <typename T>
struct Function {
  using F = std::function<void(T)>;
};
template <>
struct Function<void> {
  using F = std::function<void()>;
};

// Generate a "unique" id per subscription to self-clean.
auto getNextId() {
  static std::atomic_size_t sNextId{};
  return ++sNextId;
}

// Core observer. All callbacks, subscriptions and memory is shared with this
// object.
template <typename T, class F>
struct ObserverCore final {
  // Keeps track of subscriber ids and their callback functions
  std::unordered_map<std::size_t, F> m_map;
  // thread safety
  std::mutex m_mutex;
  // contains std::deque<T> if non-void, std::size_t otherwise
  typename ObserverMemoryType<T>::MemoryType m_memory{};
  // maximum memory size
  std::size_t m_nMemory{0u};

  // memory size setter if type is not void
  void setMemorySize(std::size_t const nMemory) {
    auto const lock = std::lock_guard{m_mutex};
    m_nMemory = nMemory;
    if constexpr (std::is_same_v<T, void>) {
      if (m_memory > m_nMemory) {
        m_memory = m_nMemory;
      }
    } else {
      auto const currentMemorySize = size(m_memory);
      if (currentMemorySize > m_nMemory) {
        auto const nMemoryToFree = currentMemorySize - m_nMemory;
        // Should we move these items outside and let them get deconstructed
        // without the lock guard?
        m_memory.erase(begin(m_memory),
                       std::next(begin(m_memory), nMemoryToFree));
      }
    }
  }

  void callbackFromMemory(F &callback) {
    if constexpr (std::is_same_v<T, void>) {
      for (auto i = std::size_t{0}; i < m_memory; ++i) {
        callback();
      }
    } else {
      std::for_each(cbegin(m_memory), cend(m_memory), callback);
    }
  }

  // Create a callbacks queue to be invoked after locks are released. Should be
  // locked before call.
  auto callbackQueue() -> std::vector<F> {
    auto vecCallbacks = std::vector<F>{};
    vecCallbacks.reserve(size(m_map));
    std::transform(cbegin(m_map), cend(m_map), back_inserter(vecCallbacks),
                   [](auto const &pair) { return pair.second; });
    return vecCallbacks;
  }

  template <typename... Args>
  auto appendMemory(Args &&...value) {
    static_assert(sizeof...(Args) < 2,
                  "appendMemory accepts maximum one parameter");
    if constexpr (sizeof...(Args) == 1) {
      if (size(m_memory) == m_nMemory) {
        m_memory.pop_front();
      }
      return m_memory.emplace_back(std::forward<Args>(value)...);
    } else if constexpr (std::is_same_v<T, void>) {
      if (m_memory < m_nMemory) {
        ++m_memory;
      }
    }
  }

  template <typename... Args>
  void next(Args &&...value) {
    auto callbacks = [&] {
      auto const lock = std::lock_guard{m_mutex};
      if constexpr (sizeof...(Args) == 1) {
        if (m_nMemory > 0) {
          appendMemory(std::forward<Args>(value)...);
        }
      } else if constexpr (std::is_same_v<T, void>) {
        if (m_memory < m_nMemory) {
          ++m_memory;
        }
      } else {
        static_assert(true, "next accepts maximum one parameter");
      }
      return callbackQueue();
    }();

    // Now invoke all callbacks without any locks.
    std::for_each(begin(callbacks), end(callbacks),
                  [&](F &callback) { callback(std::forward<Args>(value)...); });
  }
};

// Acts as a scope-guard. It will unsubscribe when the object is discarded
// (out of scope) or manually released. Unique per subscription. Shared to the
// caller with shared_ptr to manage the lifetime of the subscription.
template <typename T, class F>
class SubscriptionBase final {
 public:
  using WeakO = std::weak_ptr<ObserverCore<T, F>>;
  SubscriptionBase(WeakO &&d, std::size_t const idx)
      : m_weakD{std::forward<WeakO>(d)}, m_idx{idx} {}
  ~SubscriptionBase() {
    if (auto const d = m_weakD.lock()) {
      // Subject is still alive, we should unsubscribe
      auto const lock = std::lock_guard{d->m_mutex};
      auto const it = d->m_map.find(m_idx);
      if (it != d->m_map.end()) {
        d->m_map.erase(it);
      }
    }
  }
  SubscriptionBase(SubscriptionBase &&) = default;
  SubscriptionBase &operator=(SubscriptionBase &&) = default;
  SubscriptionBase(SubscriptionBase const &) = delete;
  SubscriptionBase &operator=(SubscriptionBase const &) = delete;

 private:
  WeakO m_weakD;
  std::size_t m_idx;
};

// Subscription helper
template <typename T, class F>
[[nodiscard]] auto subscribe(std::shared_ptr<ObserverCore<T, F>> &&d,
                             F callback) -> Subscription {
  // Access shared elements via lock-guard
  auto const idxSubscription = [&] {
    auto const lock = std::lock_guard{d->m_mutex};
    return d->m_map.emplace(getNextId(), callback).first->first;
  }();
  // Perform cached callbacks without any lock
  d->callbackFromMemory(callback);
  // Return subscription object
  return std::make_shared<SubscriptionBase<T, F>>(std::move(d),
                                                  idxSubscription);
}

template <typename T, class F>
class ObservableBase final {
  using ObserverCore = ob_internal::ObserverCore<T, F>;
  std::weak_ptr<ObserverCore> d;

 public:
  ObservableBase() = default;

  // Construct observable from a weak core.
  explicit ObservableBase(std::weak_ptr<ObserverCore> &&data)
      : d{std::move(data)} {}

  // Check if the core still exists.
  bool isAlive() const { return !d.expired(); }

  // Subscribe to the core if it still exists, returns nullopt otherwise.
  [[nodiscard]] auto subscribe(F &&callback) -> Subscription {
    if (auto s_d = d.lock()) {
      return ob_internal::subscribe(std::move(s_d), std::forward<F>(callback));
    } else {
      return nullptr;
    }
  }

  // Create another observable with the same weak_ptr core.
  auto share() const { return ObservableBase{std::weak_ptr<ObserverCore>{d}}; }
};

template <typename T, class F>
class SubjectBase final {
  using ObserverCore = ob_internal::ObserverCore<T, F>;

  // Create a core
  std::shared_ptr<ObserverCore> d = std::make_shared<ObserverCore>();

  // Construct from core.
  explicit SubjectBase(std::shared_ptr<ObserverCore> &&shallowCore)
      : d{std::move(shallowCore)} {}

 public:
  SubjectBase() = default;

  // Store the returned subscription to receive further callbacks
  [[nodiscard]] auto subscribe(F &&callback) -> Subscription {
    return ob_internal::subscribe(std::shared_ptr<ObserverCore>{d},
                                  std::forward<F>(callback));
  }

  // Number of triggers stored for new subscribers
  void setMemorySize(std::size_t const nMemory) { d->setMemorySize(nMemory); }

  // Create a sharable shallow observable. Points to the same core.
  [[nodiscard]] auto asObservable() const -> ObservableBase<T, F> {
    return ObservableBase<T, F>{d};
  }

  // Create a sharable shallow subject. Both subjects point to the same core.
  auto share() const -> SubjectBase {
    return SubjectBase{std::shared_ptr<ObserverCore>{d}};
  }

  template <typename... Args>
  auto operator<<(Args &&...value) -> SubjectBase & {
    static_assert(sizeof...(Args) < 2,
                  "pipe operator accepts maximum one parameter");
    d->next(std::forward<Args>(value)...);
    return *this;
  }

  template <typename... Args>
  auto next(Args &&...value) -> SubjectBase & {
    static_assert(sizeof...(Args) < 2, "next accepts maximum one parameter");
    d->next(std::forward<Args>(value)...);
    return *this;
  }
};
}  // namespace ob_internal

template <typename T>
using Subject =
    ob_internal::SubjectBase<T, typename ob_internal::Function<T>::F>;

template <typename T>
using Observable =
    ob_internal::ObservableBase<T, typename ob_internal::Function<T>::F>;
}  // namespace csari
