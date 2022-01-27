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

#include <couchbase/errors.hxx>
#include <couchbase/logger/logger.hxx>
#include <couchbase/operations/document_query.hxx>
#include <couchbase/operations/management/error_utils.hxx>
#include <couchbase/utils/duration_parser.hxx>
#include <couchbase/utils/json.hxx>

#include <gsl/assert>

namespace couchbase::operations
{
std::error_code
query_request::encode_to(query_request::encoded_request_type& encoded, http_context& context)
{
    ctx_.emplace(context);
    tao::json::value body{
        { "client_context_id", encoded.client_context_id },
    };
    if (adhoc) {
        body["statement"] = statement;
    } else {
        auto entry = ctx_->cache.get(statement);
        if (entry) {
            body["prepared"] = entry->name;
            if (entry->plan) {
                body["encoded_plan"] = entry->plan.value();
            }
        } else {
            body["statement"] = "PREPARE " + statement;
            if (context.config.supports_enhanced_prepared_statements()) {
                body["auto_execute"] = true;
            } else {
                extract_encoded_plan_ = true;
            }
        }
    }
    body["timeout"] =
      fmt::format("{}ms", ((timeout > std::chrono::milliseconds(5'000)) ? (timeout - std::chrono::milliseconds(500)) : timeout).count());
    if (positional_parameters.empty()) {
        for (const auto& [name, value] : named_parameters) {
            Expects(name.empty() == false);
            std::string key = name;
            if (key[0] != '$') {
                key.insert(key.begin(), '$');
            }
            body[key] = utils::json::parse(value);
        }
    } else {
        std::vector<tao::json::value> parameters;
        for (const auto& value : positional_parameters) {
            parameters.emplace_back(utils::json::parse(value));
        }
        body["args"] = std::move(parameters);
    }
    switch (profile) {
        case profile_mode::phases:
            body["profile"] = "phases";
            break;
        case profile_mode::timings:
            body["profile"] = "timings";
            break;
        case profile_mode::off:
            break;
    }
    if (max_parallelism) {
        body["max_parallelism"] = std::to_string(max_parallelism.value());
    }
    if (pipeline_cap) {
        body["pipeline_cap"] = std::to_string(pipeline_cap.value());
    }
    if (pipeline_batch) {
        body["pipeline_batch"] = std::to_string(pipeline_batch.value());
    }
    if (scan_cap) {
        body["scan_cap"] = std::to_string(scan_cap.value());
    }
    if (!metrics) {
        body["metrics"] = false;
    }
    if (readonly) {
        body["readonly"] = true;
    }
    if (flex_index) {
        body["use_fts"] = true;
    }
    if (preserve_expiry) {
        body["preserve_expiry"] = true;
    }
    bool check_scan_wait = false;
    if (scan_consistency) {
        switch (scan_consistency.value()) {
            case scan_consistency_type::not_bounded:
                body["scan_consistency"] = "not_bounded";
                break;
            case scan_consistency_type::request_plus:
                check_scan_wait = true;
                body["scan_consistency"] = "request_plus";
                break;
        }
    } else if (!mutation_state.empty()) {
        check_scan_wait = true;
        body["scan_consistency"] = "at_plus";
        tao::json::value scan_vectors = tao::json::empty_object;
        for (const auto& token : mutation_state) {
            auto* bucket = scan_vectors.find(token.bucket_name);
            if (bucket == nullptr) {
                scan_vectors[token.bucket_name] = tao::json::empty_object;
                bucket = scan_vectors.find(token.bucket_name);
            }
            auto& bucket_obj = bucket->get_object();
            bucket_obj[std::to_string(token.partition_id)] =
              std::vector<tao::json::value>{ token.sequence_number, std::to_string(token.partition_uuid) };
        }
        body["scan_vectors"] = scan_vectors;
    }
    if (check_scan_wait && scan_wait) {
        body["scan_wait"] = fmt::format("{}ms", scan_wait.value().count());
    }
    if (scope_qualifier) {
        body["query_context"] = scope_qualifier;
    } else if (scope_name) {
        if (bucket_name) {
            body["query_context"] = fmt::format("default:`{}`.`{}`", *bucket_name, *scope_name);
        }
    }
    for (const auto& [name, value] : raw) {
        body[name] = utils::json::parse(value);
    }
    encoded.type = type;
    encoded.headers["connection"] = "keep-alive";
    encoded.headers["content-type"] = "application/json";
    encoded.method = "POST";
    encoded.path = "/query/service";
    body_str = utils::json::generate(body);
    encoded.body = body_str;

    tao::json::value stmt = body["statement"];
    tao::json::value prep = body["prepared"];
    if (!stmt.is_string()) {
        stmt = statement;
    }
    if (!prep.is_string()) {
        prep = false;
    }
    if (ctx_->options.show_queries) {
        LOG_INFO("QUERY: client_context_id=\"{}\", prep={}, {}",
                 encoded.client_context_id,
                 utils::json::generate(prep),
                 utils::json::generate(stmt));
    } else {
        LOG_DEBUG("QUERY: client_context_id=\"{}\", prep={}, {}",
                  encoded.client_context_id,
                  utils::json::generate(prep),
                  utils::json::generate(stmt));
    }
    if (row_callback) {
        encoded.streaming.emplace(couchbase::io::streaming_settings{
          "/results/^",
          4,
          std::move(row_callback.value()),
        });
    }
    return {};
}

query_response
query_request::make_response(error_context::query&& ctx, const encoded_response_type& encoded)
{
    query_response response{ std::move(ctx) };
    response.ctx.statement = statement;
    response.ctx.parameters = body_str;
    response.served_by_node = response.ctx.last_dispatched_to.value_or("");
    if (!response.ctx.ec) {
        tao::json::value payload;
        try {
            payload = utils::json::parse(encoded.body.data());
        } catch (const tao::pegtl::parse_error&) {
            response.ctx.ec = error::common_errc::parsing_failure;
            return response;
        }
        response.meta.request_id = payload.at("requestID").get_string();

        if (const auto* i = payload.find("clientContextID"); i != nullptr) {
            response.meta.client_context_id = i->get_string();
            if (response.ctx.client_context_id != response.meta.client_context_id) {
                LOG_WARNING(R"(unexpected clientContextID returned by service: "{}", expected "{}")",
                            response.meta.client_context_id,
                            response.ctx.client_context_id);
            }
        }
        response.meta.status = payload.at("status").get_string();
        if (const auto* s = payload.find("signature"); s != nullptr) {
            response.meta.signature = couchbase::utils::json::generate(*s);
        }
        if (const auto* c = payload.find("prepared"); c != nullptr) {
            response.prepared = c->get_string();
        }
        if (const auto* p = payload.find("profile"); p != nullptr) {
            response.meta.profile = couchbase::utils::json::generate(*p);
        }

        if (const auto* m = payload.find("metrics"); m != nullptr) {
            query_response::query_metrics meta_metrics{};
            meta_metrics.result_count = m->at("resultCount").get_unsigned();
            meta_metrics.result_size = m->at("resultSize").get_unsigned();
            meta_metrics.elapsed_time = utils::parse_duration(m->at("elapsedTime").get_string());
            meta_metrics.execution_time = utils::parse_duration(m->at("executionTime").get_string());
            meta_metrics.sort_count = m->template optional<std::uint64_t>("sortCount");
            meta_metrics.mutation_count = m->template optional<std::uint64_t>("mutationCount");
            meta_metrics.error_count = m->template optional<std::uint64_t>("errorCount");
            meta_metrics.warning_count = m->template optional<std::uint64_t>("warningCount");
            response.meta.metrics.emplace(meta_metrics);
        }

        if (const auto* e = payload.find("errors"); e != nullptr) {
            std::vector<couchbase::operations::query_response::query_problem> problems{};
            for (const auto& err : e->get_array()) {
                couchbase::operations::query_response::query_problem problem;
                problem.code = err.at("code").get_unsigned();
                problem.message = err.at("msg").get_string();
                problems.emplace_back(problem);
            }
            response.meta.errors.emplace(problems);
        }

        if (const auto* w = payload.find("warnings"); w != nullptr) {
            std::vector<couchbase::operations::query_response::query_problem> problems{};
            for (const auto& warn : w->get_array()) {
                couchbase::operations::query_response::query_problem problem;
                problem.code = warn.at("code").get_unsigned();
                problem.message = warn.at("msg").get_string();
                problems.emplace_back(problem);
            }
            response.meta.warnings.emplace(problems);
        }

        if (const auto* r = payload.find("results"); r != nullptr) {
            response.rows.reserve(r->get_array().size());
            for (const auto& row : r->get_array()) {
                response.rows.emplace_back(couchbase::utils::json::generate(row));
            }
        }

        if (response.meta.status == "success") {
            if (response.prepared) {
                ctx_->cache.put(statement, response.prepared.value());
            } else if (extract_encoded_plan_) {
                extract_encoded_plan_ = false;
                if (response.rows.size() == 1) {
                    tao::json::value row{};
                    try {
                        row = utils::json::parse(response.rows[0]);
                    } catch (const tao::pegtl::parse_error&) {
                        response.ctx.ec = error::common_errc::parsing_failure;
                        return response;
                    }
                    auto* plan = row.find("encoded_plan");
                    auto* name = row.find("name");
                    if (plan != nullptr && name != nullptr) {
                        ctx_->cache.put(statement, name->get_string(), plan->get_string());
                        throw couchbase::priv::retry_http_request{};
                    }
                    response.ctx.ec = error::query_errc::prepared_statement_failure;

                } else {
                    response.ctx.ec = error::query_errc::prepared_statement_failure;
                }
            }
        } else {
            bool prepared_statement_failure = false;
            bool index_not_found = false;
            bool index_failure = false;
            bool planning_failure = false;
            bool syntax_error = false;
            bool server_timeout = false;
            bool invalid_argument = false;
            bool cas_mismatch = false;
            bool dml_failure = false;
            bool authentication_failure = false;
            std::optional<std::error_code> common_ec{};

            if (response.meta.errors) {
                for (const auto& error : *response.meta.errors) {
                    switch (error.code) {
                        case 1065: /* IKey: "service.io.request.unrecognized_parameter" */
                            invalid_argument = true;
                            break;
                        case 1080: /* IKey: "timeout" */
                            server_timeout = true;
                            break;
                        case 3000: /* IKey: "parse.syntax_error" */
                            syntax_error = true;
                            break;
                        case 4040: /* IKey: "plan.build_prepared.no_such_name" */
                        case 4050: /* IKey: "plan.build_prepared.unrecognized_prepared" */
                        case 4060: /* IKey: "plan.build_prepared.no_such_name" */
                        case 4070: /* IKey: "plan.build_prepared.decoding" */
                        case 4080: /* IKey: "plan.build_prepared.name_encoded_plan_mismatch" */
                        case 4090: /* IKey: "plan.build_prepared.name_not_in_encoded_plan" */
                            prepared_statement_failure = true;
                            break;
                        case 12009: /* IKey: "datastore.couchbase.DML_error" */
                            if (error.message.find("CAS mismatch") != std::string::npos) {
                                cas_mismatch = true;
                            } else {
                                dml_failure = true;
                            }
                            break;

                        case 12004: /* IKey: "datastore.couchbase.primary_idx_not_found" */
                        case 12016: /* IKey: "datastore.couchbase.index_not_found" */
                            index_not_found = true;
                            break;
                        case 13014: /* IKey: "datastore.couchbase.insufficient_credentials" */
                            authentication_failure = true;
                            break;
                        default:
                            if ((error.code >= 12000 && error.code < 13000) || (error.code >= 14000 && error.code < 15000)) {
                                index_failure = true;
                            } else if (error.code >= 4000 && error.code < 5000) {
                                planning_failure = true;
                            } else {
                                common_ec = management::extract_common_query_error_code(error.code, error.message);
                            }
                            break;
                    }
                }
            }
            if (syntax_error) {
                response.ctx.ec = error::common_errc::parsing_failure;
            } else if (invalid_argument) {
                response.ctx.ec = error::common_errc::invalid_argument;
            } else if (server_timeout) {
                response.ctx.ec = error::common_errc::unambiguous_timeout;
            } else if (prepared_statement_failure) {
                response.ctx.ec = error::query_errc::prepared_statement_failure;
            } else if (index_failure) {
                response.ctx.ec = error::query_errc::index_failure;
            } else if (planning_failure) {
                response.ctx.ec = error::query_errc::planning_failure;
            } else if (index_not_found) {
                response.ctx.ec = error::common_errc::index_not_found;
            } else if (cas_mismatch) {
                response.ctx.ec = error::common_errc::cas_mismatch;
            } else if (dml_failure) {
                response.ctx.ec = error::query_errc::dml_failure;
            } else if (authentication_failure) {
                response.ctx.ec = error::common_errc::authentication_failure;
            } else if (common_ec) {
                response.ctx.ec = common_ec.value();
            } else {
                LOG_TRACE("Unexpected error returned by query engine: client_context_id=\"{}\", body={}",
                          response.ctx.client_context_id,
                          encoded.body.data());
                response.ctx.ec = error::common_errc::internal_server_failure;
            }
        }
    }
    return response;
}
} // namespace couchbase::operations
