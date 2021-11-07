# Simple observer pattern

Single header only, asynchronous observer structure. Connection is broken when subject or subscriber gets out of scope. Connection itself is thread-safe.

[![Build Status](https://github.com/CihanSari/observer/actions/workflows/main.yml/badge.svg)](https://github.com/CihanSari/observer/actions/workflows/main.yml)

# How to use

Add the header `observer/include/csari/observer.hpp` to your project.

## Example
```cpp
auto subject = csari::Subject<int>{};
auto const subscription = subject.subscribe(
  [](int const value) { 
    // Do something... 
  });
subject << 42;
```

More uses are in unit tests `test/src/unit.cpp`.

# Caution
Callback functions are called on the sender's thread; i.e. this library does not provide an event loop. If the sender can be in a different thread, please ensure that callback functions are thread-safe.

# Similar libraries

[RxCpp](https://github.com/ReactiveX/RxCpp): Long learning curve and a little too big for my taste. It has A LOT of features. I definitely recommend going with RxCpp for new projects with a need for a strong event architecture.

[Boost](https://www.boost.org/doc/libs/1_63_0/doc/html/signals2.html): Boost has a solution for almost everything. It has lots of features but also carries a lot of the dependencies with it. Use it instead if you already have boost in your system.

[Qt](https://doc.qt.io/qt-5/signalsandslots.html): Qt has its own event loop and signals and slots. It has a lot of dependencies which may or may not be necessary for your system.

[Daniel Dinu's Observable](https://github.com/ddinu/observable): Started off with a very similar fashion, Daniel Dinu also has a simple observable library. His library, however, did not have two features I needed at the time I wrote this one: thread-safety and (more than one) memory.