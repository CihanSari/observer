#include "gtest/gtest.h"

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