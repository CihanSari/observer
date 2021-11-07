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

template <typename... Args>
struct ObserverArgumentHelper {
  using IsTuple = std::true_type;
  using Container = std::tuple<Args...>;
  using F = std::function<void(Args...)>;
};
template <typename T>
struct ObserverArgumentHelper<T> {
  using IsTuple = std::false_type;
  using Container = T;
  using F = std::function<void(T)>;
};
template <>
struct ObserverArgumentHelper<void> {
  using IsTuple = std::false_type;
  using Container = void;
  using F = std::function<void()>;
};

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

// Generate a "unique" id per subscription to self-clean.
auto getNextId() {
  static std::atomic_size_t sNextId{};
  return ++sNextId;
}

// Core observer. All callbacks, subscriptions and memory is shared with this
// object.
template <typename ArgumentHelper>
struct ObserverCore final {
  using F = typename ArgumentHelper::F;
  // Keeps track of subscriber ids and their callback functions
  std::unordered_map<std::size_t, F> m_map;
  // thread safety
  std::mutex m_mutex;
  // contains std::deque<T> if non-void, std::size_t otherwise
  typename ObserverMemoryType<typename ArgumentHelper::Container>::MemoryType
      m_memory{};
  // maximum memory size
  std::size_t m_nMemory{0u};

  // memory size setter if type is not void
  void setMemorySize(std::size_t const nMemory) {
    auto const lock = std::lock_guard{m_mutex};
    m_nMemory = nMemory;
    if constexpr (std::is_same_v<ArgumentHelper::Container, void>) {
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
    if constexpr (std::is_same_v<ArgumentHelper::Container, void>) {
      for (auto i = std::size_t{0}; i < m_memory; ++i) {
        callback();
      }
    } else {
      std::for_each(cbegin(m_memory), cend(m_memory),
                    [&callback](ArgumentHelper::Container const &args) {
                      if constexpr (ArgumentHelper::IsTuple::value) {
                        std::apply(callback, args);
                      } else {
                        std::invoke(callback, args);
                      }
                    });
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
    if (m_nMemory == 0) {
      return;
    }
    if constexpr (sizeof...(Args) == 1) {
      if (size(m_memory) == m_nMemory) {
        m_memory.pop_front();
      }
      m_memory.emplace_back(std::forward<Args>(value)...);
    } else if constexpr (std::is_same_v<ArgumentHelper, void>) {
      if (m_memory < m_nMemory) {
        ++m_memory;
      }
    }
  }

  template <typename... Args>
  void next(Args &&...value) {
    auto callbacks = [&] {
      auto const lock = std::lock_guard{m_mutex};
      appendMemory(std::forward<Args>(value)...);
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
template <typename ArgumentHelper>
class SubscriptionBase final {
 public:
  using WeakO = std::weak_ptr<ObserverCore<ArgumentHelper>>;
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
template <typename ArgumentHelper>
[[nodiscard]] auto subscribe(std::shared_ptr<ObserverCore<ArgumentHelper>> &&d,
                             typename ObserverCore<ArgumentHelper>::F callback)
    -> Subscription {
  // Access shared elements via lock-guard
  auto const idxSubscription = [&] {
    auto const lock = std::lock_guard{d->m_mutex};
    return d->m_map.emplace(getNextId(), callback).first->first;
  }();
  // Perform cached callbacks without any lock
  d->callbackFromMemory(callback);
  // Return subscription object
  return std::make_shared<SubscriptionBase<ArgumentHelper>>(std::move(d),
                                                            idxSubscription);
}

template <typename ArgumentHelper>
class ObservableBase final {
  using ObserverCore = ob_internal::ObserverCore<ArgumentHelper>;
  std::weak_ptr<ObserverCore> d;

 public:
  ObservableBase() = default;

  // Construct observable from a weak core.
  explicit ObservableBase(std::weak_ptr<ObserverCore> &&data)
      : d{std::move(data)} {}

  // Check if the core still exists.
  bool isAlive() const { return !d.expired(); }

  // Subscribe to the core if it still exists, returns nullopt otherwise.
  [[nodiscard]] auto subscribe(typename ObserverCore::F &&callback)
      -> Subscription {
    if (auto s_d = d.lock()) {
      return ob_internal::subscribe(
          std::move(s_d), std::forward<typename ObserverCore::F>(callback));
    } else {
      return nullptr;
    }
  }

  // Create another observable with the same weak_ptr core.
  auto share() const { return ObservableBase{std::weak_ptr<ObserverCore>{d}}; }
};

template <typename ArgumentHelper>
class SubjectBase final {
  using ObserverCore = ob_internal::ObserverCore<ArgumentHelper>;

  // Create a core
  std::shared_ptr<ObserverCore> d = std::make_shared<ObserverCore>();

  // Construct from core.
  explicit SubjectBase(std::shared_ptr<ObserverCore> &&shallowCore)
      : d{std::move(shallowCore)} {}

 public:
  SubjectBase() = default;

  // Store the returned subscription to receive further callbacks
  [[nodiscard]] auto subscribe(typename ObserverCore::F &&callback)
      -> Subscription {
    return ob_internal::subscribe(
        std::shared_ptr<ObserverCore>{d},
        std::forward<typename ObserverCore::F>(callback));
  }

  // Number of triggers stored for new subscribers
  void setMemorySize(std::size_t const nMemory) { d->setMemorySize(nMemory); }

  // Create a sharable shallow observable. Points to the same core.
  [[nodiscard]] auto asObservable() const -> ObservableBase<ArgumentHelper> {
    return ObservableBase<ArgumentHelper>{d};
  }

  // Create a sharable shallow subject. Both subjects point to the same core.
  auto share() const -> SubjectBase {
    return SubjectBase{std::shared_ptr<ObserverCore>{d}};
  }

  template <typename... Args>
  auto operator<<(Args &&...value) -> SubjectBase & {
    d->next(std::forward<Args>(value)...);
    return *this;
  }

  template <typename... Args>
  auto next(Args &&...value) -> SubjectBase & {
    d->next(std::forward<Args>(value)...);
    return *this;
  }
};
}  // namespace ob_internal

template <typename... Args>
using Subject = ob_internal::SubjectBase<
    typename ob_internal::ObserverArgumentHelper<Args...>>;

template <typename... Args>
using Observable = ob_internal::ObservableBase<
    typename ob_internal::ObserverArgumentHelper<Args...>>;
}  // namespace csari
