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

#include "document_upsert.hxx"
#include "core/utils/mutation_token.hxx"

#include <couchbase/codec/codec_flags.hxx>
#include <couchbase/error_codes.hxx>

namespace couchbase::core::operations
{
std::error_code
upsert_request::encode_to(upsert_request::encoded_request_type& encoded,
                          mcbp_context&& /* context */) const
{
  encoded.opaque(opaque);
  encoded.partition(partition);
  encoded.body().id(id);
  encoded.body().expiry(expiry);
  encoded.body().flags(flags);
  encoded.body().content(value);
  if (preserve_expiry) {
    encoded.body().preserve_expiry();
  }
  if (codec::codec_flags::has_common_flags(flags, codec::codec_flags::common_flags::json)) {
    encoded.datatype(protocol::datatype::json);
  }
  return {};
}

upsert_response
upsert_request::make_response(key_value_error_context&& ctx,
                              const encoded_response_type& encoded) const
{
  upsert_response response{ std::move(ctx) };
  if (!response.ctx.ec()) {
    response.cas = encoded.cas();
    response.token = couchbase::utils::build_mutation_token(
      encoded.body().token(), partition, response.ctx.bucket());
  }
  return response;
}
} // namespace couchbase::core::operations
