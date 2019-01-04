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
using Subscription = std::shared_ptr<void>;
template <class Sig>
struct Invokable;
namespace observerInternal {
template <typename U>
using EnableIfNotVoid =
    typename std::enable_if<!std::is_same<U, void>::value>::type;
template <typename U>
using EnableIfVoid =
    typename std::enable_if<std::is_same<U, void>::value>::type;
template <class T>
struct EmplaceAs final {};
}  // namespace observerInternal
template <class... Args>
struct Invokable<void(Args...)> final {
  Invokable() = default;

  // implicitly create from a type that can be compatibly invoked
  // and isn't an Invokable itself
  template <class F, std::enable_if_t<
                         !std::is_same<std::decay_t<F>, Invokable>{}, int> = 0>
  Invokable(F &&f)
      : Invokable(observerInternal::EmplaceAs<std::decay_t<F>>{},
                  std::forward<F>(f)) {}

  // emplacement construct using the EmplaceAs tag type:
  template <class F, class... FArgs>
  Invokable(observerInternal::EmplaceAs<F>, FArgs &&... functionArgs) {
    rebind<F>(std::forward<FArgs>(functionArgs)...);
  }

  // invoker
  void operator()(Args... args) {
    m_invoke(m_ptr, std::forward<Args>(args)...);
  }

  // empty the Invokable:
  void clear() {
    m_invoke = nullptr;
    m_ptr.reset();
  }

  // test if it is non-empty:
  explicit operator bool() const { return m_ptr.has_value(); }

  // change what the invokable contains:
  template <class F, class... FArgs>
  void rebind(FArgs &&... functionArgs) {
    m_ptr = std::make_any<F>(std::forward<FArgs>(functionArgs)...);
    // m_invoke will remember type of lambda to execute it
    m_invoke = [](std::any const &pf, Args... args) {
      std::any_cast<F>(pf)(std::forward<Args>(args)...);
    };
  }

 private:
  // storage for the invokable and an invoker function pointer:
  std::any m_ptr{};
  void (*m_invoke)(std::any const &, Args...) = nullptr;
};
namespace observerInternal {
// Subscription life-time guard, which will unsubscribe when the object
// is discarded (out of scope) or manually released
template <typename T>
struct ObserverCore;
template <typename T>
struct SubscriptionCleanUp final {
  SubscriptionCleanUp(std::size_t const idx,
                      std::weak_ptr<ObserverCore<T>> weakD)
      : idx(idx), weakD(std::move(weakD)) {}
  SubscriptionCleanUp() = delete;
  SubscriptionCleanUp(SubscriptionCleanUp &&) = delete;
  SubscriptionCleanUp &operator=(SubscriptionCleanUp &&) = delete;
  SubscriptionCleanUp(SubscriptionCleanUp const &) = delete;
  SubscriptionCleanUp &operator=(SubscriptionCleanUp const &) = delete;
  ~SubscriptionCleanUp() {
    if (auto d = weakD.lock()) {
      // Subject is still alive, we should unsubscribe
      auto const lock = std::lock_guard{d->m_mutex};
      auto const it = d->m_map.find(idx);
      if (it != d->m_map.end()) {
        d->m_map.erase(it);
      }
    }
  }
  std::size_t idx;
  std::weak_ptr<ObserverCore<T>> weakD;
};

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

  template <class U = T, EnableIfNotVoid<U>...>
  void makeMemory() {
    m_memory = std::deque<T>{};
  }

  template <class U = T, EnableIfNotVoid<U>...>
  void setMemorySize(std::size_t const nMemory) {
    auto const lock = std::lock_guard{m_mutex};
    m_nMemory = nMemory;
    auto &memoryQue = std::any_cast<std::deque<T> &>(m_memory);
    while (memoryQue.size() > m_nMemory) {
      memoryQue.pop_front();
    }
  }

  template <class U = T, EnableIfNotVoid<U>...>
  void callbackFromMemory(FCallback &callback) {
    auto &memoryQue = std::any_cast<std::deque<T> &>(m_memory);
    std::for_each(std::execution::seq, memoryQue.cbegin(), memoryQue.cend(),
                  [&callback](T instance) { callback(std::move(instance)); });
  }

  template <class U = T, EnableIfNotVoid<U>...>
  void next(U value) {
    auto callbacks = [&] {
      auto const lock = std::lock_guard{m_mutex};
      if (m_nMemory > 0) {
        auto &memory = std::any_cast<std::deque<T> &>(m_memory);
        if (memory.size() == m_nMemory) {
          memory.pop_front();
        }
        memory.emplace_back(value);
      }
      auto vecCallbacks = std::vector<FCallback>(m_map.size());
      std::transform(std::execution::par_unseq, m_map.cbegin(), m_map.cend(),
                     vecCallbacks.begin(),
                     [](auto const &pair) { return pair.second; });
      return vecCallbacks;
    }();
    if (!callbacks.empty()) {
      auto lastCallback = callbacks.back();
      callbacks.pop_back();
      std::for_each(std::execution::seq, callbacks.begin(), callbacks.end(),
                    [&value](auto &callback) { callback(value); });
      lastCallback(std::move(value));
    }
  }

  template <class U = T, EnableIfVoid<U>...>
  void makeMemory() {
    m_memory = std::size_t{0};
  }

  template <class U = T, EnableIfVoid<U>...>
  void setMemorySize(std::size_t const nMemory) {
    auto const lock = std::lock_guard{m_mutex};
    m_nMemory = nMemory;
    auto const memorySize = std::any_cast<std::size_t &>(m_memory);
    if (memorySize > m_nMemory) {
      m_memory = m_nMemory;
    }
  }

  template <class U = T, EnableIfVoid<U>...>
  void callbackFromMemory(FCallback &callback) {
    auto const &memorySize = std::any_cast<std::size_t &>(m_memory);
    for (auto i = std::size_t{0}; i < memorySize; ++i) {
      callback();
    }
  }

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
                  [](auto &callback) { callback(); });
  }

  ObserverCore() { makeMemory<T>(); }

  static Subscription subscribe(std::shared_ptr<ObserverCore> d,
                                FCallback callback) {
    // Access shared elements
    auto const idxSubscription = [&] {
      auto lock = std::lock_guard{d->m_mutex};
      auto const idx = d->m_callbackCounter++;
      d->m_map.emplace(idx, callback);
      return idx;
    }();
    // Perform callbacks without any lock
    d->callbackFromMemory(callback);
    return std::make_shared<observerInternal::SubscriptionCleanUp<T>>(
        idxSubscription, std::move(d));
  }
};
}  // namespace observerInternal
template <typename T>
class Observable final {
  using ObserverCore = observerInternal::ObserverCore<T>;
  std::weak_ptr<ObserverCore> d;

 public:
  using FCallback = typename ObserverCore::FCallback;
  Observable() = default;
  Observable(std::weak_ptr<ObserverCore> data) : d(std::move(data)) {}

  bool isAlive() const { return !d.expired(); }

  [[nodiscard]] std::optional<Subscription> subscribe(FCallback callback) {
    if (auto s_d = d.lock()) {
      return ObserverCore::subscribe(s_d, std::move(callback));
    } else {
      return std::nullopt;
    }
  }

  Observable share() const {
    return {d};
  }
};

template <typename T>
class Subject final {
  using ObserverCore = observerInternal::ObserverCore<T>;
  std::shared_ptr<ObserverCore> d = std::make_shared<ObserverCore>();
  explicit Subject(std::shared_ptr<ObserverCore> shallowCore)
      : d(std::move(shallowCore)) {}

 public:
  using FCallback = typename ObserverCore::FCallback;
  Subject() = default;
  // Keep the returned pointer to keep receiving callbacks
  [[nodiscard]] Subscription subscribe(FCallback &&callback) {
    return ObserverCore::subscribe(d, std::forward<FCallback>(callback));
  }

  void setMemorySize(std::size_t const nMemory) {
    d->setMemorySize(nMemory);
  }

  [[nodiscard]] Observable<T> asObservable() const { return {d}; }

  Subject share() const {
    return Subject{d};
  }

  template <class U = T, observerInternal::EnableIfVoid<U>...>
  Subject &next() {
    d->next();
    return *this;
  }

  template <class U = T, observerInternal::EnableIfNotVoid<U>...>
  Subject &next(U value) {
    d->next(std::move(value));
    return *this;
  }
};
}  // namespace csari
