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

#include <couchbase/api/document_id.hxx>

#include <chrono>
#include <memory>
#include <optional>

namespace couchbase
{
namespace impl
{
/**
 * Opaque structure representing get_all_replicas request.
 *
 * @see make_get_all_replicas_request()
 *
 * @since 1.0.0
 * @committed
 */
class get_all_replicas_request;
} // namespace impl

namespace api
{
/**
 * Options for make_get_all_replicas_request().
 *
 * @since 1.0.0
 * @committed
 */
struct get_all_replicas_options {
    /**
     * The time allowed for the operation to be terminated.
     *
     * @since 1.0.0
     * @committed
     */
    std::optional<std::chrono::milliseconds> timeout{};
};

/**
 * Creates operation to retrieve a document for a given id, leveraging both the active and all available replicas.
 *
 * @param id the document identifier
 * @param options operation customization
 * @return couchbase::operation::get_all_replicas_request
 *
 * @see make_get_any_replica_request
 *
 * @since 1.0.0
 * @committed
 */
[[nodiscard]] std::shared_ptr<couchbase::impl::get_all_replicas_request>
make_get_all_replicas_request(document_id id, const get_all_replicas_options& options);
} // namespace api
} // namespace couchbase
