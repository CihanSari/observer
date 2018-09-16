#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <unordered_map>

namespace csari {
using Subscription = std::shared_ptr<void>;
template <typename T>
class Subject {
  using FCallback = std::function<void(T)>;
  struct ObserverData {
    std::unordered_map<std::size_t, FCallback> map;
    std::mutex mutex;
    std::deque<T> memory;
    std::size_t nMemory{0u};
    std::size_t callbackCounter{0u};
  };
  std::shared_ptr<ObserverData> d = std::make_shared<ObserverData>();

 public:
  // Keep the returned pointer to keep receiving callbacks
  [[nodiscard]] Subscription subscribe(FCallback callback) {
    std::unique_lock<std::mutex> lock(d->mutex);
    auto idx = d->callbackCounter++;
    d->map.emplace(idx, callback);
    for (auto &memory : d->memory) {
      callback(memory);
    }
    lock.unlock();
    // return a scope-guard, which will unsubscribe when the object is discarded
    // (out of scope) or manually released
    return Subscription{(void *)3,
                        [idx, w_d = std::weak_ptr<ObserverData>(d)](void *) {
                          if (auto d = w_d.lock()) {
                            // Subject is still alive, we should unsubscribe
                            std::unique_lock<std::mutex> lock(d->mutex);
                            auto it = d->map.find(idx);
                            if (it != d->map.end()) {
                              d->map.erase(it);
                            }
                          }
                        }};
  }

  Subject &next(T value) {
    std::unique_lock<std::mutex> lock(d->mutex);
    if (d->nMemory > 0) {
      if (d->memory.size() == d->nMemory) {
        d->memory.pop_front();
      }
      d->memory.emplace_back(value);
    }
    for (auto &callback : d->map) {
      callback.second(value);
    }
    return *this;
  }

  void setMemorySize(std::size_t nMemory) {
    std::unique_lock<std::mutex> lock(d->mutex);
    d->nMemory = nMemory;
    while (d->memory.size() > d->nMemory) {
      d->memory.pop_front();
    }
  }
};
}  // namespace csari
