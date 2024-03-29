#pragma once
#include <algorithm>
#include <atomic>
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
    if constexpr (std::is_same_v<typename ArgumentHelper::Container, void>) {
      if (m_memory > m_nMemory) {
        m_memory = m_nMemory;
      }
    } else {
      if (auto const currentMemorySize = size(m_memory);
          currentMemorySize > m_nMemory) {
        auto const nMemoryToFree = currentMemorySize - m_nMemory;
        // Should we move these items outside and let them get deconstructed
        // without the lock guard?
        m_memory.erase(begin(m_memory),
                       std::next(begin(m_memory), nMemoryToFree));
      }
    }
  }

  void callbackFromMemory(F &callback) {
    if constexpr (std::is_same_v<typename ArgumentHelper::Container, void>) {
      for (auto i = std::size_t{0}; i < m_memory; ++i) {
        callback();
      }
    } else {
      std::for_each(cbegin(m_memory), cend(m_memory),
          [&callback](typename ArgumentHelper::Container const &args) {
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
    } else if constexpr (std::is_same_v<typename ArgumentHelper::Container, void>) {
      if (m_memory < m_nMemory) {
        ++m_memory;
      }
    }
  }

  template <typename... Args>
  void next(Args &&...value) {
    auto callbacks = [&] {
      auto const lock = std::lock_guard{m_mutex};
      appendMemory(value...);
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
  using WeakCore = std::weak_ptr<ObserverCore<ArgumentHelper>>;
  SubscriptionBase(WeakCore &&d, std::size_t const idx)
      : m_weakD{std::forward<WeakCore>(d)}, m_idx{idx} {}
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
  WeakCore m_weakD;
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
    static std::atomic<size_t> sNextId{};
    return d->m_map.emplace(++sNextId, callback).first->first;
  }();
  // Perform cached callbacks without any lock
  d->callbackFromMemory(callback);
  // Return subscription object
  return std::make_shared<SubscriptionBase<ArgumentHelper>>(std::move(d),
                                                            idxSubscription);
}

template <typename ArgumentHelper>
class ObservableBase final {
  using Core = ObserverCore<ArgumentHelper>;
  std::weak_ptr<Core> d;

 public:
  ObservableBase() = default;

  // Construct observable from a weak core.
  explicit ObservableBase(std::weak_ptr<Core> &&data) : d{std::move(data)} {}

  // Check if the core still exists.
  [[nodiscard]] bool isAlive() const { return !d.expired(); }

  // Subscribe to the core if it still exists, returns nullopt otherwise.
  [[nodiscard]] auto subscribe(typename Core::F &&callback) -> Subscription {
    if (auto s_d = d.lock()) {
      return ob_internal::subscribe(std::move(s_d),
                                    std::forward<typename Core::F>(callback));
    } else {
      return nullptr;
    }
  }

  // Create another observable with the same weak_ptr core.
  auto share() const { return ObservableBase{std::weak_ptr<Core>{d}}; }
};

template <typename ArgumentHelper>
class SubjectBase final {
  using Core = ob_internal::ObserverCore<ArgumentHelper>;

  // Create a core
  std::shared_ptr<Core> d = std::make_shared<Core>();

  // Construct from core.
  explicit SubjectBase(std::shared_ptr<Core> &&shallowCore)
      : d{std::move(shallowCore)} {}

 public:
  SubjectBase() = default;

  // Store the returned subscription to receive further callbacks
  [[nodiscard]] auto subscribe(typename Core::F &&callback) -> Subscription {
    return ob_internal::subscribe(std::shared_ptr<Core>{d},
                                  std::forward<typename Core::F>(callback));
  }

  // Number of triggers stored for new subscribers
  void setMemorySize(std::size_t const nMemory) { d->setMemorySize(nMemory); }

  // Create a sharable shallow observable. Points to the same core.
  [[nodiscard]] auto asObservable() const -> ObservableBase<ArgumentHelper> {
    return ObservableBase<ArgumentHelper>{d};
  }

  // Create a sharable shallow subject. Both subjects point to the same core.
  [[nodiscard]] auto share() const -> SubjectBase {
    return SubjectBase{std::shared_ptr<Core>{d}};
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

template <typename ArgumentHelper>
class PipeBase final {
 public:
  PipeBase() = default;
  template <class SuperObservable, class F>
  PipeBase(SuperObservable &&superObservable, F pipeFunction) {
    createPipe(std::forward<SuperObservable>(superObservable),
               std::move(pipeFunction));
  }

  template <class SuperObservable, class F>
  auto createPipe(SuperObservable &&superObservable, F pipeFunction) {
    m_subscriptions.emplace_back(superObservable.subscribe(
        [&pipeSubject = m_pipeSubject, pipeFunction](auto... args) mutable {
          pipeFunction(pipeSubject, args...);
        }));
  }

  // Store the returned subscription to receive further callbacks
  [[nodiscard]] auto subscribe(typename ArgumentHelper::F &&callback)
      -> Subscription {
    return m_pipeSubject.subscribe(
        std::forward<typename ArgumentHelper::F>(callback));
  }

  auto asObservable() const { return m_pipeSubject.asObservable(); }

  auto subject() { return m_pipeSubject.share(); }

 private:
  SubjectBase<ArgumentHelper> m_pipeSubject;
  std::vector<Subscription> m_subscriptions;
};
}  // namespace ob_internal

template <typename... Args>
using Subject = ob_internal::SubjectBase<ob_internal::ObserverArgumentHelper<Args...>>;

template <typename... Args>
using Observable = ob_internal::ObservableBase<ob_internal::ObserverArgumentHelper<Args...>>;

template <typename... Args>
using Pipe = ob_internal::PipeBase<ob_internal::ObserverArgumentHelper<Args...>>;
}  // namespace csari
