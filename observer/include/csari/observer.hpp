#pragma once
#include <any>
#include <deque>
#include <functional>
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
template <typename T>
struct ObserverCore {
  using FCallback = std::function<void(T)>;
  // Listeners
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
    std::unique_lock<std::mutex> lock(m_mutex);
    m_nMemory = nMemory;
    auto &memoryQue = std::any_cast<std::deque<T> &>(m_memory);
    while (memoryQue.size() > m_nMemory) {
      memoryQue.pop_front();
    }
  }

  template <class U = T, EnableIfNotVoid<U>...>
  void callbackFromMemory(FCallback const callback) {
    auto memoryQue = std::any_cast<std::deque<T>>(this->m_memory);
    for (auto &instance : memoryQue) {
      callback(std::move(instance));
    }
  }

  template <class U = T, EnableIfNotVoid<U>...>
  void next(U value) {
    std::unique_lock<std::mutex> lock(m_mutex);
    if (m_nMemory > 0) {
      auto &memory = std::any_cast<std::deque<T> &>(m_memory);
      if (memory.size() == m_nMemory) {
        memory.pop_front();
      }
      memory.emplace_back(value);
    }
    std::vector<FCallback> callbacks;
    for (auto &callback : m_map) {
      callbacks.emplace_back(callback.second);
    }
    lock.unlock();
    if (callbacks.size() > 0) {
      auto lastCallback = callbacks.back();
      callbacks.pop_back();
      for (auto &callback : callbacks) {
        callback(value);
      }
      lastCallback(std::move(value));
    }
  }

  template <class U = T, EnableIfVoid<U>...>
  void makeMemory() {
    m_memory = std::size_t{0};
  }

  template <class U = T, EnableIfVoid<U>...>
  void setMemorySize(std::size_t nMemory) {
    std::unique_lock<std::mutex> lock(m_mutex);
    m_nMemory = nMemory;
    auto const memorySize = std::any_cast<std::size_t &>(m_memory);
    if (memorySize > m_nMemory) {
      this->memory = m_nMemory;
    }
  }

  template <class U = T, EnableIfVoid<U>...>
  void callbackFromMemory(FCallback callback) {
    auto const m_memorySize = std::any_cast<std::size_t &>(this->m_memory);
    for (auto i = std::size_t{0}; i < m_memorySize; ++i) {
      callback();
    }
  }

  template <class U = T, EnableIfVoid<U>...>
  void next() {
    std::unique_lock<std::mutex> lock(m_mutex);
    auto &memory = std::any_cast<std::size_t &>(m_memory);
    if (memory < m_nMemory) {
      ++memory;
    }
    std::vector<FCallback> callbacks;
    for (auto &callback : m_map) {
      callbacks.emplace_back(callback.second);
    }
    lock.unlock();
    for (auto &callback : callbacks) {
      callback();
    }
  }

  ObserverCore() { makeMemory<T>(); }

  static Subscription subscribe(std::shared_ptr<ObserverCore> d,
                                FCallback callback) {
    // Subject is still alive, we can proceed
    std::unique_lock<std::mutex> lock(d->m_mutex);
    auto idx = d->m_callbackCounter++;
    d->m_map.emplace(idx, callback);
    std::vector<FCallback> callbacks;
    lock.unlock();
    d->callbackFromMemory(callback);
    // return a scope-guard, which will unsubscribe when the object is
    // discarded (out of scope) or manually released
    return Subscription{(void *)3,
                        [idx, w_d = std::weak_ptr<ObserverCore>(d)](void *) {
                          if (auto d = w_d.lock()) {
                            // Subject is still alive, we should unsubscribe
                            std::unique_lock<std::mutex> lock(d->m_mutex);
                            auto it = d->m_map.find(idx);
                            if (it != d->m_map.end()) {
                              d->m_map.erase(it);
                            }
                          }
                        }};
  }
};

template <typename T>
class Observable {
  std::weak_ptr<ObserverCore<T>> d;

 public:
  using FCallback = typename ObserverCore<T>::FCallback;
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
class Subject {
  std::shared_ptr<ObserverCore<T>> d = std::make_shared<ObserverCore<T>>();
  Subject(std::shared_ptr<ObserverCore<T>> shallowCore) : d(shallowCore) {}

 public:
  using FCallback = typename ObserverCore<T>::FCallback;
  Subject() = default;
  // Keep the returned pointer to keep receiving callbacks
  [[nodiscard]] Subscription subscribe(FCallback callback) {
    return ObserverCore<T>::subscribe(d, std::move(callback));
  }

  void setMemorySize(std::size_t const nMemory) {
    d->setMemorySize(nMemory);
  }

  [[nodiscard]] Observable<T> asObservable() const { return {d}; }

  Subject share() const {
    return {d};
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
