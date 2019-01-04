#include "gtest/gtest.h"

#include <array>
#include <csari/observer.hpp>

TEST(UnitTests, ObserverDiesEarly) {
  auto subject = std::make_unique<csari::Subject<int>>();
  auto senderThread = std::thread([&subject] {
    for (auto i = 0; i < 7; i += 1) {
      std::cout << "[Sender] Sent " << i << '\n';
      subject->next(i);
      std::this_thread::sleep_for(std::chrono::milliseconds{5});
    }
    subject.reset();
    std::cout << "[Sender] Closed\n";
  });
  auto receiverThread = std::thread{[ptrSubject = subject.get()] {
    std::this_thread::sleep_for(std::chrono::milliseconds{5});
    std::cout << "[Receiver] Subscribed\n";
    auto subscription = ptrSubject->subscribe([](int const value) {
      std::cout << "[Receiver] Received " << value << '\n';
    });
    std::this_thread::sleep_for(std::chrono::milliseconds{20});
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
  auto subject = std::make_unique<csari::Subject<int>>();
  auto senderThread = std::thread([&subject] {
    for (auto i = 0; i < 5; i += 1) {
      std::cout << "[Sender] Sent " << i << '\n';
      subject->next(i);
      std::this_thread::sleep_for(std::chrono::milliseconds{5});
    }
    subject.reset();
    std::cout << "[Sender] Closed\n";
  });
  auto receiverThread = std::thread{[ptrSubject = subject.get()] {
    std::this_thread::sleep_for(std::chrono::milliseconds{10});
    std::cout << "[Receiver] Subscribed\n";
    auto subscription = ptrSubject->subscribe([](int const value) {
      std::cout << "[Receiver] Received " << value << '\n';
    });
    std::this_thread::sleep_for(std::chrono::milliseconds{10});
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
  auto subject = std::make_unique<csari::Subject<int>>();
  auto senderThread = std::thread([&subject] {
    subject->setMemorySize(2);
    for (auto i = 0; i < 10; i += 1) {
      std::cout << "[Sender] Sent " << i << '\n';
      subject->next(i);
      std::this_thread::sleep_for(std::chrono::milliseconds{5});
    }
    subject.reset();
    std::cout << "[Sender] Closed\n";
  });
  auto receiverThread = std::thread{[ptrSubject = subject.get()] {
    std::this_thread::sleep_for(std::chrono::milliseconds{50});
    std::cout << "[Receiver] Subscribed\n";
    auto subscription = ptrSubject->subscribe([](int const value) {
      std::cout << "[Receiver] Received " << value << '\n';
    });
    std::this_thread::sleep_for(std::chrono::milliseconds{25});
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
  auto subject = std::make_unique<csari::Subject<int>>();

  auto senderThread = std::thread([&subject] {
    subject->setMemorySize(2);
    for (auto i = 0; i < 10; i += 1) {
      std::cout << "[Sender] Sent " << i << '\n';
      subject->next(i);
      std::this_thread::sleep_for(std::chrono::milliseconds{5});
    }
    subject.reset();
    std::cout << "[Sender] Closed\n";
  });
  auto receiverThread = std::thread{[ptrSubject = subject.get()] {
    std::this_thread::sleep_for(std::chrono::milliseconds{20});
    std::cout << "[Receiver] Subscribed\n";
    auto subscription = ptrSubject->subscribe([](int const value) {
      std::cout << "[Receiver] Received " << value << '\n';
    });
    std::this_thread::sleep_for(std::chrono::milliseconds{20});
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
  auto sub1 = subject.subscribe([&t = sub1Triggered](int const i) {
    t = true;
    std::cout << "sub1(" << i << ")\n";
  });
  auto const sub2 = subject.subscribe([&t = sub2Triggered](int const i) {
    t = true;
    std::cout << "sub2(" << i << ")\n";
  });
  sub1.reset();
  auto const sub3 = subject.subscribe([&t = sub3Triggered](int const i) {
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
  auto sub1 =
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
  auto subject = csari::Subject<int>{};
  auto o = subject.asObservable();
  auto triggered = false;
  auto sub1 = o.subscribe([&triggered](int) { triggered = true; });
  auto const sub2 = o.subscribe([&sub1](int) { sub1.reset(); });
  EXPECT_FALSE(triggered);
  subject.next(1);
  EXPECT_TRUE(triggered);
}

TEST(UnitTests, observableShallowCopyTests1) {
  auto s1 = std::make_unique<csari::Subject<int>>();
  auto s2 = s1->share();
  auto const o1 = s1->asObservable();
  s1.reset();
  // s2 is completely functional and o1 is still connected to s2
  auto o11 = o1.share();
  auto o2 = s2.asObservable();
  auto o21 = o2.share();
  auto triggered = false;
  auto sub1 = o11.subscribe([&triggered](int) { triggered = true; });
  auto const sub2 = o21.subscribe([&sub1](int) { sub1.reset(); });
  EXPECT_FALSE(triggered);
  s2.share().next(1);
  EXPECT_TRUE(triggered);
}

TEST(UnitTests, voidMemoryTests) {
  auto subject = csari::Subject<void>{};
  subject.setMemorySize(30);
  for (auto i = std::size_t{0}; i < std::size_t{20}; i += 1) {
    subject.next();
  }
  class SimpleCounter final {
   public:
    explicit SimpleCounter(csari::Subject<void> &subject)
        : counter{0}, sub(subject.subscribe([this] { ++counter; })) {}
    int operator()() const { return counter; }

   private:
    int counter;
    csari::Subscription sub;
  };
  auto const firstCallCounter = SimpleCounter{subject};
  EXPECT_EQ(firstCallCounter(), 20);
  subject.setMemorySize(10);
  auto const secondCallCounter = SimpleCounter{subject};
  EXPECT_EQ(secondCallCounter(), 10);
  subject.setMemorySize(5);
  auto const thirdCallCounter = SimpleCounter{subject};
  EXPECT_EQ(thirdCallCounter(), 5);
  for (auto i = std::size_t{0}; i < std::size_t{20}; i += 1) {
    subject.next();
  }
  subject.setMemorySize(0);
  auto const fourthCallCounter = SimpleCounter{subject};
  EXPECT_EQ(firstCallCounter(), 40);
  EXPECT_EQ(secondCallCounter(), 30);
  EXPECT_EQ(thirdCallCounter(), 25);
  EXPECT_EQ(fourthCallCounter(), 0);
}
