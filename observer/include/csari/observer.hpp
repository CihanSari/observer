#pragma once
#include <algorithm>
#include <any>
#include <deque>
#include <memory>
#include <mutex>
#include <optional>
#include <unordered_map>
#include <vector>

namespace csari {
namespace ob_internal {
template <typename U>
using EnableIfNotVoid =
    typename std::enable_if<!std::is_same<U, void>::value>::type;
template <typename U>
using EnableIfVoid =
    typename std::enable_if<std::is_same<U, void>::value>::type;

template <typename T>
struct ObserverMemoryType {
  using MemoryType = std::deque<T>;
};
template <>
struct ObserverMemoryType<void> {
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
// Core observer. All callbacks, subscriptions and memory is shared with this
// object.
template <typename T, class F>
struct ObserverCore final {
  std::unordered_map<std::size_t, F> m_map;
  // thread safety
  std::mutex m_mutex;
  // contains std::deque<T> if non-void, std::size_t otherwise
  typename ObserverMemoryType<T>::MemoryType m_memory{};
  // maximum memory size
  std::size_t m_nMemory{0u};
  // number of listeners out there
  std::size_t m_callbackCounter{0u};

  // memory size setter if type is not void
  template <class U = T, EnableIfNotVoid<U>...>
  void setMemorySize(std::size_t const nMemory) {
    auto const lock = std::lock_guard{m_mutex};
    m_nMemory = nMemory;
    auto const currentMemorySize = m_memory.size();
    if (currentMemorySize > m_nMemory) {
      auto const nMemoryToFree = currentMemorySize - m_nMemory;
      // Should we move these items outside and let them get deconstructed
      // without the lock guard?
      m_memory.erase(m_memory.begin(), m_memory.begin() + nMemoryToFree);
    }
  }

  // memory size setter if type is void
  template <class U = T, EnableIfVoid<U>...>
  void setMemorySize(std::size_t const nMemory) {
    auto const lock = std::lock_guard{m_mutex};
    m_nMemory = nMemory;
    if (m_memory > m_nMemory) {
      m_memory = m_nMemory;
    }
  }
  // memory callback if type is not void
  template <class U = T, EnableIfNotVoid<U>...>
  void callbackFromMemory(F &callback) {
    static_cast<void>(
        std::for_each(m_memory.cbegin(), m_memory.cend(), callback));
  }

  // memory callback if type is void
  template <class U = T, EnableIfVoid<U>...>
  void callbackFromMemory(F &callback) {
    for (auto i = std::size_t{0}; i < m_memory; ++i) {
      callback();
    }
  }

  // Create a callbacks queue to be invoked after locks are released. Should be
  // locked before call.
  auto callbackQueue() -> std::vector<F> {
    auto vecCallbacks = std::vector<F>(m_map.size());
    std::transform(m_map.cbegin(), m_map.cend(), vecCallbacks.begin(),
                   [](auto const &pair) { return pair.second; });
    return vecCallbacks;
  }

  // Appends the value to the memory. Should be locked before call.
  template <class U = T, EnableIfNotVoid<U>...>
  auto appendMemory(U &&value) -> U & {
    if (m_memory.size() == m_nMemory) {
      m_memory.pop_front();
    }
    return m_memory.emplace_back(std::move(value));
  }

  // Appends the memory. Should be locked before call.
  template <class U = T, EnableIfVoid<U>...>
  void appendMemory() {
    if (m_memory < m_nMemory) {
      ++m_memory;
    }
  }

  // Push into the memory if type is not void
  template <class U = T, EnableIfNotVoid<U>...>
  void next(U &&value) {
    // Create a callbacks queue to be invoked after locks are released
    auto callbacks = [&] {
      auto const lock = std::lock_guard{m_mutex};
      if (m_nMemory > 0) {
        appendMemory(value);
      }
      return callbackQueue();
    }();

    // Now invoke all callbacks without any locks.
    if (!callbacks.empty()) {
      auto lastCallback = callbacks.back();
      callbacks.pop_back();
      std::for_each(callbacks.begin(), callbacks.end(),
                    [&value](F &callback) { callback(value); });

      // Move the value to the last callback.
      lastCallback(std::forward<U>(value));
    }
  }

  // Push into the memory if type is void
  template <class U = T, EnableIfVoid<U>...>
  void next() {
    auto callbacks = [&] {
      auto const lock = std::lock_guard{m_mutex};
      if (m_memory < m_nMemory) {
        ++m_memory;
      }
      auto vecCallbacks = std::vector<F>(m_map.size());
      std::transform(m_map.cbegin(), m_map.cend(), vecCallbacks.begin(),
                     [](auto const &pair) { return pair.second; });
      return vecCallbacks;
    }();
    std::for_each(callbacks.begin(), callbacks.end(),
                  [](F &callback) { callback(); });
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
      : m_weakD(std::forward<WeakO>(d)), m_idx(idx) {}
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
}  // namespace ob_internal

// Subscription can be copied and shared around. Callbacks will continue until
// last copy is removed.
using Subscription = std::shared_ptr<void>;
namespace ob_internal {

// Subscription helper
template <typename T, class F>
[[nodiscard]] auto subscribe(std::shared_ptr<ObserverCore<T, F>> &&d,
                             F callback) -> Subscription {
  // Access shared elements via lock-guard
  auto const idxSubscription = [&] {
    auto const lock = std::lock_guard{d->m_mutex};
    auto const idx = d->m_callbackCounter++;
    d->m_map.emplace(idx, callback);
    return idx;
  }();
  // Perform cached callbacks without any lock
  d->callbackFromMemory(callback);
  // Return subscription object
  return std::make_shared<SubscriptionBase<T, F>>(std::move(d),
                                                  idxSubscription);
}
}  // namespace ob_internal

template <typename T, class F>
class ObservableBase final {
  using ObserverCore = ob_internal::ObserverCore<T, F>;
  std::weak_ptr<ObserverCore> d;

 public:
  ObservableBase() = default;

  // Construct observable from a weak core.
  explicit ObservableBase(std::weak_ptr<ObserverCore> &&data)
      : d(std::move(data)) {}

  // Check if the core still exists.
  bool isAlive() const { return !d.expired(); }

  // Subscribe to the core if it still exists, returns nullopt otherwise.
  [[nodiscard]] auto subscribe(F &&callback) -> std::optional<Subscription> {
    if (auto s_d = d.lock()) {
      return ob_internal::subscribe(std::move(s_d), std::forward<F>(callback));
    } else {
      return std::nullopt;
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
      : d(std::move(shallowCore)) {}

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

  // Trigger subject if not void
  template <class U = T, ob_internal::EnableIfNotVoid<U>...>
  auto operator<<(U &&value) -> SubjectBase & {
    d->next(std::forward<U>(value));
    return *this;
  }

  // Trigger subject if not void
  template <class U = T, ob_internal::EnableIfNotVoid<U>...>
  auto next(U &&value) -> SubjectBase & {
    d->next(std::forward<U>(value));
    return *this;
  }

  // Trigger subject if not void
  template <class U = T, ob_internal::EnableIfNotVoid<U>...>
  auto operator<<(U const &value) -> SubjectBase & {
    d->next(value);
    return *this;
  }

  // Trigger subject if not void
  template <class U = T, ob_internal::EnableIfNotVoid<U>...>
  auto next(U const &value) -> SubjectBase & {
    d->next(value);
    return *this;
  }

  // Trigger subject if void
  template <class U = T, ob_internal::EnableIfVoid<U>...>
  void operator()() {
    d->next();
  }

  // Trigger subject if void
  template <class U = T, ob_internal::EnableIfVoid<U>...>
  auto next() -> SubjectBase & {
    d->next();
    return *this;
  }
};

template <typename T>
using Subject = SubjectBase<T, typename ob_internal::Function<T>::F>;

template <typename T>
using Observable = ObservableBase<T, typename ob_internal::Function<T>::F>;
}  // namespace csari
