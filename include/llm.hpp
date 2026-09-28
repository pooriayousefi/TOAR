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
#include <utility>

#include "asyncore.hpp"
#include "io_thread_pool.hpp"
#include "utilities.hpp"

// Cross-platform OS headers for native HTTP client
#ifdef _WIN32
#  define WIN32_LEAN_AND_MEAN
#  define NOMINMAX
#  include <winsock2.h>
#  include <ws2tcpip.h>
#  pragma comment(lib, "ws2_32.lib")
#else
#  include <sys/socket.h>
#  include <netinet/in.h>
#  include <unistd.h>
#  include <arpa/inet.h>
#  include <netdb.h>
#endif

namespace pooriayousefi::toar
{
    using namespace core;
    using namespace io_bound;

    /// @brief Async LLM client for OpenAI-compatible chat completions API.
    ///
    /// Uses a native cross-platform HTTP client wrapped in 
    /// ThreadPool::run_blocking to send POST requests to /v1/chat/completions.
    /// Built for llama-server, Ollama, OpenAI, and any server that 
    /// implements the OpenAI chat format.
    ///
    /// All methods are coroutines — use with co_await inside an
    /// AsyncTask context (e.g., ThreadPool::run()).
    class AsyncLLMClient
    {
        std::string model_name_;
        std::string base_url_;
        std::string api_path_;
        std::string api_key_;
        ThreadPool& pool_;

    public:
        explicit AsyncLLMClient(
            ThreadPool& pool, 
            std::string model_name, 
            std::string base_url, 
            std::string api_path = {"/v1/chat/completions"},
            std::string api_key = {}
        )
            : pool_{pool}
            , model_name_{std::move(model_name)}
            , base_url_{std::move(base_url)}
            , api_path_{std::move(api_path)}
            , api_key_{std::move(api_key)}
        {
        }

        AsyncLLMClient() = delete;
        AsyncLLMClient(const AsyncLLMClient&) = delete;
        AsyncLLMClient& operator=(const AsyncLLMClient&) = delete;
        AsyncLLMClient(AsyncLLMClient&&) noexcept = default;
        AsyncLLMClient& operator=(AsyncLLMClient&&) noexcept = delete;

        [[nodiscard]] AsyncTask<std::expected<JSON, std::string>>
        reasoning(double temperature, const JSON& messages,
                  const JSON& tools_schema = JSONArray{}) const
        {
            std::expected<JSON, std::string> result{};

            JSON request_body;
            request_body["model"] = model_name_;
            request_body["messages"] = messages;
            request_body["temperature"] = temperature;
            
            // Enable SSE streaming
            request_body["stream"] = true;
            request_body["max_tokens"] = 1000;

            if (!tools_schema.empty())
            {
                request_body["tools"] = tools_schema;
                request_body["tool_choice"] = "auto";
            }

            std::string body_str = request_body.dump();

            auto http_result = co_await pool_.run_blocking(
                [this, &body_str]() -> std::expected<std::string, std::string>
                {
                    ParsedURL parsed = parse_url(base_url_);
                    if (parsed.host.empty())
                    {
                        return std::unexpected("Invalid base URL");
                    }

                    struct addrinfo hints{};
                    hints.ai_family = AF_UNSPEC;
                    hints.ai_socktype = SOCK_STREAM;
                    struct addrinfo* addr_result = nullptr;

                    if (getaddrinfo(parsed.host.c_str(), std::to_string(parsed.port).c_str(), &hints, &addr_result) != 0 || !addr_result)
                    {
                        return std::unexpected("Failed to resolve host");
                    }

                    int sock = -1;
                    for (struct addrinfo* rp = addr_result; rp != nullptr; rp = rp->ai_next)
                    {
                        sock = ::socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
                        if (sock < 0)
                        {
                            continue;
                        }

                        if (::connect(sock, rp->ai_addr, static_cast<int>(rp->ai_addrlen)) == 0)
                        {
                            break; // Success
                        }

                        ::close(sock);
                        sock = -1;
                    }

                    freeaddrinfo(addr_result);

                    if (sock < 0)
                    {
                        return std::unexpected("Failed to connect to LLM server");
                    }

                    std::string request = "POST " + api_path_ + " HTTP/1.1\r\n";
                    request += "Host: " + parsed.host + "\r\n";
                    request += "Content-Type: application/json\r\n";
                    request += "Content-Length: " + std::to_string(body_str.size()) + "\r\n";
                    request += "Connection: close\r\n";
                    if (!api_key_.empty())
                    {
                        request += "Authorization: Bearer " + api_key_ + "\r\n";
                    }
                    request += "\r\n" + body_str;

                    std::size_t total_sent = 0;
                    while (total_sent < request.size())
                    {
                        int n = ::send(sock, request.data() + total_sent, static_cast<int>(request.size() - total_sent), 0);
                        if (n <= 0)
                        {
                            ::close(sock);
                            return std::unexpected("Failed to send HTTP request");
                        }
                        total_sent += n;
                    }

                    std::string raw;
                    char chunk[4096];
                    std::string buffer;
                    JSON final_msg;
                    final_msg["role"] = "assistant";
                    std::string content_str;
                    JSON tool_calls = JSONArray{};

                    while (true)
                    {
                        int n = ::recv(sock, chunk, sizeof(chunk), 0);
                        if (n <= 0) break;
                        buffer.append(chunk, n);

                        std::size_t pos = 0;
                        while ((pos = buffer.find("\n")) != std::string::npos)
                        {
                            std::string line = buffer.substr(0, pos);
                            buffer.erase(0, pos + 1);
                            if (!line.empty() && line.back() == '\r') line.pop_back();

                            if (line.starts_with("data: "))
                            {
                                std::string data = line.substr(6);
                                if (data == "[DONE]")
                                {
                                    continue;
                                }
                                
                                auto parsed_json = pooriayousefi::json::parse(data);
                                if (parsed_json && (*parsed_json).contains("choices"))
                                {
                                    auto& choice = (*parsed_json)["choices"][0];
                                    if (choice.contains("delta"))
                                    {
                                        auto& delta = choice["delta"];
                                        if (delta.contains("content") && delta["content"].is_string())
                                        {
                                            std::string token = delta["content"].get_string();
                                            content_str += token;
                                            std::cout << token << std::flush; // Stream token to console!
                                        }
                                        if (delta.contains("tool_calls") && delta["tool_calls"].is_array())
                                        {
                                            for (auto& tc : delta["tool_calls"].get_array())
                                            {
                                                int idx = 0;
                                                if (tc.contains("index")) idx = static_cast<int>(tc["index"].get_number());
                                                
                                                while (static_cast<int>(tool_calls.size()) <= idx)
                                                {
                                                    JSON empty_tc;
                                                    empty_tc["id"] = "";
                                                    empty_tc["type"] = "function";
                                                    empty_tc["function"]["name"] = "";
                                                    empty_tc["function"]["arguments"] = "";
                                                    tool_calls.push_back(empty_tc);
                                                }
                                                
                                                if (tc.contains("id")) tool_calls[idx]["id"] = tc["id"].get_string();
                                                if (tc.contains("function"))
                                                {
                                                    if (tc["function"].contains("name")) tool_calls[idx]["function"]["name"] = tc["function"]["name"].get_string();
                                                    if (tc["function"].contains("arguments"))
                                                    {
                                                        std::string current_args = tool_calls[idx]["function"]["arguments"].get_string();
                                                        tool_calls[idx]["function"]["arguments"] = current_args + tc["function"]["arguments"].get_string();
                                                    }
                                                }
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }

                    ::close(sock);
                    std::cout << std::endl; // Newline after streaming completes

                    if (!content_str.empty()) final_msg["content"] = content_str;
                    if (!tool_calls.empty()) final_msg["tool_calls"] = tool_calls;

                    return final_msg.dump();
                }
            );

            if (!http_result)
            {
                result = std::unexpected(http_result.error());
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
                    result = body; // The reconstructed assistant message
                }
            }

            co_return result;
        }
    };
}