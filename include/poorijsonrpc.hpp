// ============================================================================
//  poorijsonrpc.hpp — JSON-RPC 2.0 Helpers for C++23
//  Developed by: Pooria Yousefi
//  License: Apache 2.0
// ============================================================================

#pragma once

#include "poorijson.hpp"
#include <vector>

namespace pooriayousefi::json
{
    /// @brief Helpers for constructing and classifying JSON-RPC 2.0 messages.
    /// @see https://www.jsonrpc.org/specification
    namespace rpc
    {
        /// @brief The reserved JSON-RPC 2.0 error codes.
        enum class ErrorCode : int
        {
            PARSE_ERROR = -32700,      ///< Invalid JSON was received by the server.
            INVALID_REQUEST = -32600,  ///< The JSON sent is not a valid Request object.
            METHOD_NOT_FOUND = -32601, ///< The method does not exist or is not available.
            INVALID_PARAMS = -32602,   ///< Invalid method parameter(s).
            INTERNAL_ERROR = -32603    ///< Internal JSON-RPC error.
        };

        // ----------------------------------------------------------------
        //  Message construction
        // ----------------------------------------------------------------

        /// @brief Builds a JSON-RPC 2.0 request object.
        /// @param method  name of the remote method to invoke.
        /// @param params  arguments for the method; omitted when null.
        /// @param id      caller-supplied request identifier, echoed in the response.
        /// @return `{"jsonrpc":"2.0","method":...[,"params":...],"id":...}`
        [[nodiscard]] inline JSON make_request(
            std::string_view method,
            JSON params = nullptr,
            JSON id = nullptr
        )
        {
            JSON req;
            req["jsonrpc"] = "2.0";
            req["method"] = std::string(method);
            if (!params.is_null())
            {
                req["params"] = std::move(params);
            }
            req["id"] = std::move(id);
            return req;
        }

        /// @brief Builds a JSON-RPC 2.0 notification (request without an id).
        ///
        /// Notifications are fire-and-forget messages — the server MUST NOT
        /// send a response. Used heavily in MCP for events such as
        /// `notifications/initialized`, `notifications/progress`, etc.
        ///
        /// @param method  name of the notification method.
        /// @param params  arguments for the method; omitted when null.
        /// @return `{"jsonrpc":"2.0","method":...[,"params":...]}`
        [[nodiscard]] inline JSON make_notification(
            std::string_view method,
            JSON params = nullptr
        )
        {
            JSON notif;
            notif["jsonrpc"] = "2.0";
            notif["method"] = std::string(method);
            if (!params.is_null())
            {
                notif["params"] = std::move(params);
            }
            return notif;
        }

        /// @brief Builds a JSON-RPC 2.0 success response.
        /// @param result  the method's return value.
        /// @param id      the identifier of the request being answered.
        /// @return `{"jsonrpc":"2.0","result":...,"id":...}`
        [[nodiscard]] inline JSON make_response(
            JSON result,
            JSON id = nullptr
        )
        {
            JSON resp;
            resp["jsonrpc"] = "2.0";
            resp["result"] = std::move(result);
            resp["id"] = std::move(id);
            return resp;
        }

        /// @brief Builds a JSON-RPC 2.0 error response.
        /// @param code     numeric error code (see ErrorCode for the reserved ones).
        /// @param message  short human-readable description of the error.
        /// @param id       the identifier of the request being answered.
        /// @param data     optional structured details about the error; the
        ///                 "data" member is emitted only when it is not null.
        /// @return `{"jsonrpc":"2.0","error":{"code":...,"message":...[,"data":...]},"id":...}`
        [[nodiscard]] inline JSON make_error(
            int code,
            std::string_view message,
            JSON id = nullptr,
            JSON data = nullptr
        )
        {
            JSON resp;
            JSON error_obj;
            resp["jsonrpc"] = "2.0";
            error_obj["code"] = static_cast<double>(code); // JSON numbers are doubles.
            error_obj["message"] = std::string(message);
            if (!data.is_null())
            {
                error_obj["data"] = std::move(data);
            }
            resp["error"] = std::move(error_obj);
            resp["id"] = std::move(id);
            return resp;
        }

        /// @brief Builds a JSON-RPC 2.0 error response from a reserved ErrorCode.
        /// @param code     one of the reserved JSON-RPC error codes.
        /// @param message  short human-readable description of the error.
        /// @param id       the identifier of the request being answered.
        /// @param data     optional structured details about the error.
        /// @return same as make_error(int, ...) with code from ErrorCode.
        [[nodiscard]] inline JSON make_error(
            ErrorCode code,
            std::string_view message,
            JSON id = nullptr,
            JSON data = nullptr
        )
        {
            return make_error(
                static_cast<int>(code),
                message,
                std::move(id),
                std::move(data)
            );
        }

        /// @brief Builds a JSON-RPC 2.0 batch message (array of messages).
        /// @param messages  the request/notification/response objects to batch.
        /// @return `[msg0, msg1, ...]`
        [[nodiscard]] inline JSON make_batch(std::vector<JSON> messages)
        {
            return JSON(std::move(messages));
        }

        // ----------------------------------------------------------------
        //  Message classification
        // ----------------------------------------------------------------

        /// @brief The structural type of a JSON-RPC 2.0 message.
        enum class MessageType
        {
            REQUEST,          ///< Has "method" and "id" — expects a response.
            NOTIFICATION,     ///< Has "method" but no "id" — no response expected.
            RESPONSE_SUCCESS, ///< Has "result" and "id".
            RESPONSE_ERROR,   ///< Has "error" and "id".
            BATCH,            ///< A JSON array — may contain multiple messages.
            INVALID           ///< Does not match any valid JSON-RPC 2.0 shape.
        };

        /// @brief Classifies a JSON value as a JSON-RPC 2.0 message type.
        ///
        /// Determines the message type based on the presence and mutual
        /// exclusivity of the "method", "result", "error", and "id" members.
        /// This is a structural check — it does not validate the "jsonrpc"
        /// version string or the contents of individual fields.
        ///
        /// @param msg  the JSON value to classify.
        /// @return the MessageType, or INVALID if the structure doesn't match.
        [[nodiscard]] inline MessageType classify(const JSON& msg)
        {
            MessageType result = MessageType::INVALID;

            if (msg.is_array())
            {
                result = MessageType::BATCH;
            }
            else if (msg.is_object())
            {
                bool has_method = msg.contains("method");
                bool has_result = msg.contains("result");
                bool has_error = msg.contains("error");
                bool has_id = msg.contains("id");

                // "method" is mutually exclusive with "result" and "error".
                if (has_method && !has_result && !has_error)
                {
                    if (has_id)
                    {
                        result = MessageType::REQUEST;
                    }
                    else
                    {
                        result = MessageType::NOTIFICATION;
                    }
                }
                else if (has_result && !has_error && !has_method && has_id)
                {
                    result = MessageType::RESPONSE_SUCCESS;
                }
                else if (has_error && !has_result && !has_method && has_id)
                {
                    result = MessageType::RESPONSE_ERROR;
                }
                // else: stays INVALID — structurally unrecognised.
            }

            return result;
        }
    } // namespace rpc
} // namespace pooriayousefi::json