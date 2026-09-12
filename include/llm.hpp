// ============================================================================
//  llm.hpp — Async LLM Client (OpenAI-compatible API)
//  Developed by: Pooria Yousefi
//  License: Apache 2.0
// ============================================================================
#pragma once

#include <expected>
#include <format>
#include <print>
#include <string>
#include <string_view>

#include "poorimcp.hpp"
#include "utilities.hpp"

namespace pooriayousefi::toar
{
    using namespace core;
    using namespace mcp;

    /// @brief Async LLM client for OpenAI-compatible chat completions API.
    ///
    /// Uses AsyncHTTPClient (from poorimcp.hpp) to send POST requests
    /// to /v1/chat/completions.  Built for llama-server, Ollama, OpenAI,
    /// and any server that implements the OpenAI chat format.
    ///
    /// All methods are coroutines — use with co_await inside an
    /// AsyncTask context (e.g., ThreadPool::run()).
    class AsyncLLMClient
    {
        std::string model_name_{};
        std::string host_{};
        int port_{80};
        std::string api_path_{"/v1/chat/completions"};

    public:
        AsyncLLMClient() = default;

        AsyncLLMClient(std::string model_name, std::string host, int port)
            : model_name_{std::move(model_name)}
            , host_{std::move(host)}
            , port_{port}
        {
        }

        AsyncLLMClient(const AsyncLLMClient&) = delete;
        AsyncLLMClient& operator=(const AsyncLLMClient&) = delete;
        AsyncLLMClient(AsyncLLMClient&&) noexcept = default;
        AsyncLLMClient& operator=(AsyncLLMClient&&) noexcept = default;

        /// @brief Sends messages + tool schema to the LLM and returns the
        ///        assistant's message object.
        ///
        /// The returned JSON contains the full assistant message, including
        /// "role", "content", and optionally "tool_calls".
        ///
        /// @param temperature   sampling temperature (0.0 - 2.0).
        /// @param messages       the conversation messages array.
        /// @param tools_schema   OpenAI-compatible tool definitions (optional).
        /// @return the assistant message JSON, or an error string.
        [[nodiscard]] AsyncTask<std::expected<JSON, std::string>>
        reasoning(double temperature, const JSON& messages,
                  const JSON& tools_schema = JSONArray{}) const
        {
            std::expected<JSON, std::string> result{};

            JSON request_body;
            request_body["model"] = model_name_;
            request_body["messages"] = messages;
            request_body["temperature"] = temperature;

            if (!tools_schema.empty())
            {
                request_body["tools"] = tools_schema;
                request_body["tool_choice"] = "auto";
            }

            std::string body_str = request_body.dump();

            AsyncHTTPClient http_client{host_, port_};

            auto http_result = co_await http_client.post(api_path_, body_str,
                                                          "application/json");

            if (!http_result)
            {
                result = std::unexpected(
                    std::format("HTTP error: {}", http_result.error().message())
                );
            }
            else
            {
                auto parsed_body = pooriayousefi::json::parse(*http_result);

                if (!parsed_body)
                {
                    result = std::unexpected(
                        std::format("JSON parse failed. Raw: {}", *http_result)
                    );
                }
                else
                {
                    JSON& body = *parsed_body;

                    if (!body.contains("choices") || body["choices"].empty())
                    {
                        result = std::unexpected("LLM response missing 'choices' array.");
                    }
                    else if (!body["choices"][0].contains("message"))
                    {
                        result = std::unexpected(
                            "LLM response 'choices[0]' missing 'message' key."
                        );
                    }
                    else
                    {
                        result = body["choices"][0]["message"];
                    }
                }
            }

            co_return result;
        }
    };
}