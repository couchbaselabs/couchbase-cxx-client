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

#pragma once

#include <couchbase/api/get_all_replicas.hxx>
#include <couchbase/api/get_any_replica.hxx>

#include <memory>

namespace couchbase
{
class cluster;
} // namespace couchbase

namespace couchbase::api
{
class bucket;
class scope;

class collection
{
  public:
    /**
     * Constant for the name of the default collection in the bucket.
     */
    static constexpr auto default_name{ "_default" };

    /**
     * Returns name of the bucket where the collection is defined.
     *
     * @return name of the bucket
     *
     * @since 1.0.0
     * @committed
     */
    [[nodiscard]] const std::string& bucket_name() const
    {
        return bucket_name_;
    }

    /**
     * Returns name of the scope where the collection is defined.
     *
     * @return name of the scope
     *
     * @since 1.0.0
     * @committed
     */
    [[nodiscard]] const std::string& scope_name() const
    {
        return scope_name_;
    }

    /**
     * Returns name of the collection.
     *
     * @return name of the collection
     *
     * @since 1.0.0
     * @committed
     */
    [[nodiscard]] const std::string& name() const
    {
        return name_;
    }

    template<typename Handler>
    void get_any_replica(std::string document_key, const get_any_replica_options& options, Handler&& handler) const
    {
        return impl::initiate_get_any_replica_operation(
          core_, bucket_name_, scope_name_, name_, std::move(document_key), options, std::forward<Handler>(handler));
    }

    [[nodiscard]] auto get_any_replica(std::string document_key, const get_any_replica_options& options) const
      -> std::future<std::pair<get_any_replica_error_context, get_any_replica_result>>
    {
        auto barrier = std::make_shared<std::promise<std::pair<get_any_replica_error_context, get_any_replica_result>>>();
        auto future = barrier->get_future();
        impl::initiate_get_any_replica_operation(
          core_, bucket_name_, scope_name_, name_, std::move(document_key), options, [barrier](auto ctx, auto result) {
              barrier->set_value({ std::move(ctx), std::move(result) });
          });
        return future;
    }

    template<typename Handler>
    void get_all_replicas(std::string document_key, const get_all_replicas_options& options, Handler&& handler) const
    {
        return impl::initiate_get_all_replicas_operation(
          core_, bucket_name_, scope_name_, name_, std::move(document_key), options, std::forward<Handler>(handler));
    }

    [[nodiscard]] auto get_all_replicas(std::string document_key, const get_all_replicas_options& options) const
      -> std::future<std::pair<get_all_replicas_error_context, get_all_replicas_result>>
    {
        auto barrier = std::make_shared<std::promise<std::pair<get_all_replicas_error_context, get_all_replicas_result>>>();
        auto future = barrier->get_future();
        impl::initiate_get_all_replicas_operation(
          core_, bucket_name_, scope_name_, name_, std::move(document_key), options, [barrier](auto ctx, auto result) {
              barrier->set_value({ std::move(ctx), std::move(result) });
          });
        return future;
    }

  private:
    friend class bucket;
    friend class scope;

    /**
     * @param core
     * @param bucket_name
     * @param scope_name
     * @param name
     *
     * @since 1.0.0
     * @internal
     */
    collection(std::shared_ptr<couchbase::cluster> core, std::string_view bucket_name, std::string_view scope_name, std::string_view name)
      : core_(std::move(core))
      , bucket_name_(bucket_name)
      , scope_name_(scope_name)
      , name_(name)
    {
    }

    std::shared_ptr<couchbase::cluster> core_;
    std::string bucket_name_;
    std::string scope_name_;
    std::string name_;
};
} // namespace couchbase::api
