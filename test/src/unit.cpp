#include "gtest/gtest.h"

#include <array>
#include <csari/observer.hpp>

TEST(UnitTests, ObserverDiesEarly) {
  csari::Subject<int> *ptrSubject = nullptr;
  auto senderThread = std::thread([&ptrSubject] {
    auto subject = std::make_unique<csari::Subject<int>>();
    ptrSubject = subject.get();
    for (auto i = 0; i < 7; i += 1) {
      std::cout << "[Sender] Sent " << i << '\n';
      ptrSubject->next(i);
      std::this_thread::sleep_for(std::chrono::milliseconds{5});
    }
    std::cout << "[Sender] Closed\n";
  });
  auto receiverThread = std::thread{[&ptrSubject] {
    std::this_thread::sleep_for(std::chrono::milliseconds{5});
    std::cout << "[Receiver] Subscribed\n";
    auto subscription = ptrSubject->subscribe([](int value) {
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
  csari::Subject<int> *ptrSubject = nullptr;
  auto senderThread = std::thread([&ptrSubject] {
    auto subject = std::make_unique<csari::Subject<int>>();
    ptrSubject = subject.get();
    for (auto i = 0; i < 5; i += 1) {
      std::cout << "[Sender] Sent " << i << '\n';
      ptrSubject->next(i);
      std::this_thread::sleep_for(std::chrono::milliseconds{5});
    }
    std::cout << "[Sender] Closed\n";
  });
  auto receiverThread = std::thread{[&ptrSubject] {
    std::this_thread::sleep_for(std::chrono::milliseconds{10});
    std::cout << "[Receiver] Subscribed\n";
    auto subscription = ptrSubject->subscribe([](int value) {
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
  csari::Subject<int> *ptrSubject = nullptr;
  auto senderThread = std::thread([&ptrSubject] {
    auto subject = std::make_unique<csari::Subject<int>>();
    subject->setMemorySize(2);
    ptrSubject = subject.get();
    for (auto i = 0; i < 10; i += 1) {
      std::cout << "[Sender] Sent " << i << '\n';
      ptrSubject->next(i);
      std::this_thread::sleep_for(std::chrono::milliseconds{5});
    }
    std::cout << "[Sender] Closed\n";
  });
  auto receiverThread = std::thread{[&ptrSubject] {
    std::this_thread::sleep_for(std::chrono::milliseconds{50});
    std::cout << "[Receiver] Subscribed\n";
    auto subscription = ptrSubject->subscribe([](int value) {
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
  csari::Subject<int> *ptrSubject = nullptr;
  auto senderThread = std::thread([&ptrSubject] {
    auto subject = std::make_unique<csari::Subject<int>>();
    subject->setMemorySize(2);
    ptrSubject = subject.get();
    for (auto i = 0; i < 10; i += 1) {
      std::cout << "[Sender] Sent " << i << '\n';
      ptrSubject->next(i);
      std::this_thread::sleep_for(std::chrono::milliseconds{5});
    }
    std::cout << "[Sender] Closed\n";
  });
  auto receiverThread = std::thread{[&ptrSubject] {
    std::this_thread::sleep_for(std::chrono::milliseconds{20});
    std::cout << "[Receiver] Subscribed\n";
    auto subscription = ptrSubject->subscribe([](int value) {
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
  csari::Subject<int> s;
  bool sub1Triggered = false, sub2Triggered = false, sub3Triggered = false;
  csari::Subscription sub1 = s.subscribe([&t = sub1Triggered](int i) {
    t = true;
    std::cout << "sub1(" << i << ")\n";
  });
  csari::Subscription sub2 = s.subscribe([&t = sub2Triggered](int i) {
    t = true;
    std::cout << "sub2(" << i << ")\n";
  });
  sub1.reset();
  csari::Subscription sub3 = s.subscribe([&t = sub3Triggered](int i) {
    t = true;
    std::cout << "sub3(" << i << ")\n";
  });
  s.next(42);
  EXPECT_FALSE(sub1Triggered);
  EXPECT_TRUE(sub2Triggered);
  EXPECT_TRUE(sub3Triggered);
}

TEST(UnitTests, voip_geek) {
  csari::Subject<int> s;
  s.setMemorySize(3);
  s.next(0).next(1).next(2).next(3);
  s.setMemorySize(2);
  std::array<bool, 4> triggers{{false, false, false, false}};
  csari::Subscription sub1 =
      s.subscribe([&triggers](int i) { triggers.at(i) = true; });
  EXPECT_FALSE(triggers.at(0));
  EXPECT_FALSE(triggers.at(1));
  EXPECT_TRUE(triggers.at(2));
  EXPECT_TRUE(triggers.at(3));
}

TEST(UnitTests, voidCall) {
  csari::Subject<void> s;
  bool triggered = false;
  csari::Subscription sub1 = s.subscribe([&triggered] { triggered = true; });
  EXPECT_FALSE(triggered);
  s.next();
  EXPECT_TRUE(triggered);
}
