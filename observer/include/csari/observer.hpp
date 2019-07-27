#pragma once
#include <algorithm>
#include <any>
#include <deque>
#include <execution>
#include <memory>
#include <mutex>
#include <optional>
#include <unordered_map>

namespace csari {
namespace observerInternal {
template <typename U>
using EnableIfNotVoid =
    typename std::enable_if<!std::is_same<U, void>::value>::type;
template <typename U>
using EnableIfVoid =
    typename std::enable_if<std::is_same<U, void>::value>::type;
}  // namespace observerInternal

// This declaration is used to deduce arguments. Read more (if link is still
// available): https://functionalcpp.wordpress.com/2013/08/05/function-traits/
template <class Sig>
struct Invokable;
// Actual Invokable declaration
template <class... Args>
struct Invokable<void(Args...)> final {
  Invokable() = default;

  // implicitly create from a type that can be compatibly invoked
  // and isn't an Invokable itself
  template <class F>
  Invokable(F &&f)
      : m_ptr(std::move(f)), m_invoke([](std::any &pf, Args... args) {
          auto &fInternal = std::any_cast<F &>(pf);
          fInternal(std::forward<Args>(args)...);
        }) {}

  // invoker
  void operator()(Args... args) {
    m_invoke(m_ptr, std::forward<Args>(args)...);
  }

 private:
  // storage for the invokable and an invoker function pointer:
  std::any m_ptr{};
  void (*m_invoke)(std::any &, Args...) = nullptr;
};

namespace observerInternal {
// Core observer. All callbacks, subscriptions and memory is shared with this
// object.
template <typename T>
struct ObserverCore final {
  using FCallback = Invokable<void(T)>;  // Listeners
  std::unordered_map<std::size_t, FCallback> m_map;
  // thread safety
  std::mutex m_mutex;
  // contains std::deque<T> if non-void, std::size_t otherwise
  std::any m_memory;
  // maximum memory size
  std::size_t m_nMemory{0u};
  // number of listeners out there
  std::size_t m_callbackCounter{0u};

  // Constructor (allocator) if type is not void
  template <class U = T, EnableIfNotVoid<U>...>
  ObserverCore() : m_memory(std::deque<T>{}) {}

  // Constructor (allocator) if type is void
  template <class U = T, EnableIfVoid<U>...>
  ObserverCore() : m_memory(std::size_t{0}) {}

  // memory size setter if type is not void
  template <class U = T, EnableIfNotVoid<U>...>
  void setMemorySize(std::size_t const nMemory) {
    auto const lock = std::lock_guard{m_mutex};
    auto &memoryQue = std::any_cast<std::deque<T> &>(m_memory);
    m_nMemory = nMemory;
    auto const currentMemorySize = memoryQue.size();
    if (currentMemorySize > m_nMemory) {
      auto const nMemoryToFree = currentMemorySize - m_nMemory;
      // Should we move these items outside and let them get deconstructed
      // without the lock guard?
      memoryQue.erase(memoryQue.begin(), memoryQue.begin() + nMemoryToFree);
    }
  }

  // memory size setter if type is void
  template <class U = T, EnableIfVoid<U>...>
  void setMemorySize(std::size_t const nMemory) {
    auto const lock = std::lock_guard{m_mutex};
    m_nMemory = nMemory;
    auto const memorySize = std::any_cast<std::size_t &>(m_memory);
    if (memorySize > m_nMemory) {
      m_memory = m_nMemory;
    }
  }

  // memory callback if type is not void
  template <class U = T, EnableIfNotVoid<U>...>
  void callbackFromMemory(FCallback &callback) {
    auto &memoryQue = std::any_cast<std::deque<T> &>(m_memory);
    std::for_each(std::execution::seq, memoryQue.cbegin(), memoryQue.cend(),
                  [&callback](T const &instance) { callback(instance); });
  }

  // memory callback if type is void
  template <class U = T, EnableIfVoid<U>...>
  void callbackFromMemory(FCallback &callback) {
    auto const &memorySize = std::any_cast<std::size_t &>(m_memory);
    for (auto i = std::size_t{0}; i < memorySize; ++i) {
      callback();
    }
  }

  // Create a callbacks queue to be invoked after locks are released. Should be
  // locked before call.
  auto callbackQueue() -> std::vector<FCallback> {
    auto vecCallbacks = std::vector<FCallback>(m_map.size());
    std::transform(std::execution::par_unseq, m_map.cbegin(), m_map.cend(),
                   vecCallbacks.begin(),
                   [](auto const &pair) { return pair.second; });
    return vecCallbacks;
  }

  // Appends the value to the memory. Should be locked before call.
  template <class U = T, EnableIfNotVoid<U>...>
  auto appendMemory(U &&value) -> U & {
    auto &memory = std::any_cast<std::deque<T> &>(m_memory);
    if (memory.size() == m_nMemory) {
      memory.pop_front();
    }
    return memory.emplace_back(std::move(value));
  }

  // Appends the memory. Should be locked before call.
  template <class U = T, EnableIfVoid<U>...>
  void appendMemory() {
    auto &memory = std::any_cast<std::size_t &>(m_memory);
    if (memory < m_nMemory) {
      ++memory;
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
      std::for_each(std::execution::seq, callbacks.begin(), callbacks.end(),
                    [&value](FCallback &callback) { callback(value); });

      // Move the value to the last callback.
      lastCallback(std::forward<U>(value));
    }
  }

  // Push into the memory if type is void
  template <class U = T, EnableIfVoid<U>...>
  void next() {
    auto callbacks = [&] {
      auto const lock = std::lock_guard{m_mutex};
      auto &memory = std::any_cast<std::size_t &>(m_memory);
      if (memory < m_nMemory) {
        ++memory;
      }
      auto vecCallbacks = std::vector<FCallback>(m_map.size());
      std::transform(std::execution::par_unseq, m_map.cbegin(), m_map.cend(),
                     vecCallbacks.begin(),
                     [](auto const &pair) { return pair.second; });
      return vecCallbacks;
    }();
    std::for_each(std::execution::seq, callbacks.begin(), callbacks.end(),
                  [](FCallback &callback) { callback(); });
  }
};

// Acts as a scope-guard. It will unsubscribe when the object is discarded
// (out of scope) or manually released. Unique per subscription. Shared to the
// caller with shared_ptr to keep subscription alive in different scopes.
struct SubscriptionCore final {
  template <typename T>
  SubscriptionCore(std::size_t const idx,
                   std::weak_ptr<observerInternal::ObserverCore<T>> &&weakD)
      : unsubscribe([weakD = std::move(weakD), idx] {
          if (auto const d = weakD.lock()) {
            // Subject is still alive, we should unsubscribe
            auto const lock = std::lock_guard{d->m_mutex};
            auto const it = d->m_map.find(idx);
            if (it != d->m_map.end()) {
              d->m_map.erase(it);
            }
          }
        }) {}
  SubscriptionCore() = delete;
  SubscriptionCore(SubscriptionCore &&) = default;
  SubscriptionCore &operator=(SubscriptionCore &&) = default;
  SubscriptionCore(SubscriptionCore const &) = delete;
  SubscriptionCore &operator=(SubscriptionCore const &) = delete;
  ~SubscriptionCore() { unsubscribe(); }

 private:
  Invokable<void()> unsubscribe;
};
}  // namespace observerInternal

// Subscription can be copied and shared around. Callbacks will continue until
// last copy is removed.
using Subscription = std::shared_ptr<observerInternal::SubscriptionCore>;
namespace observerInternal {

// Subscription helper
template <typename T>
[[nodiscard]] auto subscribe(std::shared_ptr<ObserverCore<T>> &&d,
                             typename ObserverCore<T>::FCallback callback)
    -> Subscription {
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
  return std::make_shared<SubscriptionCore>(
      idxSubscription, std::weak_ptr<ObserverCore<T>>{std::move(d)});
}
}  // namespace observerInternal

template <typename T>
class Observable final {
  using ObserverCore = observerInternal::ObserverCore<T>;
  std::weak_ptr<ObserverCore> d;

 public:
  using FCallback = typename ObserverCore::FCallback;

  Observable() = default;

  // Construct observable from a weak core.
  explicit Observable(std::weak_ptr<ObserverCore> &&data)
      : d(std::move(data)) {}

  // Check if the core still exists.
  bool isAlive() const { return !d.expired(); }

  // Subscribe to the core if it still exists, returns nullopt otherwise.
  [[nodiscard]] auto subscribe(FCallback &&callback)
      -> std::optional<Subscription> {
    if (auto s_d = d.lock()) {
      return observerInternal::subscribe(std::move(s_d),
                                         std::forward<FCallback>(callback));
    } else {
      return std::nullopt;
    }
  }

  // Create another observable with the same weak_ptr core.
  Observable share() const {
    return Observable{std::weak_ptr<ObserverCore>{d}};
  }
};

template <typename T>
class Subject final {
  using ObserverCore = observerInternal::ObserverCore<T>;

  // Create a core
  std::shared_ptr<ObserverCore> d = std::make_shared<ObserverCore>();

  // Construct from core.
  explicit Subject(std::shared_ptr<ObserverCore> &&shallowCore)
      : d(std::move(shallowCore)) {}

 public:
  using FCallback = typename ObserverCore::FCallback;
  Subject() = default;

  // Store the returned subscription to receive further callbacks
  [[nodiscard]] auto subscribe(FCallback &&callback) -> Subscription {
    return observerInternal::subscribe(std::shared_ptr<ObserverCore>{d},
                                       std::forward<FCallback>(callback));
  }

  // Number of triggers stored for new subscribers
  void setMemorySize(std::size_t const nMemory) { d->setMemorySize(nMemory); }

  // Create a sharable shallow observable. Points to the same core.
  [[nodiscard]] auto asObservable() const -> Observable<T> {
    return Observable<T>{d};
  }

  // Create a sharable shallow subject. Both subjects point to the same core.
  auto share() const -> Subject {
    return Subject{std::shared_ptr<ObserverCore>{d}};
  }

  // Trigger subject if not void
  template <class U = T, observerInternal::EnableIfNotVoid<U>...>
  auto operator<<(U &&value) -> Subject & {
    d->next(std::forward<U>(value));
    return *this;
  }

  // Trigger subject if not void
  template <class U = T, observerInternal::EnableIfNotVoid<U>...>
  auto next(U &&value) -> Subject & {
    d->next(std::forward<U>(value));
    return *this;
  }

  // Trigger subject if not void
  template <class U = T, observerInternal::EnableIfNotVoid<U>...>
  auto operator<<(U const &value) -> Subject & {
    d->next(value);
    return *this;
  }

  // Trigger subject if not void
  template <class U = T, observerInternal::EnableIfNotVoid<U>...>
  auto next(U const &value) -> Subject & {
    d->next(value);
    return *this;
  }

  // Trigger subject if void
  template <class U = T, observerInternal::EnableIfVoid<U>...>
  void operator()() {
    d->next();
  }

  // Trigger subject if void
  template <class U = T, observerInternal::EnableIfVoid<U>...>
  auto next() -> Subject & {
    d->next();
    return *this;
  }
};
}  // namespace csari
