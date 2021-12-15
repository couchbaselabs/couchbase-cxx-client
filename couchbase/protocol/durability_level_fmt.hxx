/* -*- Mode: C++; tab-width: 4; c-basic-offset: 4; indent-tabs-mode: nil -*- */
/*
 *   Copyright 2020-2021 Couchbase, Inc.
 *
 *   Licensed under the Apache License, Version 2.0 (the "License");
 *   you may not use this file except in compliance with the License.
 *   You may obtain a copy of the License at
 *
 *       http://www.apache.org/licenses/LICENSE-2.0
 *
 *   Unless required by applicable law or agreed to in writing, software
 *   distributed under the License is distributed on an "AS IS" BASIS,
 *   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *   See the License for the specific language governing permissions and
 *   limitations under the License.
 */

#pragma once

#include <couchbase/protocol/durability_level.hxx>

#include <spdlog/fmt/bundled/core.h>

template<>
struct fmt::formatter<couchbase::protocol::durability_level> {
    template<typename ParseContext>
    constexpr auto parse(ParseContext& ctx)
    {
        return ctx.begin();
    }

    template<typename FormatContext>
    auto format(couchbase::protocol::durability_level value, FormatContext& ctx) const
    {
        string_view name = "unknown";
        switch (value) {
            case couchbase::protocol::durability_level::none:
                name = "none";
                break;
            case couchbase::protocol::durability_level::majority:
                name = "majority";
                break;
            case couchbase::protocol::durability_level::majority_and_persist_to_active:
                name = "majority_and_persist_to_active";
                break;
            case couchbase::protocol::durability_level::persist_to_majority:
                name = "persist_to_majority";
                break;
        }
        return format_to(ctx.out(), "{}", name);
    }
};
