#include "gtest/gtest.h"

#include <array>
#include <csari/observer.hpp>

TEST(UnitTests, ObserverDiesEarly) {
  using namespace std::chrono_literals;
  auto subject = std::make_unique<csari::Subject<int>>();
  auto senderThread = std::thread([&subject] {
    for (auto i = 0; i < 7; i += 1) {
      std::cout << "[Sender] Sent " << i << '\n';
      subject->next(i);
      std::this_thread::sleep_for(5ms);
    }
    subject.reset();
    std::cout << "[Sender] Closed\n";
  });
  auto receiverThread = std::thread{[ptrSubject = subject.get()] {
    std::this_thread::sleep_for(5ms);
    std::cout << "[Receiver] Subscribed\n";
    auto const subscription = ptrSubject->subscribe([](int const value) {
      std::cout << "[Receiver] Received " << value << '\n';
    });
    std::this_thread::sleep_for(20ms);
    std::cout << "[Receiver] Unsubscribed\n";
  }};
  if (receiverThread.joinable()) {
    receiverThread.join();
  }
  if (senderThread.joinable()) {
    senderThread.join();
  }
}

TEST(UnitTests, SubjectDiesEarly) {
  using namespace std::chrono_literals;
  auto subject = std::make_unique<csari::Subject<int>>();
  auto senderThread = std::thread([&subject] {
    for (auto i = 0; i < 5; i += 1) {
      std::cout << "[Sender] Sent " << i << '\n';
      subject->next(i);
      std::this_thread::sleep_for(5ms);
    }
    subject.reset();
    std::cout << "[Sender] Closed\n";
  });
  auto receiverThread = std::thread{[ptrSubject = subject.get()] {
    std::this_thread::sleep_for(10ms);
    std::cout << "[Receiver] Subscribed\n";
    auto const subscription = ptrSubject->subscribe([](int const value) {
      std::cout << "[Receiver] Received " << value << '\n';
    });
    std::this_thread::sleep_for(10ms);
    std::cout << "[Receiver] Unsubscribed\n";
  }};
  if (receiverThread.joinable()) {
    receiverThread.join();
  }
  if (senderThread.joinable()) {
    senderThread.join();
  }
}

TEST(UnitTests, ObserverDiesEarlyWithMemory) {
  using namespace std::chrono_literals;
  auto subject = std::make_unique<csari::Subject<int>>();
  auto senderThread = std::thread([&subject] {
    subject->setMemorySize(2);
    for (auto i = 0; i < 10; i += 1) {
      std::cout << "[Sender] Sent " << i << '\n';
      subject->next(i);
      std::this_thread::sleep_for(5ms);
    }
    subject.reset();
    std::cout << "[Sender] Closed\n";
  });
  auto receiverThread = std::thread{[ptrSubject = subject.get()] {
    std::this_thread::sleep_for(50ms);
    std::cout << "[Receiver] Subscribed\n";
    auto const subscription = ptrSubject->subscribe([](int const value) {
      std::cout << "[Receiver] Received " << value << '\n';
    });
    std::this_thread::sleep_for(25ms);
    std::cout << "[Receiver] Unsubscribed\n";
  }};
  if (receiverThread.joinable()) {
    receiverThread.join();
  }
  if (senderThread.joinable()) {
    senderThread.join();
  }
}

TEST(UnitTests, SubjectDiesEarlyWithMemory) {
  using namespace std::chrono_literals;
  auto subject = std::make_unique<csari::Subject<int>>();
  auto senderThread = std::thread([&subject] {
    subject->setMemorySize(2);
    for (auto i = 0; i < 10; i += 1) {
      std::cout << "[Sender] Sent " << i << '\n';
      subject->next(i);
      std::this_thread::sleep_for(5ms);
    }
    subject.reset();
    std::cout << "[Sender] Closed\n";
  });
  auto receiverThread = std::thread{[ptrSubject = subject.get()] {
    std::this_thread::sleep_for(20ms);
    std::cout << "[Receiver] Subscribed\n";
    auto const subscription = ptrSubject->subscribe([](int const value) {
      std::cout << "[Receiver] Received " << value << '\n';
    });
    std::this_thread::sleep_for(20ms);
    std::cout << "[Receiver] Unsubscribed\n";
  }};
  if (receiverThread.joinable()) {
    receiverThread.join();
  }
  if (senderThread.joinable()) {
    senderThread.join();
  }
}

TEST(UnitTests, streu) {
  auto subject = csari::Subject<int>{};
  auto sub1Triggered = false, sub2Triggered = false, sub3Triggered = false;
  auto sub1 = subject.subscribe([& t = sub1Triggered](int const i) {
    t = true;
    std::cout << "sub1(" << i << ")\n";
  });
  auto const sub2 = subject.subscribe([& t = sub2Triggered](int const i) {
    t = true;
    std::cout << "sub2(" << i << ")\n";
  });
  sub1.reset();
  auto const sub3 = subject.subscribe([& t = sub3Triggered](int const i) {
    t = true;
    std::cout << "sub3(" << i << ")\n";
  });
  subject.next(42);
  EXPECT_FALSE(sub1Triggered);
  EXPECT_TRUE(sub2Triggered);
  EXPECT_TRUE(sub3Triggered);
}

TEST(UnitTests, voip_geek) {
  auto subject = csari::Subject<int>{};
  subject.setMemorySize(3);
  subject.next(0).next(1).next(2).next(3);
  subject.setMemorySize(2);
  auto triggers = std::array{false, false, false, false};
  auto const sub1 =
      subject.subscribe([&triggers](int const i) { triggers.at(i) = true; });
  EXPECT_FALSE(triggers.at(0));
  EXPECT_FALSE(triggers.at(1));
  EXPECT_TRUE(triggers.at(2));
  EXPECT_TRUE(triggers.at(3));
}

TEST(UnitTests, voidCall) {
  auto subject = csari::Subject<void>{};
  auto triggered = false;
  auto const sub1 = subject.subscribe([&triggered] { triggered = true; });
  EXPECT_FALSE(triggered);
  subject.next();
  EXPECT_TRUE(triggered);
}

TEST(UnitTests, unsubscribeOnCallback) {
  auto subject = csari::Subject<void>{};
  auto triggered = false;
  auto sub1 = subject.subscribe([&triggered] { triggered = true; });
  auto const sub2 = subject.subscribe([&sub1] { sub1.reset(); });
  EXPECT_FALSE(triggered);
  subject.next();
  EXPECT_TRUE(triggered);
}

TEST(UnitTests, observableLifeTimeTests) {
  auto const observable = [] {
    auto subject = csari::Subject<int>{};
    auto observable = subject.asObservable();
    EXPECT_TRUE(observable.isAlive());
    return observable;
  }();
  EXPECT_FALSE(observable.isAlive());
}

TEST(UnitTests, observableSubscriptionTests1) {
  auto subject = csari::Subject<void>{};
  auto observable = subject.asObservable();
  auto triggered = false;
  auto sub1 = observable.subscribe([&triggered] { triggered = true; });
  auto const sub2 = observable.subscribe([&sub1] { sub1.reset(); });
  EXPECT_FALSE(triggered);
  subject.next();
  EXPECT_TRUE(triggered);
}

TEST(UnitTests, observableShallowCopyTests1) {
  auto s1 = std::make_unique<csari::Subject<std::any>>();
  auto const s2 = s1->share();
  auto const o1 = s1->asObservable();
  s1.reset();
  // s2 is completely functional and o1 is still connected to s2
  auto o11 = o1.share();
  auto const o2 = s2.asObservable();
  auto o21 = o2.share();
  auto triggered = false;
  auto sub1 = o11.subscribe([&triggered](auto) { triggered = true; });
  auto const sub2 = o21.subscribe([&sub1](auto) { sub1.reset(); });
  EXPECT_FALSE(triggered);
  s2.share().next({});
  EXPECT_TRUE(triggered);
}

TEST(UnitTests, voidMemoryTests) {
  auto subject = csari::Subject<int>{};
  subject.setMemorySize(30);
  for (auto i = std::size_t{0}; i < std::size_t{20}; i += 1) {
    subject.next(42);
  }
  class SimpleCounter final {
   public:
    explicit SimpleCounter(csari::Subject<int>& subject)
        : counter{0},
          sub(subject.subscribe([& counter = counter](auto) { ++counter; })) {}
    int operator()() const { return counter; }

   private:
    int counter;
    csari::Subscription sub;
  };
  auto const firstCallCounter = SimpleCounter{subject};
  subject.setMemorySize(10);
  auto const secondCallCounter = SimpleCounter{subject};
  subject.setMemorySize(5);
  auto const thirdCallCounter = SimpleCounter{subject};
  subject.setMemorySize(0);
  auto const fourthCallCounter = SimpleCounter{subject};

  EXPECT_EQ(firstCallCounter(), 20);
  EXPECT_EQ(secondCallCounter(), 10);
  EXPECT_EQ(thirdCallCounter(), 5);
  EXPECT_EQ(fourthCallCounter(), 0);

  for (auto i = std::size_t{0}; i < std::size_t{20}; i += 1) {
    subject.next(42);
  }

  auto const fifthCallCounter = SimpleCounter{subject};

  EXPECT_EQ(firstCallCounter(), 40);
  EXPECT_EQ(secondCallCounter(), 30);
  EXPECT_EQ(thirdCallCounter(), 25);
  EXPECT_EQ(fourthCallCounter(), 20);
  EXPECT_EQ(fifthCallCounter(), 0);
}

TEST(UnitTests, voidSubjectOperatorCallTests) {
  auto constexpr nCallsToMake = 10;

  auto subject = csari::Subject<void>{};
  subject.setMemorySize(nCallsToMake);

  auto callCounter = std::size_t{0};
  auto const sub = subject.subscribe([&callCounter] { ++callCounter; });

  for (auto i = std::size_t{0}; i < nCallsToMake; ++i) {
    subject();
  }

  EXPECT_EQ(nCallsToMake, callCounter);
}

TEST(UnitTests, nonVoidSubjectOperatorCallTests) {
  auto const cacheValues = std::array{std::rand(), std::rand(), std::rand(),
                                      std::rand(), std::rand()};

  auto subject = csari::Subject<int>{};
  subject.setMemorySize(cacheValues.size());

  // Fill subject with junk values
  for (auto i = std::size_t{0}; i < cacheValues.size(); ++i) {
    subject << std::rand();
  }

  auto returnedValues = std::vector<int>{};
  returnedValues.reserve(cacheValues.size());

  // Refill subject with correct values
  std::for_each(cacheValues.begin(), cacheValues.end(),
                [&subject](int const val) { subject << val; });

  auto const sub = subject.subscribe(
      [&returnedValues](int const val) { returnedValues.emplace_back(val); });

  // Check if the contets are the same.
  EXPECT_TRUE(std::equal(cacheValues.begin(), cacheValues.end(),
                         returnedValues.begin(), returnedValues.end()));
}

