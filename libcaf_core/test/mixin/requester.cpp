/******************************************************************************
 *                       ____    _    _____                                   *
 *                      / ___|  / \  |  ___|    C++                           *
 *                     | |     / _ \ | |_       Actor                         *
 *                     | |___ / ___ \|  _|      Framework                     *
 *                      \____/_/   \_|_|                                      *
 *                                                                            *
 * Copyright 2011-2018 Dominik Charousset                                     *
 *                                                                            *
 * Distributed under the terms and conditions of the BSD 3-Clause License or  *
 * (at your option) under the terms and conditions of the Boost Software      *
 * License 1.0. See accompanying files LICENSE and LICENSE_ALTERNATIVE.       *
 *                                                                            *
 * If you did not receive a copy of the license files, see                    *
 * http://opensource.org/licenses/BSD-3-Clause and                            *
 * http://www.boost.org/LICENSE_1_0.txt.                                      *
 ******************************************************************************/

#define CAF_SUITE mixin.requester

#include "caf/mixin/requester.hpp"

#include "caf/test/dsl.hpp"

#include "caf/event_based_actor.hpp"

using namespace caf;

namespace {

using discarding_server_type = typed_actor<replies_to<int, int>::with<void>>;

using adding_server_type = typed_actor<replies_to<int, int>::with<int>>;

using result_type = variant<none_t, unit_t, int>;

struct fixture : test_coordinator_fixture<> {
  fixture() {
    result = std::make_shared<result_type>(none);
    discarding_server = make_server([](int, int) {});
    adding_server = make_server([](int x, int y) { return x + y; });
    run();
  }

  template <class F>
  auto make_server(F f)
    -> typed_actor<replies_to<int, int>::with<decltype(f(1, 2))>> {
    using impl = typed_actor<replies_to<int, int>::with<decltype(f(1, 2))>>;
    auto init = [f]() -> typename impl::behavior_type {
      return {
        [f](int x, int y) { return f(x, y); },
      };
    };
    return sys.spawn(init);
  }

  template <class T>
  T make_delegator(T dest) {
    auto f = [=](typename T::pointer self) -> typename T::behavior_type {
      return {
        [=](int x, int y) { return self->delegate(dest, x, y); },
      };
    };
    return sys.spawn<lazy_init>(f);
  }

  std::shared_ptr<result_type> result = std::make_shared<result_type>(none);
  discarding_server_type discarding_server;
  adding_server_type adding_server;
};

} // namespace

#define ERROR_HANDLER [&](error& err) { CAF_FAIL(sys.render(err)); }

#define SUBTEST(message)                                                       \
  *result = none;                                                              \
  run();                                                                       \
  CAF_MESSAGE("subtest: " message);                                            \
  for (int subtest_dummy = 0; subtest_dummy < 1; ++subtest_dummy)

CAF_TEST_FIXTURE_SCOPE(requester_tests, fixture)

CAF_TEST(requests without result) {
  auto server = discarding_server;
  SUBTEST("request.then") {
    auto client = sys.spawn([=](event_based_actor* self) {
      self->request(server, infinite, 1, 2).then([=] { *result = unit; });
    });
    run_once();
    expect((int, int), from(client).to(server).with(1, 2));
    expect((void), from(server).to(client));
    CAF_CHECK_EQUAL(*result, unit);
  }
  SUBTEST("request.await") {
    auto client = sys.spawn([=](event_based_actor* self) {
      self->request(server, infinite, 1, 2).await([=] { *result = unit; });
    });
    run_once();
    expect((int, int), from(client).to(server).with(1, 2));
    expect((void), from(server).to(client));
    CAF_CHECK_EQUAL(*result, unit);
  }
  SUBTEST("request.receive") {
    auto res_hdl = self->request(server, infinite, 1, 2);
    run();
    res_hdl.receive([&] { *result = unit; }, ERROR_HANDLER);
    CAF_CHECK_EQUAL(*result, unit);
  }
}

CAF_TEST(requests with integer result) {
  auto server = adding_server;
  SUBTEST("request.then") {
    auto client = sys.spawn([=](event_based_actor* self) {
      self->request(server, infinite, 1, 2).then([=](int x) { *result = x; });
    });
    run_once();
    expect((int, int), from(client).to(server).with(1, 2));
    expect((int), from(server).to(client).with(3));
    CAF_CHECK_EQUAL(*result, 3);
  }
  SUBTEST("request.await") {
    auto client = sys.spawn([=](event_based_actor* self) {
      self->request(server, infinite, 1, 2).await([=](int x) { *result = x; });
    });
    run_once();
    expect((int, int), from(client).to(server).with(1, 2));
    expect((int), from(server).to(client).with(3));
    CAF_CHECK_EQUAL(*result, 3);
  }
  SUBTEST("request.receive") {
    auto res_hdl = self->request(server, infinite, 1, 2);
    run();
    res_hdl.receive([&](int x) { *result = x; }, ERROR_HANDLER);
    CAF_CHECK_EQUAL(*result, 3);
  }
}

CAF_TEST(delegated request with integer result) {
  auto worker = adding_server;
  auto server = make_delegator(worker);
  auto client = sys.spawn([=](event_based_actor* self) {
    self->request(server, infinite, 1, 2).then([=](int x) { *result = x; });
  });
  run_once();
  expect((int, int), from(client).to(server).with(1, 2));
  expect((int, int), from(client).to(worker).with(1, 2));
  expect((int), from(worker).to(client).with(3));
  CAF_CHECK_EQUAL(*result, 3);
}

#ifndef CAF_NO_EXCEPTIONS

CAF_TEST(exceptions while processing requests trigger error messages) {
  auto worker = sys.spawn([] {
    return behavior{
      [](int) { throw std::runtime_error(""); },
    };
  });
  run();
  auto client = sys.spawn([worker](event_based_actor* self) {
    self->request(worker, infinite, 42).then([](int) {
      CAF_FAIL("unexpected handler called");
    });
  });
  run_once();
  expect((int), from(client).to(worker).with(42));
  expect((error), from(worker).to(client).with(sec::runtime_error));
}

#endif // CAF_NO_EXCEPTIONS

CAF_TEST_FIXTURE_SCOPE_END()
