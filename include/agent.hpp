// ============================================================================
//  agent.hpp — Tool-Oriented Agentic Runtime (TOAR)
//  Developed by: Pooria Yousefi
//  License: Apache 2.0
// ============================================================================
#pragma once

#include <algorithm>
#include <print>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "llm.hpp"

namespace pooriayousefi::toar
{
    using namespace core;
    using namespace mcp;

    /// @brief Configuration for constructing an Agent.
    struct AgentConfig
    {
        ID id{};
        std::string llm_model_name{};
        std::string llm_host{};
        int llm_port{8080};
        std::string system_prompt{};
        std::string mcp_config_file{"tool_servers.json"};
        double temperature{0.7};
        int max_cycles{10};
    };

    /// @brief The agentic AI runtime.
    ///
    /// Reads MCP server configs, connects to each enabled server, discovers
    /// tools, and orchestrates a ReAct (Reason → Act → Tool Call → Loop)
    /// conversation loop with an LLM.
    ///
    /// All I/O is fully async — LLM calls, tool dispatch, and MCP transport
    /// use C++23 coroutines via co_await.
    class Agent
    {
    public:
        using ToolMap = std::unordered_map<std::string, std::size_t,
                                           JSONStringHash, JSONStringEqual>;

    private:
        ID id_;
        AgentConfig config_;
        AsyncLLMClient llm_client_;
        std::vector<MCPClient> mcp_clients_{};
        std::vector<MCPTool> discovered_tools_{};
        Logger logger_;
        JSON history_;
        ToolMap tool_map_{};

    public:
        explicit Agent(AgentConfig config)
            : id_{config.id}
            , config_{std::move(config)}
            , llm_client_{config_.llm_model_name, config_.llm_host, config_.llm_port}
            , logger_{id_}
        {
            logger_.open_stream();
        }

        Agent(const Agent&) = delete;
        Agent& operator=(const Agent&) = delete;
        Agent(Agent&&) = delete;
        Agent& operator=(Agent&&) = delete;

        ~Agent() = default;

        // ----------------------------------------------------------------
        //  Setup: connect to MCP servers and discover tools
        // ----------------------------------------------------------------

        /// @brief Reads the config file, connects to each enabled MCP server,
        ///        and discovers available tools.
        ///
        /// Must be called before reason_act_loop() or chat().
        /// Must be called from inside a coroutine running on a ThreadPool
        /// (NetworkReactor::current must be set).
        [[nodiscard]] AsyncTask<std::expected<void, std::string>> setup()
        {
            std::expected<void, std::string> result{};

            auto config_result = parse_server_config(config_.mcp_config_file);
            if (!config_result)
            {
                result = std::unexpected(config_result.error());
            }
            else
            {
                std::println("{}", logger_.log(
                    std::format("Found {} server configs.", config_result->size())
                ));

                bool had_error = false;

                for (auto& server_cfg : *config_result)
                {
                    if (!server_cfg.enabled)
                    {
                        std::println("{}", logger_.log(
                            std::format("Skipping disabled server: {}", server_cfg.name)
                        ));
                        continue;
                    }

                    std::println("{}", logger_.log(
                        std::format("Connecting to: {} ({})",
                                    server_cfg.name, server_cfg.transport)
                    ));

                    MCPTransport transport{};

                    if (server_cfg.transport == "stdio")
                    {
                        if (server_cfg.command.empty())
                        {
                            std::println("{}", logger_.log(
                                std::format("Error: stdio server '{}' has no command.",
                                            server_cfg.name)
                            ));
                            had_error = true;
                            continue;
                        }

                        std::string executable = server_cfg.command[0];
                        std::vector<std::string> args{};
                        if (server_cfg.command.size() > 1)
                        {
                            args.assign(server_cfg.command.begin() + 1,
                                        server_cfg.command.end());
                        }

                        transport = MCPTransport::create_stdio(std::move(executable),
                                                                 std::move(args));
                    }
                    else if (server_cfg.transport == "http")
                    {
                        if (server_cfg.url.empty())
                        {
                            std::println("{}", logger_.log(
                                std::format("Error: http server '{}' has no url.",
                                            server_cfg.name)
                            ));
                            had_error = true;
                            continue;
                        }

                        ParsedURL parsed = parse_url(server_cfg.url);
                        transport = MCPTransport::create_http(std::move(parsed.host),
                                                               parsed.port,
                                                               std::move(parsed.path));
                    }
                    else
                    {
                        std::println("{}", logger_.log(
                            std::format("Error: unknown transport '{}' for server '{}'",
                                        server_cfg.transport, server_cfg.name)
                        ));
                        had_error = true;
                        continue;
                    }

                    MCPClient client{std::move(transport)};

                    auto connect_result = co_await client.connect_async();
                    if (!connect_result)
                    {
                        std::println("{}", logger_.log(
                            std::format("Failed to connect to '{}': {}",
                                        server_cfg.name, connect_result.error().message())
                        ));
                        had_error = true;
                        continue;
                    }

                    auto init_result = co_await client.initialize();
                    if (!init_result)
                    {
                        std::println("{}", logger_.log(
                            std::format("Failed to initialize '{}': {}",
                                        server_cfg.name, init_result.error())
                        ));
                        had_error = true;
                        continue;
                    }

                    std::size_t client_index = mcp_clients_.size();
                    for (const auto& tool : client.get_tools())
                    {
                        std::println("{}", logger_.log(
                            std::format("Discovered tool: {} — {}", tool.name, tool.description)
                        ));
                        discovered_tools_.push_back(tool);
                        tool_map_[tool.name] = client_index;
                    }

                    mcp_clients_.push_back(std::move(client));

                    std::println("{}", logger_.log(
                        std::format("Connected to {} — {} tools discovered.",
                                    server_cfg.name, client.get_tools().size())
                    ));
                }

                if (!had_error || !mcp_clients_.empty())
                {
                    std::println("{}", logger_.log(
                        std::format("Setup complete. {} tools available across {} servers.",
                                    discovered_tools_.size(), mcp_clients_.size())
                    ));
                    result = {};
                }
                else
                {
                    result = std::unexpected("Failed to connect to any MCP server.");
                }
            }

            co_return result;
        }

        // ----------------------------------------------------------------
        //  Tool schema building (OpenAI function-calling format)
        // ----------------------------------------------------------------

        /// @brief Builds an OpenAI-compatible tools schema array from
        ///        discovered MCP tools.
        [[nodiscard]] JSON build_tools_schema() const
        {
            JSON schema = JSONArray{};

            for (const auto& tool : discovered_tools_)
            {
                JSON tool_schema;
                tool_schema["type"] = "function";
                tool_schema["function"]["name"] = tool.name;
                tool_schema["function"]["description"] = tool.description;

                if (!tool.parameters_schema.is_null())
                {
                    tool_schema["function"]["parameters"] = tool.parameters_schema;
                }
                else
                {
                    tool_schema["function"]["parameters"] = JSONObject{};
                }

                schema.push_back(tool_schema);
            }

            return schema;
        }

        // ----------------------------------------------------------------
        //  Tool dispatch
        // ----------------------------------------------------------------

        /// @brief Routes a tool call to the appropriate MCPClient.
        ///
        /// @param tool_name  the name of the tool to call.
        /// @param args       the JSON arguments for the tool.
        /// @return the tool's text result, or an error string.
        [[nodiscard]] AsyncTask<std::expected<std::string, std::string>>
        dispatch_tool(const std::string& tool_name, const JSON& args)
        {
            std::expected<std::string, std::string> result{};

            auto it = tool_map_.find(tool_name);
            if (it == tool_map_.end())
            {
                result = std::unexpected(
                    std::format("Tool '{}' not found.", tool_name)
                );
            }
            else
            {
                std::size_t client_index = it->second;

                if (client_index >= mcp_clients_.size())
                {
                    result = std::unexpected(
                        std::format("Client index {} out of range.", client_index)
                    );
                }
                else
                {
                    auto call_result = co_await
                        mcp_clients_[client_index].call_tool_async(tool_name, args);

                    if (!call_result)
                    {
                        result = std::unexpected(call_result.error());
                    }
                    else
                    {
                        result = std::move(*call_result);
                    }
                }
            }

            co_return result;
        }

        // ----------------------------------------------------------------
        //  ReAct loop (core engine)
        // ----------------------------------------------------------------

        /// @brief The ReAct loop: Reason → Act → Tool Call → Loop.
        ///
        /// Sends the conversation to the LLM. If the LLM requests tool calls,
        /// dispatches them to the appropriate MCPClient, appends the results
        /// as "tool" role messages, and loops until the LLM produces a final
        /// text answer or max_cycles is reached.
        ///
        /// @param messages  the conversation messages (mutated in-place).
        /// @param max_cycles  maximum iterations before giving up.
        /// @return the final text answer from the LLM.
        [[nodiscard]] AsyncTask<std::string>
        reason_act_loop(JSON& messages, int max_cycles = 10)
        {
            std::string final_result{};

            std::println("{}", logger_.log(
                std::format("STARTING REACT LOOP ({})", id_)
            ));

            JSON tools_schema = build_tools_schema();
            int cycle_count = 0;

            while (cycle_count < max_cycles && final_result.empty())
            {
                cycle_count++;
                std::println("{}", logger_.log(std::format("CYCLE {}", cycle_count)));

                auto reasoning_result = co_await llm_client_.reasoning(
                    config_.temperature, messages, tools_schema
                );

                if (!reasoning_result)
                {
                    std::println("{}", logger_.log(
                        std::format("LLM error: {}", reasoning_result.error())
                    ));
                    break;
                }

                JSON assistant_msg = std::move(*reasoning_result);

                // Some local LLMs (llama.cpp) reject null content on assistant messages.
                if (!assistant_msg.contains("content") || assistant_msg["content"].is_null())
                {
                    assistant_msg["content"] = "";
                }

                messages.push_back(assistant_msg);

                if (assistant_msg.contains("tool_calls") &&
                    !assistant_msg["tool_calls"].empty())
                {
                    JSON tool_calls = assistant_msg["tool_calls"];

                    for (const auto& tc : tool_calls)
                    {
                        std::string tool_name{};
                        if (tc.contains("function") && tc["function"].contains("name"))
                        {
                            tool_name = tc["function"]["name"].get_string();
                        }

                        std::string args_str = "{}";
                        if (tc.contains("function") && tc["function"].contains("arguments"))
                        {
                            args_str = tc["function"]["arguments"].get_string();
                        }

                        std::string tool_call_id = "0";
                        if (tc.contains("id"))
                        {
                            tool_call_id = tc["id"].get_string();
                        }

                        auto parsed_args = pooriayousefi::json::parse_lenient(
                            args_str.empty() ? "{}" : args_str
                        );
                        JSON arguments = parsed_args.value_or(JSON{});

                        std::println("{}", logger_.log(
                            std::format("Dispatching tool: {}", tool_name)
                        ));

                        auto dispatch_result = co_await dispatch_tool(tool_name, arguments);

                        std::string obs_str{};
                        if (dispatch_result)
                        {
                            obs_str = std::move(*dispatch_result);
                        }
                        else
                        {
                            obs_str = std::format("Error: {}", dispatch_result.error());
                        }

                        std::println("{}", logger_.log(
                            std::format("Observation for {}: {}", tool_name, obs_str)
                        ));

                        JSON tool_msg;
                        tool_msg["role"] = "tool";
                        tool_msg["tool_call_id"] = tool_call_id;
                        tool_msg["content"] = obs_str;
                        messages.push_back(tool_msg);
                    }
                }
                else
                {
                    final_result = assistant_msg.contains("content") &&
                                   assistant_msg["content"].is_string()
                        ? assistant_msg["content"].get_string()
                        : "";

                    std::println("{}", logger_.log("Loop terminated. Final answer received."));
                    std::println("{}", logger_.log(
                        std::format("FINAL RESPONSE: {}", final_result)
                    ));
                }
            }

            if (final_result.empty())
            {
                std::println("{}", logger_.log("Loop exceeded max cycles. Terminating."));
                final_result = "(Agent reached maximum reasoning cycles without a final answer.)";
            }

            co_return final_result;
        }

        // ----------------------------------------------------------------
        //  Chat (stateful multi-turn)
        // ----------------------------------------------------------------

        /// @brief Multi-turn chat with persistent conversation history.
        ///
        /// Appends the user prompt to history, runs the ReAct loop,
        /// syncs new messages back to history, and returns the final text.
        ///
        /// @param user_prompt  the user's input text.
        /// @return the agent's final text response.
        [[nodiscard]] AsyncTask<std::string> chat(std::string_view user_prompt)
        {
            if (!user_prompt.empty())
            {
                JSON user_msg;
                user_msg["role"] = "user";
                user_msg["content"] = std::string{user_prompt};
                history_.push_back(user_msg);
            }

            JSON messages = JSONArray{};

            JSON sys_msg;
            sys_msg["role"] = "system";
            sys_msg["content"] = config_.system_prompt;
            messages.push_back(sys_msg);

            for (const auto& msg : history_)
            {
                messages.push_back(msg);
            }

            std::size_t history_size_before = history_.size();

            std::string final_text = co_await reason_act_loop(messages, config_.max_cycles);

            for (std::size_t i = history_size_before + 1; i < messages.size(); ++i)
            {
                history_.push_back(messages[i]);
            }

            if (final_text.empty())
            {
                final_text = "Chat exceeded max cycles or encountered an error.";
            }

            std::println("{}", logger_.log(std::format("[{}]: {}", id_, final_text)));

            co_return final_text;
        }

        // ----------------------------------------------------------------
        //  Accessors
        // ----------------------------------------------------------------

        [[nodiscard]] const ID& get_id() const noexcept
        {
            return id_;
        }

        [[nodiscard]] std::size_t tool_count() const noexcept
        {
            return discovered_tools_.size();
        }

        [[nodiscard]] const std::vector<MCPTool>& get_discovered_tools() const noexcept
        {
            return discovered_tools_;
        }

        [[nodiscard]] const JSON& get_history() const noexcept
        {
            return history_;
        }

        void clear_history() noexcept
        {
            history_ = JSONArray{};
        }
    };
}