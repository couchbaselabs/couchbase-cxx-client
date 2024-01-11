/* -*- Mode: C++; tab-width: 4; c-basic-offset: 4; indent-tabs-mode: nil -*- */
/*
 *   Copyright 2020-Present Couchbase, Inc.
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

#include "core/cluster.hxx"
#include "core/operations/document_search.hxx"
#include "core/utils/json.hxx"
#include "encoded_search_facet.hxx"
#include "encoded_search_query.hxx"
#include "encoded_search_sort.hxx"
#include "internal_date_range_facet_result.hxx"
#include "internal_numeric_range_facet_result.hxx"
#include "internal_term_facet_result.hxx"

#include <couchbase/cluster.hxx>
#include <couchbase/match_none_query.hxx>
#include <utility>

#include <fmt/core.h>

namespace couchbase::core::impl
{
namespace
{
std::optional<core::search_highlight_style>
map_highlight_style(const std::optional<couchbase::highlight_style>& style)
{
    if (style) {
        switch (style.value()) {
            case highlight_style::html:
                return core::search_highlight_style::html;
            case highlight_style::ansi:
                return core::search_highlight_style::ansi;
        }
    }
    return {};
}

static std::optional<core::search_scan_consistency>
map_scan_consistency(const std::optional<couchbase::search_scan_consistency>& scan_consistency)
{
    if (scan_consistency == couchbase::search_scan_consistency::not_bounded) {
        return core::search_scan_consistency::not_bounded;
    }
    return {};
}

static std::vector<std::string>
map_sort(const std::vector<std::shared_ptr<search_sort>>& sort, const std::vector<std::string>& sort_string)
{
    std::vector<std::string> sort_specs{};
    sort_specs.reserve(sort.size() + sort_string.size());

    for (const auto& s : sort) {
        auto encoded = s->encode();
        if (encoded.ec) {
            throw std::system_error(encoded.ec, "unable to encode search sort object");
        }
        sort_specs.emplace_back(core::utils::json::generate(encoded.sort));
    }

    for (const auto& s : sort_string) {
        sort_specs.emplace_back(core::utils::json::generate(s));
    }

    return sort_specs;
}

static std::map<std::string, std::string>
map_facets(const std::map<std::string, std::shared_ptr<search_facet>, std::less<>>& facets)
{
    std::map<std::string, std::string> core_facets{};

    for (const auto& [name, f] : facets) {
        auto encoded = f->encode();
        if (encoded.ec) {
            throw std::system_error(encoded.ec, "unable to encode search facet object in request");
        }
        core_facets[name] = core::utils::json::generate(encoded.facet);
    }

    return core_facets;
}

static std::map<std::string, couchbase::core::json_string>
map_raw(std::map<std::string, codec::binary, std::less<>>& raw)
{
    std::map<std::string, couchbase::core::json_string> core_raw{};
    for (const auto& [name, value] : raw) {
        core_raw[name] = value;
    }
    return core_raw;
}

static std::optional<core::vector_query_combination>
map_vector_query_combination(const std::optional<couchbase::vector_query_combination>& combination)
{
    if (combination) {
        switch (combination.value()) {
            case couchbase::vector_query_combination::combination_and:
                return core::vector_query_combination::combination_and;
            case couchbase::vector_query_combination::combination_or:
                return core::vector_query_combination::combination_or;
        }
    }
    return {};
}

} // namespace

core::operations::search_request
build_search_request(std::string index_name,
                     const search_query& query,
                     search_options::built options,
                     std::optional<std::string> /* bucket_name */,
                     std::optional<std::string> /* scope_name */)
{
    auto encoded = query.encode();
    if (encoded.ec) {
        throw std::system_error(encoded.ec, fmt::format("unable to encode search query for index \"{}\"", index_name));
    }
    core::operations::search_request request{
        std::move(index_name),
        core::utils::json::generate_binary(encoded.query),
        {},
        {},
        {},
        options.limit,
        options.skip,
        options.explain,
        options.disable_scoring,
        options.include_locations,
        map_highlight_style(options.highlight_style),
        options.highlight_fields,
        options.fields,
        options.collections,
        map_scan_consistency(options.scan_consistency),
        options.mutation_state,
        map_sort(options.sort, options.sort_string),
        map_facets(options.facets),
        map_raw(options.raw),
        {},
        options.client_context_id,
        options.timeout,
    };
    return request;
}

core::operations::search_request
build_search_request(std::string index_name,
                     couchbase::search_request request,
                     search_options::built options,
                     std::optional<std::string> /* bucket_name */,
                     std::optional<std::string> /* scope_name */)
{
    if (!request.search_query().has_value()) {
        request.search_query(couchbase::match_none_query{});
    }

    core::operations::search_request core_request{
        std::move(index_name),
        core::utils::json::generate_binary(request.search_query().value().query),
        true,
        {},
        {},
        options.limit,
        options.skip,
        options.explain,
        options.disable_scoring,
        options.include_locations,
        map_highlight_style(options.highlight_style),
        options.highlight_fields,
        options.fields,
        options.collections,
        map_scan_consistency(options.scan_consistency),
        options.mutation_state,
        map_sort(options.sort, options.sort_string),
        map_facets(options.facets),
        map_raw(options.raw),
        {},
        options.client_context_id,
        options.timeout,
    };

    if (!request.vector_search().has_value()) {
        return core_request;
    }
    auto encoded_vector_search = request.vector_search().value().encode();
    if (encoded_vector_search.ec) {
        throw std::system_error(encoded_vector_search.ec, fmt::format("unable to encode vector_search for index \"{}\"", index_name));
    }
    core_request.vector_query = core::utils::json::generate_binary(encoded_vector_search.query);

    auto vector_search_opts = request.vector_search().value().options();
    core_request.vector_query_combination = map_vector_query_combination(vector_search_opts.query_combination());
    return core_request;
}
} // namespace couchbase::core::impl
