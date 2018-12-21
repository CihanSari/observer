#pragma once
#include <algorithm>
#include <any>
#include <deque>
#include <memory>
#include <mutex>
#include <optional>
#include <unordered_map>

namespace csari {
using Subscription = std::shared_ptr<void>;
template <typename U>
using EnableIfNotVoid =
    typename std::enable_if<!std::is_same<U, void>::value>::type;
template <typename U>
using EnableIfVoid =
    typename std::enable_if<std::is_same<U, void>::value>::type;

template <class Sig>
struct Invokable;

namespace invokableInternal {
template <class T>
struct EmplaceAs final {};
}  // namespace invokableInternal
template <class... Args>
struct Invokable<void(Args...)> final {
  // can be default constructed and moved:
  Invokable() = default;
  Invokable(Invokable &&) = default;
  Invokable &operator=(Invokable &&) = default;
  Invokable(Invokable const &) noexcept = default;
  Invokable &operator=(Invokable const &) noexcept = default;
  ~Invokable() = default;

  // implicitly create from a type that can be compatibly invoked
  // and isn't a Invokable itself
  template <class F, std::enable_if_t<
                         !std::is_same<std::decay_t<F>, Invokable>{}, int> = 0>

  Invokable(F f)
      : Invokable(invokableInternal::EmplaceAs<std::decay_t<F>>{},
                  std::move(f)) {}
  // emplacement construct using the EmplaceAs tag type:
  template <class F, class... FArgs>
  Invokable(invokableInternal::EmplaceAs<F>, FArgs &&... functionArgs) {
    rebind<F>(std::forward<FArgs>(functionArgs)...);
  }
  // invoke in the case where R is void:
  void operator()(Args... args) {
    m_invoke(m_ptr.get(), std::forward<Args>(args)...);
  }

  // empty the Invokable:
  void clear() {
    m_invoke = nullptr;
    m_ptr.reset();
  }

  // test if it is non-empty:
  explicit operator bool() const { return static_cast<bool>(m_ptr); }

  // change what the invokable contains:
  template <class F, class... FArgs>
  void rebind(FArgs &&... functionArgs) {
    m_ptr = std::make_shared<F>(std::forward<FArgs>(functionArgs)...);
    m_invoke = [](void *pf, Args... args) {
      (*static_cast<F *>(pf))(std::forward<Args>(args)...);
    };
  }

 private:
  // storage for the invokable and an invoker function pointer:
  std::shared_ptr<void> m_ptr{};
  void (*m_invoke)(void *, Args...) = nullptr;
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
    auto const lock = std::unique_lock<std::mutex>(m_mutex);
    m_nMemory = nMemory;
    auto &memoryQue = std::any_cast<std::deque<T> &>(m_memory);
    while (memoryQue.size() > m_nMemory) {
      memoryQue.pop_front();
    }
  }

  template <class U = T, EnableIfNotVoid<U>...>
  void callbackFromMemory(FCallback &callback) {
    auto &memoryQue = std::any_cast<std::deque<T> &>(m_memory);
    std::for_each(memoryQue.cbegin(), memoryQue.cend(),
                  [&callback](T instance) { callback(std::move(instance)); });
  }

  template <class U = T, EnableIfNotVoid<U>...>
  void next(U value) {
    auto lock = std::unique_lock<std::mutex>(m_mutex);
    if (m_nMemory > 0) {
      auto &memory = std::any_cast<std::deque<T> &>(m_memory);
      if (memory.size() == m_nMemory) {
        memory.pop_front();
      }
      memory.emplace_back(value);
    }
    auto callbacks = std::vector<FCallback>{};
    callbacks.reserve(m_map.size());
    std::transform(m_map.cbegin(), m_map.cend(), std::back_inserter(callbacks),
                   [](std::pair<std::size_t, FCallback> const &callbackPair) {
                     return callbackPair.second;
                   });
    lock.unlock();
    if (!callbacks.empty()) {
      auto lastCallback = callbacks.back();
      callbacks.pop_back();
      std::for_each(callbacks.begin(), callbacks.end(),
                    [&value](auto &callback) { callback(value); });
      lastCallback(std::move(value));
    }
  }

  template <class U = T, EnableIfVoid<U>...>
  void makeMemory() {
    m_memory = std::size_t{0};
  }

  template <class U = T, EnableIfVoid<U>...>
  void setMemorySize(std::size_t nMemory) {
    auto const lock = std::unique_lock<std::mutex>(m_mutex);
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
    auto lock = std::unique_lock<std::mutex>(m_mutex);
    auto &memory = std::any_cast<std::size_t &>(m_memory);
    if (memory < m_nMemory) {
      ++memory;
    }
    auto callbacks = std::vector<FCallback>{};
    callbacks.reserve(m_map.size());
    std::transform(m_map.cbegin(), m_map.cend(), std::back_inserter(callbacks),
                   [](std::pair<std::size_t, FCallback> const &callbackPair) {
                     return callbackPair.second;
                   });
    lock.unlock();
    std::for_each(callbacks.begin(), callbacks.end(),
                  [](auto &callback) { callback(); });
  }

  ObserverCore() { makeMemory<T>(); }

  static Subscription subscribe(std::shared_ptr<ObserverCore> d,
                                FCallback callback) {
    // Subject is still alive, we can proceed
    auto lock = std::unique_lock<std::mutex>(d->m_mutex);
    auto idx = d->m_callbackCounter++;
    d->m_map.emplace(idx, callback);
    lock.unlock();
    d->callbackFromMemory(callback);
    // return a scope-guard, which will unsubscribe when the object is
    // discarded (out of scope) or manually released
    struct SubscriptionCleanUp final {
      SubscriptionCleanUp(std::size_t const idx,
                          std::weak_ptr<ObserverCore> weakD)
          : idx(idx), weakD(std::move(weakD)) {}
      ~SubscriptionCleanUp() {
        if (auto d = weakD.lock()) {
          // Subject is still alive, we should unsubscribe
          auto const lock = std::unique_lock<std::mutex>(d->m_mutex);
          auto it = d->m_map.find(idx);
          if (it != d->m_map.end()) {
            d->m_map.erase(it);
          }
        }
      }
      std::size_t idx;
      std::weak_ptr<ObserverCore> weakD;
    };
    return Subscription{std::make_shared<SubscriptionCleanUp>(idx, d)};
  }
};

template <typename T>
class Observable final {
  std::weak_ptr<ObserverCore<T>> d;

 public:
  using FCallback = typename ObserverCore<T>::FCallback;
  Observable() = default;
  Observable(std::weak_ptr<ObserverCore<T>> data) : d(std::move(data)) {}

  bool isAlive() const { return !d.expired(); }

  [[nodiscard]] std::optional<Subscription> subscribe(FCallback callback) {
    if (auto s_d = d.lock()) {
      return ObserverCore<T>::subscribe(s_d, std::move(callback));
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
  std::shared_ptr<ObserverCore<T>> d = std::make_shared<ObserverCore<T>>();
  explicit Subject(std::shared_ptr<ObserverCore<T>> shallowCore) : d(shallowCore) {}

 public:
  using FCallback = typename ObserverCore<T>::FCallback;
  Subject() = default;
  // Keep the returned pointer to keep receiving callbacks
  [[nodiscard]] Subscription subscribe(FCallback &&callback) {
    return ObserverCore<T>::subscribe(d, std::forward<FCallback>(callback));
  }

  void setMemorySize(std::size_t const nMemory) {
    d->setMemorySize(nMemory);
  }

  [[nodiscard]] Observable<T> asObservable() const { return {d}; }

  Subject share() const {
    return Subject{d};
  }

  template <class U = T, EnableIfVoid<U>...>
  Subject &next() {
    d->next();
    return *this;
  }

  template <class U = T, EnableIfNotVoid<U>...>
  Subject &next(U value) {
    d->next(std::move(value));
    return *this;
  }
};
}  // namespace csari
