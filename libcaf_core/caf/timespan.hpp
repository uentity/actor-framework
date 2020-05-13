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

#pragma once

#include <chrono>
#include <cstdint>

namespace caf {

/// A portable timespan type with nanosecond resolution.
using timespan = std::chrono::duration<int64_t, std::nano>;

/// Represents an infinite amount of timeout for specifying "invalid" timeouts.
struct infinite_t {
  constexpr infinite_t() {
    // nop
  }
};

/// Constant for passing "no timeout" to functions such as `request`.
static constexpr infinite_t infinite = infinite_t{};

// -- forward compatibility with CAF 0.18 --------------------------------------

constexpr bool is_infinite(infinite_t) {
  return true;
}

template <class Rep, class Period>
constexpr bool is_infinite(std::chrono::duration<Rep, Period>) {
  return false;
}

} // namespace caf
