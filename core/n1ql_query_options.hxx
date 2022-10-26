/* -*- Mode: C++; tab-width: 4; c-basic-offset: 4; indent-tabs-mode: nil -*- */
/*
 * Copyright 2022-Present Couchbase, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file
 * except in compliance with the License. You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software distributed under
 * the License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF
 * ANY KIND, either express or implied. See the License for the specific language governing
 * permissions and limitations under the License.
 */

#pragma once

#include "utils/movable_function.hxx"

#include <chrono>
#include <cinttypes>
#include <memory>
#include <string>
#include <system_error>
#include <vector>

namespace couchbase
{
class retry_strategy;
namespace tracing
{
class request_span;
} // namespace tracing
} // namespace couchbase

namespace couchbase::core
{
class n1ql_query_options
{
  public:
    std::vector<std::byte> payload{};
    std::shared_ptr<couchbase::retry_strategy> retry_strategy{};
    std::chrono::milliseconds timeout{};
    std::shared_ptr<couchbase::tracing::request_span> parent_span{};

    struct {
        std::string user{};
    } internal{};
    std::string endpoint_{};
};

class n1ql_query_row_reader_impl;

class n1ql_query_row_reader
{
  public:
    n1ql_query_row_reader();

    auto next_row() -> std::vector<std::byte>;

    auto error() -> std::error_code;

    auto meta_data() -> std::pair<std::vector<std::byte>, std::error_code>;

    auto close() -> std::error_code;

    auto prepared_name() -> std::pair<std::string, std::error_code>;

    auto endpoint() -> std::string;

  private:
    std::shared_ptr<n1ql_query_row_reader_impl> impl_;
};

using n1ql_query_callback = utils::movable_function<void(n1ql_query_row_reader reader, std::error_code ec)>;

} // namespace couchbase::core
