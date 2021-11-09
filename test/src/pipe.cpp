#include <catch.hpp>
#include <csari/observer.hpp>

TEST_CASE("pipeSubject") {
  auto superSubject1 = csari::Subject<double, double>{};
  auto superSubject2 = csari::Subject<double>{};
  auto pipe = csari::Pipe<double>{};

  pipe.createPipe(superSubject1.asObservable(),
                  [](csari::Subject<double>& pipeSubject, double const val1,
                     double const val2) {
                    // filter
                    if (val1 > 0.) {
                      // transform
                      pipeSubject.next(val1 + val2);
                    }
                  });
  pipe.createPipe(superSubject2.asObservable(),
                  [](csari::Subject<double>& pipeSubject, double const val) {
                    pipeSubject.next(val / 2.);
                  });

  auto pipeCounter = 0U;
  auto const sub = pipe.subscribe([&pipeCounter](double const val) {
    REQUIRE(val == 4.5);
    ++pipeCounter;
  });

  // Passes
  superSubject1.next(0.1, 4.4);
  // Gets filtered
  superSubject1.next(-0.1, 4);
  // Passes
  superSubject2.next(9.);

  REQUIRE(pipeCounter == 2U);
}
