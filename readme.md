# Simple observer pattern

Single header only, asynchronous observer structure. Connection is broken when subject or subscriber gets out of scope. Connection itself is thread-safe.

# How to use

Add `include/csari/observer.hpp` to your project.

## Example

```cpp
#include <csari/observer.hpp>
#include <iostream>

int main() {
  csari::Subject<int> subject;
  auto subscription = subject.subscribe(
      [](int value) { std::cout << "Received: " << value << '\n'; });
  for (auto i = 0; i < 7; i += 1) {
    subject.next(i);
  }
  return 0;
}
```

More examples are in unit tests `test/src/unit.cpp`.

## Option with no C++17 features
Simply remove `[[nodiscard]]` on `subscribe` function. But remember, you should capture the subscription, or else you will break the subscription in the same line.

# Caution
Callback functions are called on the sender's thread; i.e. this library does not provide an event loop. If the sender can be in a different thread, please ensure that callback functions are thread-safe.

# Similar libraries

[RxCpp](https://github.com/ReactiveX/RxCpp): Long learning curve and a little too big for my taste. It has A LOT of features. I definitely recommend going with RxCpp for new projects with a need for a strong event architecture.

[Boost](https://www.boost.org/doc/libs/1_63_0/doc/html/signals2.html): Boost has a solution for almost everything. It has lots of features but also carries a lot of the dependencies with it. Use it instead if you already have boost in your system.

[Qt](https://doc.qt.io/qt-5/signalsandslots.html): Qt has its own event loop and signals and slots. It has a lot of dependencies which may or may not be necessary for your system.

[Daniel Dinu's Observable](https://github.com/ddinu/observable): Started off with a very similar fashion, Daniel Dinu also has a simple observable library. His library, however, did not have two features I needed at the time I wrote this one: thread-safety and (more than one) memory.