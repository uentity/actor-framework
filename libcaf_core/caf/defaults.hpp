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
#include <cstddef>
#include <algorithm>
#include <limits>

#include "caf/atom.hpp"
#include "caf/timestamp.hpp"

// -- hard-coded default values for various CAF options ------------------------

namespace caf {
namespace defaults {

namespace {

using us_t = std::chrono::microseconds;

constexpr caf::timespan us(us_t::rep x) {
  return std::chrono::duration_cast<caf::timespan>(us_t{x});
}

using ms_t = std::chrono::milliseconds;

constexpr caf::timespan ms(ms_t::rep x) {
  return std::chrono::duration_cast<caf::timespan>(ms_t{x});
}

} // namespace <anonymous>

namespace stream {

constexpr timespan desired_batch_complexity = us(50);
constexpr timespan max_batch_delay = ms(5);
constexpr timespan credit_round_interval = ms(10);

} // namespace streaming

namespace scheduler {

constexpr atom_value policy = atom("stealing");
constexpr char* profiling_output_file = "";
size_t max_threads();
constexpr size_t max_throughput = std::numeric_limits<size_t>::max();
constexpr timespan profiling_resolution = ms(100);

} // namespace scheduler

namespace work_stealing {

constexpr size_t aggressive_poll_attempts = 100;
constexpr size_t aggressive_steal_interval = 10;
constexpr size_t moderate_poll_attempts = 500;
constexpr size_t moderate_steal_interval = 5;
constexpr timespan moderate_sleep_duration = us(50);
constexpr size_t relaxed_steal_interval = 1;
constexpr timespan relaxed_sleep_duration = ms(10);

} // namespace work_stealing

namespace logger {

constexpr atom_value console = atom("none");
constexpr atom_value verbosity = atom("trace");
constexpr char* component_filter = "";
constexpr char* console_format = "%m";
constexpr char* file_format = "%r %c %p %a %t %C %M %F:%L %m%n";
constexpr char* file_name = "actor_log_[PID]_[TIMESTAMP]_[NODE].log";

} // namespace logger

namespace middleman {

constexpr char* app_identifier = "";
constexpr atom_value network_backend = atom("default");
constexpr size_t max_consecutive_reads = 50;
constexpr size_t heartbeat_interval = 0;
constexpr size_t cached_udp_buffers = 10;
constexpr size_t max_pending_msgs = 10;

} // namespace middleman

} // namespace defaults
} // namespace caf
