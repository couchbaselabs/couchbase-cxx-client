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

#include "test_helper_integration.hxx"
#include "utils/move_only_context.hxx"

#include <couchbase/api/get_any_replica.hxx>

static const tao::json::value basic_doc = {
    { "a", 1.0 },
    { "b", 2.0 },
};
static const std::vector<std::byte> basic_doc_json = couchbase::utils::json::generate_binary(basic_doc);

TEST_CASE("integration: get any replica", "[integration]")
{
    test::utils::integration_test_guard integration;

    if (integration.number_of_nodes() <= integration.number_of_replicas()) {
        return;
    }

    test::utils::open_bucket(integration.cluster, integration.ctx.bucket);

    std::string scope{ "_default" };
    std::string collection{ "_default" };
    std::string key = test::utils::uniq_id("get_any_replica");

    {
        couchbase::document_id id{ integration.ctx.bucket, scope, collection, key };

        couchbase::operations::insert_request req{ id, basic_doc_json };
        auto resp = test::utils::execute(integration.cluster, req);
        REQUIRE_FALSE(resp.ctx.ec);
    }

    {
        couchbase::api::document_id id{ integration.ctx.bucket, scope, collection, key };
        auto req = couchbase::api::make_get_any_replica_request(id, {});
        auto [ctx, resp] = test::utils::execute(integration.cluster, req);
        REQUIRE_FALSE(ctx.ec);
        REQUIRE(resp.content() == basic_doc_json);
    }
}

TEST_CASE("integration: get all replicas", "[integration]")
{
    test::utils::integration_test_guard integration;

    auto number_of_replicas = integration.number_of_replicas();
    if (integration.number_of_nodes() <= number_of_replicas) {
        return;
    }

    test::utils::open_bucket(integration.cluster, integration.ctx.bucket);

    std::string scope{ "_default" };
    std::string collection{ "_default" };
    std::string key = test::utils::uniq_id("get_any_replica");

    {
        couchbase::document_id id{ integration.ctx.bucket, scope, collection, key };

        couchbase::operations::insert_request req{ id, basic_doc_json };
        auto resp = test::utils::execute(integration.cluster, req);
        REQUIRE_FALSE(resp.ctx.ec);
    }

    {
        couchbase::api::document_id id{ integration.ctx.bucket, scope, collection, key };
        auto req = couchbase::api::make_get_all_replicas_request(id, {});
        auto [ctx, resp] = test::utils::execute(integration.cluster, req);
        REQUIRE_FALSE(ctx.ec);
        REQUIRE(resp.size() == number_of_replicas + 1);
        auto responses_from_active = std::count_if(resp.begin(), resp.end(), [](const auto& r) { return !r.is_replica(); });
        REQUIRE(responses_from_active == 1);
    }
}
