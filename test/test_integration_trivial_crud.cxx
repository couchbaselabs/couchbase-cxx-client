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

#include "test_helper_integration.hxx"

static const tao::json::value basic_doc = {
    { "a", 1.0 },
    { "b", 2.0 },
};
static const std::string basic_doc_json = couchbase::utils::json::generate(basic_doc);

TEST_CASE("integration: upsert document into default collection", "[integration]")
{
    test::utils::integration_test_guard integration;

    test::utils::open_bucket(integration.cluster, integration.ctx.bucket);

    {
        couchbase::document_id id{ integration.ctx.bucket, "_default", "_default", test::utils::uniq_id("foo") };
        couchbase::operations::upsert_request req{ id, basic_doc_json };
        auto resp = test::utils::execute(integration.cluster, req);
        INFO(resp.ctx.ec.message())
        REQUIRE_FALSE(resp.ctx.ec);
        REQUIRE(!resp.cas.empty());
        INFO("seqno=" << resp.token.sequence_number)
        REQUIRE(resp.token.sequence_number != 0);
    }
}

TEST_CASE("integration: get", "[integration]")
{
    test::utils::integration_test_guard integration;
    test::utils::open_bucket(integration.cluster, integration.ctx.bucket);

    couchbase::document_id id{ integration.ctx.bucket, "_default", "_default", test::utils::uniq_id("get") };

    SECTION("miss")
    {
        couchbase::operations::get_request req{ id };
        auto resp = test::utils::execute(integration.cluster, req);
        INFO(resp.ctx.ec.message());
        REQUIRE(resp.ctx.ec == couchbase::error::key_value_errc::document_not_found);
    }

    SECTION("hit")
    {
        auto flags = 0xdeadbeef;
        {
            couchbase::operations::insert_request req{ id, basic_doc_json };
            req.flags = flags;
            auto resp = test::utils::execute(integration.cluster, req);
            REQUIRE_FALSE(resp.ctx.ec);
        }
        {
            couchbase::operations::get_request req{ id };
            auto resp = test::utils::execute(integration.cluster, req);
            REQUIRE_FALSE(resp.ctx.ec);
            REQUIRE(resp.value == basic_doc_json);
            REQUIRE(resp.flags == flags);
        }
    }
}

TEST_CASE("integration: touch", "[integration]")
{
    test::utils::integration_test_guard integration;
    test::utils::open_bucket(integration.cluster, integration.ctx.bucket);

    couchbase::document_id id{ integration.ctx.bucket, "_default", "_default", test::utils::uniq_id("touch") };

    SECTION("miss")
    {
        couchbase::operations::touch_request req{ id };
        req.expiry = 666;
        auto resp = test::utils::execute(integration.cluster, req);
        REQUIRE(resp.ctx.ec == couchbase::error::key_value_errc::document_not_found);
    }

    SECTION("hit")
    {
        {
            couchbase::operations::insert_request req{ id, basic_doc_json };
            auto resp = test::utils::execute(integration.cluster, req);
            REQUIRE_FALSE(resp.ctx.ec);
        }
        {
            couchbase::operations::touch_request req{ id };
            req.expiry = 666;
            auto resp = test::utils::execute(integration.cluster, req);
            REQUIRE_FALSE(resp.ctx.ec);
        }
    }
}

TEST_CASE("integration: pessimistic locking", "[integration]")
{
    test::utils::integration_test_guard integration;
    test::utils::open_bucket(integration.cluster, integration.ctx.bucket);

    couchbase::document_id id{ integration.ctx.bucket, "_default", "_default", test::utils::uniq_id("locking") };
    uint32_t lock_time = 10;

    couchbase::protocol::cas cas;

    {
        couchbase::operations::insert_request req{ id, basic_doc_json };
        auto resp = test::utils::execute(integration.cluster, req);
        REQUIRE_FALSE(resp.ctx.ec);
        cas = resp.cas;
    }

    // lock and record CAS of the locked document
    {
        couchbase::operations::get_and_lock_request req{ id };
        req.lock_time = lock_time;
        auto resp = test::utils::execute(integration.cluster, req);
        REQUIRE_FALSE(resp.ctx.ec);
        REQUIRE(cas != resp.cas);
        cas = resp.cas;
    }

    // real CAS is masked now and not visible by regular GET
    {
        couchbase::operations::get_request req{ id };
        auto resp = test::utils::execute(integration.cluster, req);
        REQUIRE_FALSE(resp.ctx.ec);
        REQUIRE(cas != resp.cas);
    }

    // it is not allowed to lock the same key twice
    {
        couchbase::operations::get_and_lock_request req{ id };
        req.lock_time = lock_time;
        auto resp = test::utils::execute(integration.cluster, req);
        REQUIRE(resp.ctx.ec == couchbase::error::common_errc::ambiguous_timeout);
        REQUIRE(resp.ctx.retry_reasons.count(couchbase::io::retry_reason::kv_locked) == 1);
    }

    // but mutating the locked key is allowed with known cas
    {
        couchbase::operations::replace_request req{ id, basic_doc_json };
        req.cas = cas;
        auto resp = test::utils::execute(integration.cluster, req);
        REQUIRE_FALSE(resp.ctx.ec);
    }

    {
        couchbase::operations::get_and_lock_request req{ id };
        req.lock_time = lock_time;
        auto resp = test::utils::execute(integration.cluster, req);
        REQUIRE_FALSE(resp.ctx.ec);
        cas = resp.cas;
    }

    // to unlock key without mutation, unlock might be used
    {
        couchbase::operations::unlock_request req{ id };
        req.cas = cas;
        auto resp = test::utils::execute(integration.cluster, req);
        REQUIRE_FALSE(resp.ctx.ec);
    }

    // now the key is not locked
    {
        couchbase::operations::upsert_request req{ id, basic_doc_json };
        auto resp = test::utils::execute(integration.cluster, req);
        REQUIRE_FALSE(resp.ctx.ec);
    }
}

TEST_CASE("integration: touch with zero expiry resets expiry", "[integration]")
{
    test::utils::integration_test_guard integration;
    test::utils::open_bucket(integration.cluster, integration.ctx.bucket);

    couchbase::document_id id{ integration.ctx.bucket, "_default", "_default", test::utils::uniq_id("get_reset_expiry_key") };

    {
        couchbase::operations::insert_request req{ id, basic_doc_json };
        auto resp = test::utils::execute(integration.cluster, req);
        REQUIRE_FALSE(resp.ctx.ec);
    }

    // set expiry with touch
    {
        couchbase::operations::touch_request req{ id };
        req.expiry = 1;
        auto resp = test::utils::execute(integration.cluster, req);
        REQUIRE_FALSE(resp.ctx.ec);
    }

    // reset expiry
    {
        couchbase::operations::get_and_touch_request req{ id };
        req.expiry = 0;
        auto resp = test::utils::execute(integration.cluster, req);
        REQUIRE_FALSE(resp.ctx.ec);
    }

    // wait for original expiry to pass
    std::this_thread::sleep_for(std::chrono::seconds(2));

    // check that the key still exists
    {
        couchbase::operations::get_request req{ id };
        auto resp = test::utils::execute(integration.cluster, req);
        REQUIRE_FALSE(resp.ctx.ec);
        REQUIRE(resp.value == basic_doc_json);
    }
}

TEST_CASE("integration: exists", "[integration]")
{
    test::utils::integration_test_guard integration;
    test::utils::open_bucket(integration.cluster, integration.ctx.bucket);

    couchbase::document_id id{ integration.ctx.bucket, "_default", "_default", test::utils::uniq_id("exists") };

    {
        couchbase::operations::insert_request req{ id, basic_doc_json };
        auto resp = test::utils::execute(integration.cluster, req);
        REQUIRE_FALSE(resp.ctx.ec);
    }

    {
        couchbase::operations::exists_request req{ id };
        auto resp = test::utils::execute(integration.cluster, req);
        REQUIRE_FALSE(resp.ctx.ec);
        REQUIRE(resp.exists());
        REQUIRE((resp.status == couchbase::operations::exists_response::observe_status::found ||
                 resp.status == couchbase::operations::exists_response::observe_status::persisted));
    }
}