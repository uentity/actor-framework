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

#include "caf/config.hpp"

#define CAF_SUITE serialization
#include "caf/test/unit_test.hpp"

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <functional>
#include <iostream>
#include <iterator>
#include <limits>
#include <list>
#include <locale>
#include <memory>
#include <new>
#include <set>
#include <sstream>
#include <stack>
#include <stdexcept>
#include <string>
#include <tuple>
#include <type_traits>
#include <typeinfo>
#include <vector>

#include "caf/actor_system.hpp"
#include "caf/actor_system_config.hpp"
#include "caf/binary_deserializer.hpp"
#include "caf/binary_serializer.hpp"
#include "caf/deserializer.hpp"
#include "caf/event_based_actor.hpp"
#include "caf/make_type_erased_tuple_view.hpp"
#include "caf/make_type_erased_view.hpp"
#include "caf/message.hpp"
#include "caf/message_handler.hpp"
#include "caf/primitive_variant.hpp"
#include "caf/proxy_registry.hpp"
#include "caf/ref_counted.hpp"
#include "caf/serializer.hpp"
#include "caf/stream_deserializer.hpp"
#include "caf/stream_serializer.hpp"
#include "caf/streambuf.hpp"
#include "caf/variant.hpp"

#include "caf/detail/get_mac_addresses.hpp"
#include "caf/detail/ieee_754.hpp"
#include "caf/detail/int_list.hpp"
#include "caf/detail/safe_equal.hpp"
#include "caf/detail/type_traits.hpp"

namespace {

enum class test_enum : uint32_t;
struct raw_struct;
struct test_array;
struct test_empty_non_pod;

} // namespace

CAF_BEGIN_TYPE_ID_BLOCK(serialization, first_custom_type_id)

  CAF_ADD_TYPE_ID(serialization, (raw_struct))
  CAF_ADD_TYPE_ID(serialization, (std::vector<bool>) )
  CAF_ADD_TYPE_ID(serialization, (test_array))
  CAF_ADD_TYPE_ID(serialization, (test_empty_non_pod))
  CAF_ADD_TYPE_ID(serialization, (test_enum))

CAF_END_TYPE_ID_BLOCK(serialization)

using namespace std;
using namespace caf;
using caf::detail::type_erased_value_impl;

namespace {

using strmap = map<string, u16string>;

struct raw_struct {
  string str;
};

template <class Inspector>
typename Inspector::result_type inspect(Inspector& f, raw_struct& x) {
  return f(x.str);
}

bool operator==(const raw_struct& lhs, const raw_struct& rhs) {
  return lhs.str == rhs.str;
}

enum class test_enum : uint32_t {
  a,
  b,
  c,
};

const char* test_enum_strings[] = {
  "a",
  "b",
  "c",
};

std::string to_string(test_enum x) {
  return test_enum_strings[static_cast<uint32_t>(x)];
}

struct test_array {
  int value[4];
  int value2[2][4];
};

template <class Inspector>
typename Inspector::result_type inspect(Inspector& f, test_array& x) {
  return f(x.value, x.value2);
}

struct test_empty_non_pod {
  test_empty_non_pod() = default;
  test_empty_non_pod(const test_empty_non_pod&) = default;
  test_empty_non_pod& operator=(const test_empty_non_pod&) = default;
  virtual void foo() {
    // nop
  }
  virtual ~test_empty_non_pod() {
    // nop
  }
};

template <class Inspector>
typename Inspector::result_type inspect(Inspector& f, test_empty_non_pod&) {
  return f();
}

class config : public actor_system_config {
public:
  config() {
    puts("add_message_types");
    add_message_types<id_block::serialization>();
  }
};

template <class Serializer, class Deserializer>
struct fixture {
  int32_t i32 = -345;
  int64_t i64 = -1234567890123456789ll;
  float f32 = 3.45f;
  double f64 = 54.3;
  duration dur = duration{time_unit::seconds, 123};
  timestamp ts = timestamp{timestamp::duration{1478715821 * 1000000000ll}};
  test_enum te = test_enum::b;
  string str = "Lorem ipsum dolor sit amet.";
  raw_struct rs;
  test_array ta{
    {0, 1, 2, 3},
    {{0, 1, 2, 3}, {4, 5, 6, 7}},
  };
  int ra[3] = {1, 2, 3};

  config cfg;
  actor_system system;
  message msg;
  message recursive;

  template <class T, class... Ts>
  vector<char> serialize(T& x, Ts&... xs) {
    vector<char> buf;
    binary_serializer sink{system, buf};
    if (auto err = sink(x, xs...))
      CAF_FAIL("serialization failed: "
               << system.render(err) << ", data: "
               << deep_to_string(std::forward_as_tuple(x, xs...)));
    return buf;
  }

  template <class T, class... Ts>
  void deserialize(const vector<char>& buf, T& x, Ts&... xs) {
    binary_deserializer source{system, buf};
    if (auto err = source(x, xs...))
      CAF_FAIL("deserialization failed: " << system.render(err));
  }

  // serializes `x` and then deserializes and returns the serialized value
  template <class T>
  T roundtrip(T x) {
    T result;
    deserialize(serialize(x), result);
    return result;
  }

  // converts `x` to a message, serialize it, then deserializes it, and
  // finally returns unboxed value
  template <class T>
  T msg_roundtrip(const T& x) {
    message result;
    auto tmp = make_message(x);
    deserialize(serialize(tmp), result);
    if (!result.match_elements<T>())
      CAF_FAIL("expected: " << x << ", got: " << result);
    return result.get_as<T>(0);
  }

  fixture() : system(cfg) {
    rs.str.assign(string(str.rbegin(), str.rend()));
    msg = make_message(i32, i64, dur, ts, te, str, rs);
    config_value::dictionary dict;
    put(dict, "scheduler.policy", atom("none"));
    put(dict, "scheduler.max-threads", 42);
    put(dict, "nodes.preload",
        make_config_value_list("sun", "venus", "mercury", "earth", "mars"));
    recursive = make_message(config_value{std::move(dict)});
  }
};

struct is_message {
  explicit is_message(message& msgref) : msg(msgref) {
    // nop
  }

  message& msg;

  template <class T, class... Ts>
  bool equal(T&& v, Ts&&... vs) {
    bool ok = false;
    // work around for gcc 4.8.4 bug
    auto tup = tie(v, vs...);
    message_handler impl{
      [&](T const& u, Ts const&... us) { ok = tup == tie(u, us...); },
    };
    impl(msg);
    return ok;
  }
};

} // namespace

#define SERIALIZATION_TEST(name)                                               \
  namespace {                                                                  \
  template <class Serializer, class Deserializer>                              \
  struct name##_tpl : fixture<Serializer, Deserializer> {                      \
    using super = fixture<Serializer, Deserializer>;                           \
    using super::i32;                                                          \
    using super::i64;                                                          \
    using super::f32;                                                          \
    using super::f64;                                                          \
    using super::dur;                                                          \
    using super::ts;                                                           \
    using super::te;                                                           \
    using super::str;                                                          \
    using super::rs;                                                           \
    using super::ta;                                                           \
    using super::ra;                                                           \
    using super::system;                                                       \
    using super::msg;                                                          \
    using super::recursive;                                                    \
    using super::serialize;                                                    \
    using super::deserialize;                                                  \
    using super::roundtrip;                                                    \
    using super::msg_roundtrip;                                                \
    void run_test_impl();                                                      \
  };                                                                           \
  using name##_binary = name##_tpl<binary_serializer, binary_deserializer>;    \
  using name##_stream                                                          \
    = name##_tpl<stream_serializer<vectorbuf>, stream_deserializer<charbuf>>;  \
  ::caf::test::detail::adder<::caf::test::test_impl<name##_binary>>            \
    CAF_UNIQUE(a_binary){CAF_XSTR(CAF_SUITE), CAF_XSTR(name##_binary), false}; \
  ::caf::test::detail::adder<::caf::test::test_impl<name##_stream>>            \
    CAF_UNIQUE(a_stream){CAF_XSTR(CAF_SUITE), CAF_XSTR(name##_stream), false}; \
  }                                                                            \
  template <class Serializer, class Deserializer>                              \
  void name##_tpl<Serializer, Deserializer>::run_test_impl()

SERIALIZATION_TEST(ieee_754_conversion) {
  // check conversion of float
  float f1 = 3.1415925f;              // float value
  auto p1 = caf::detail::pack754(f1); // packet value
  CAF_CHECK_EQUAL(p1, static_cast<decltype(p1)>(0x40490FDA));
  auto u1 = caf::detail::unpack754(p1); // unpacked value
  CAF_CHECK_EQUAL(f1, u1);
  // check conversion of double
  double f2 = 3.14159265358979311600; // double value
  auto p2 = caf::detail::pack754(f2); // packet value
  CAF_CHECK_EQUAL(p2, static_cast<decltype(p2)>(0x400921FB54442D18));
  auto u2 = caf::detail::unpack754(p2); // unpacked value
  CAF_CHECK_EQUAL(f2, u2);
}

SERIALIZATION_TEST(i32_values) {
  auto buf = serialize(i32);
  int32_t x;
  deserialize(buf, x);
  CAF_CHECK_EQUAL(i32, x);
}

SERIALIZATION_TEST(i64_values) {
  auto buf = serialize(i64);
  int64_t x;
  deserialize(buf, x);
  CAF_CHECK_EQUAL(i64, x);
}

SERIALIZATION_TEST(float_values) {
  auto buf = serialize(f32);
  float x;
  deserialize(buf, x);
  CAF_CHECK_EQUAL(f32, x);
}

SERIALIZATION_TEST(double_values) {
  auto buf = serialize(f64);
  double x;
  deserialize(buf, x);
  CAF_CHECK_EQUAL(f64, x);
}

SERIALIZATION_TEST(duration_values) {
  auto buf = serialize(dur);
  duration x;
  deserialize(buf, x);
  CAF_CHECK_EQUAL(dur, x);
}

SERIALIZATION_TEST(timestamp_values) {
  auto buf = serialize(ts);
  timestamp x;
  deserialize(buf, x);
  CAF_CHECK_EQUAL(ts, x);
}

SERIALIZATION_TEST(enum_classes) {
  auto buf = serialize(te);
  test_enum x;
  deserialize(buf, x);
  CAF_CHECK_EQUAL(te, x);
}

SERIALIZATION_TEST(strings) {
  auto buf = serialize(str);
  string x;
  deserialize(buf, x);
  CAF_CHECK_EQUAL(str, x);
}

SERIALIZATION_TEST(custom_struct) {
  auto buf = serialize(rs);
  raw_struct x;
  deserialize(buf, x);
  CAF_CHECK_EQUAL(rs, x);
}

SERIALIZATION_TEST(atoms) {
  auto foo = atom("foo");
  CAF_CHECK_EQUAL(foo, roundtrip(foo));
  CAF_CHECK_EQUAL(foo, msg_roundtrip(foo));
  CAF_CHECK_EQUAL(atom("bar"), roundtrip(atom("bar")));
  CAF_CHECK_EQUAL(atom("bar"), msg_roundtrip(atom("bar")));
}

SERIALIZATION_TEST(raw_arrays) {
  auto buf = serialize(ra);
  int x[3];
  deserialize(buf, x);
  for (auto i = 0; i < 3; ++i)
    CAF_CHECK_EQUAL(ra[i], x[i]);
}

SERIALIZATION_TEST(arrays) {
  auto buf = serialize(ta);
  test_array x;
  deserialize(buf, x);
  for (auto i = 0; i < 4; ++i)
    CAF_CHECK_EQUAL(ta.value[i], x.value[i]);
  for (auto i = 0; i < 2; ++i)
    for (auto j = 0; j < 4; ++j)
      CAF_CHECK_EQUAL(ta.value2[i][j], x.value2[i][j]);
}

SERIALIZATION_TEST(empty_non_pods) {
  test_empty_non_pod x;
  auto buf = serialize(x);
  CAF_REQUIRE(buf.empty());
  deserialize(buf, x);
}

std::string hexstr(const std::vector<char>& buf) {
  using namespace std;
  ostringstream oss;
  oss << hex;
  oss.fill('0');
  for (auto& c : buf) {
    oss.width(2);
    oss << int{c};
  }
  return oss.str();
}

SERIALIZATION_TEST(messages) {
  // serialize original message which uses tuple_vals internally and
  // deserialize into a message which uses type_erased_value pointers
  message x;
  auto buf1 = serialize(msg);
  deserialize(buf1, x);
  CAF_CHECK_EQUAL(to_string(msg), to_string(x));
  CAF_CHECK(is_message(x).equal(i32, i64, dur, ts, te, str, rs));
  // serialize fully dynamic message again (do another roundtrip)
  message y;
  auto buf2 = serialize(x);
  CAF_CHECK_EQUAL(buf1, buf2);
  deserialize(buf2, y);
  CAF_CHECK_EQUAL(to_string(msg), to_string(y));
  CAF_CHECK(is_message(y).equal(i32, i64, dur, ts, te, str, rs));
  CAF_CHECK_EQUAL(to_string(recursive), to_string(roundtrip(recursive)));
}

SERIALIZATION_TEST(multiple_messages) {
  auto m = make_message(rs, te);
  auto buf = serialize(te, m, msg);
  test_enum t;
  message m1;
  message m2;
  deserialize(buf, t, m1, m2);
  CAF_CHECK_EQUAL(std::make_tuple(t, to_string(m1), to_string(m2)),
                  std::make_tuple(te, to_string(m), to_string(msg)));
  CAF_CHECK(is_message(m1).equal(rs, te));
  CAF_CHECK(is_message(m2).equal(i32, i64, dur, ts, te, str, rs));
}

SERIALIZATION_TEST(type_erased_value) {
  auto buf = serialize(str);
  type_erased_value_ptr ptr{new type_erased_value_impl<std::string>};
  binary_deserializer source{system, buf};
  ptr->load(source);
  CAF_CHECK_EQUAL(str, *reinterpret_cast<const std::string*>(ptr->get()));
}

SERIALIZATION_TEST(type_erased_view) {
  auto str_view = make_type_erased_view(str);
  auto buf = serialize(str_view);
  std::string res;
  deserialize(buf, res);
  CAF_CHECK_EQUAL(str, res);
}

SERIALIZATION_TEST(type_erased_tuple) {
  auto tview = make_type_erased_tuple_view(str, i32);
  CAF_CHECK_EQUAL(to_string(tview), deep_to_string(std::make_tuple(str, i32)));
  auto buf = serialize(tview);
  CAF_REQUIRE(!buf.empty());
  std::string tmp1;
  int32_t tmp2;
  deserialize(buf, tmp1, tmp2);
  CAF_CHECK_EQUAL(tmp1, str);
  CAF_CHECK_EQUAL(tmp2, i32);
  deserialize(buf, tview);
  CAF_CHECK_EQUAL(to_string(tview), deep_to_string(std::make_tuple(str, i32)));
}

SERIALIZATION_TEST(streambuf_serialization) {
  auto data = std::string{"The quick brown fox jumps over the lazy dog"};
  std::vector<char> buf;
  // First, we check the standard use case in CAF where stream serializers own
  // their stream buffers.
  stream_serializer<vectorbuf> bs{vectorbuf{buf}};
  auto e = bs(data);
  CAF_REQUIRE_EQUAL(e, none);
  stream_deserializer<charbuf> bd{charbuf{buf}};
  std::string target;
  e = bd(target);
  CAF_REQUIRE_EQUAL(e, none);
  CAF_CHECK_EQUAL(data, target);
  // Second, we test another use case where the serializers only keep
  // references of the stream buffers.
  buf.clear();
  target.clear();
  vectorbuf vb{buf};
  stream_serializer<vectorbuf&> vs{vb};
  e = vs(data);
  CAF_REQUIRE_EQUAL(e, none);
  charbuf cb{buf};
  stream_deserializer<charbuf&> vd{cb};
  e = vd(target);
  CAF_REQUIRE_EQUAL(e, none);
  CAF_CHECK(data == target);
}

SERIALIZATION_TEST(byte_sequence_optimization) {
  std::vector<uint8_t> data(42);
  std::fill(data.begin(), data.end(), 0x2a);
  std::vector<uint8_t> buf;
  using streambuf_type = containerbuf<std::vector<uint8_t>>;
  streambuf_type cb{buf};
  stream_serializer<streambuf_type&> bs{cb};
  auto e = bs(data);
  CAF_REQUIRE(!e);
  data.clear();
  streambuf_type cb2{buf};
  stream_deserializer<streambuf_type&> bd{cb2};
  e = bd(data);
  CAF_REQUIRE(!e);
  CAF_CHECK_EQUAL(data.size(), 42u);
  CAF_CHECK(
    std::all_of(data.begin(), data.end(), [](uint8_t c) { return c == 0x2a; }));
}

SERIALIZATION_TEST(long_sequences) {
  std::vector<char> data;
  binary_serializer sink{nullptr, data};
  size_t n = std::numeric_limits<uint32_t>::max();
  sink.begin_sequence(n);
  sink.end_sequence();
  binary_deserializer source{nullptr, data};
  size_t m = 0;
  source.begin_sequence(m);
  source.end_sequence();
  CAF_CHECK_EQUAL(n, m);
}

SERIALIZATION_TEST(non_empty_vector) {
  CAF_MESSAGE("deserializing into a non-empty vector overrides any content");
  std::vector<int> foo{1, 2, 3};
  std::vector<int> bar{0};
  auto buf = serialize(foo);
  deserialize(buf, bar);
  CAF_CHECK_EQUAL(foo, bar);
}

SERIALIZATION_TEST(variant_with_tree_types) {
  CAF_MESSAGE("deserializing into a non-empty vector overrides any content");
  using test_variant = variant<int, double, std::string>;
  test_variant x{42};
  CAF_CHECK_EQUAL(x, roundtrip(x));
  x = 12.34;
  CAF_CHECK_EQUAL(x, roundtrip(x));
  x = std::string{"foobar"};
  CAF_CHECK_EQUAL(x, roundtrip(x));
}

// -- our vector<bool> serialization packs into an uint64_t. Hence, the
// critical sizes to test are 0, 1, 63, 64, and 65.

SERIALIZATION_TEST(bool_vector_size_0) {
  std::vector<bool> xs;
  CAF_CHECK_EQUAL(deep_to_string(xs), "[]");
  CAF_CHECK_EQUAL(xs, roundtrip(xs));
  CAF_CHECK_EQUAL(xs, msg_roundtrip(xs));
}

SERIALIZATION_TEST(bool_vector_size_1) {
  std::vector<bool> xs{true};
  CAF_CHECK_EQUAL(deep_to_string(xs), "[true]");
  CAF_CHECK_EQUAL(xs, roundtrip(xs));
  CAF_CHECK_EQUAL(xs, msg_roundtrip(xs));
}

SERIALIZATION_TEST(bool_vector_size_63) {
  std::vector<bool> xs;
  for (int i = 0; i < 63; ++i)
    xs.push_back(i % 3 == 0);
  CAF_CHECK_EQUAL(
    deep_to_string(xs),
    "[true, false, false, true, false, false, true, false, false, true, false, "
    "false, true, false, false, true, false, false, true, false, false, true, "
    "false, false, true, false, false, true, false, false, true, false, false, "
    "true, false, false, true, false, false, true, false, false, true, false, "
    "false, true, false, false, true, false, false, true, false, false, true, "
    "false, false, true, false, false, true, false, false]");
  CAF_CHECK_EQUAL(xs, roundtrip(xs));
  CAF_CHECK_EQUAL(xs, msg_roundtrip(xs));
}

SERIALIZATION_TEST(bool_vector_size_64) {
  std::vector<bool> xs;
  for (int i = 0; i < 64; ++i)
    xs.push_back(i % 5 == 0);
  CAF_CHECK_EQUAL(deep_to_string(xs),
                  "[true, false, false, false, false, true, false, false, "
                  "false, false, true, false, false, false, false, true, "
                  "false, false, false, false, true, false, false, false, "
                  "false, true, false, false, false, false, true, false, "
                  "false, false, false, true, false, false, false, false, "
                  "true, false, false, false, false, true, false, false, "
                  "false, false, true, false, false, false, false, true, "
                  "false, false, false, false, true, false, false, false]");
  CAF_CHECK_EQUAL(xs, roundtrip(xs));
  CAF_CHECK_EQUAL(xs, msg_roundtrip(xs));
}

SERIALIZATION_TEST(bool_vector_size_65) {
  std::vector<bool> xs;
  for (int i = 0; i < 65; ++i)
    xs.push_back(!(i % 7 == 0));
  CAF_CHECK_EQUAL(
    deep_to_string(xs),
    "[false, true, true, true, true, true, true, false, true, true, true, "
    "true, true, true, false, true, true, true, true, true, true, false, true, "
    "true, true, true, true, true, false, true, true, true, true, true, true, "
    "false, true, true, true, true, true, true, false, true, true, true, true, "
    "true, true, false, true, true, true, true, true, true, false, true, true, "
    "true, true, true, true, false, true]");
  CAF_CHECK_EQUAL(xs, roundtrip(xs));
  CAF_CHECK_EQUAL(xs, msg_roundtrip(xs));
}
