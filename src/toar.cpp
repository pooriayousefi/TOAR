// ============================================================================
//  main.cpp — TOAR Interactive Chat Session
//  Developed by: Pooria Yousefi
//  License: Apache 2.0
// ============================================================================
#include "agent.hpp"

#include <chrono>
#include <iostream>
#include <print>
#include <ranges>
#include <string>
#include <thread>

using namespace pooriayousefi::core;
using namespace pooriayousefi::io_bound;
using namespace pooriayousefi::json;
using namespace pooriayousefi::mcp;
using namespace pooriayousefi::toar;

// ─────────────────────────────────────────────────────────────────────────
//  Simple Banner
// ─────────────────────────────────────────────────────────────────────────

void print_banner()
{
    std::println("");
    std::println("  ==========================================================");
    std::println("  ||    T O A R  —  Tool-Oriented Agentic AI Runtime      ||");
    std::println("  ||         C++23 | Zero-Dep | MCP | JSON-RPC            ||");
    std::println("  ==========================================================");
    std::println("");
}

// ─────────────────────────────────────────────────────────────────────────
//  Entry point
// ─────────────────────────────────────────────────────────────────────────

int main(int argc, char* argv[])
{
    int exit_code{EXIT_FAILURE};

    // Rule #6: Use a lambda to enforce a single return point in main.
    auto run = [&]() -> void
    {
        print_banner();

        // ── Parse CLI arguments ─────────────────────────────────────
        if (argc < 4)
        {
            std::println("  Usage: ./toar <llm_url> <model_name> <temperature> [api_key]");
            std::println("  Example: ./toar http://localhost:8080 gpt-oss-20b 0.7");
            std::println("  Example: ./toar https://api.openai.com gpt-4o 0.7 sk-...");
            std::println("");
            std::println("  Then type messages and press Enter.");
            std::println("  Commands: /quit  /clear  /tools  /history");
            std::println("");
            exit_code = EXIT_FAILURE;
            return;
        }

        std::string llm_url_str = argv[1];
        std::string model_name = argv[2];
        std::string temp_str = argv[3];
        std::string api_key{};

        if (argc > 4)
        {
            api_key = argv[4];
        }

        // Parse temperature.
        double temperature = 0.7;
        try
        {
            temperature = std::stod(temp_str);
        }
        catch (...)
        {
            std::println("  Error: Invalid temperature '{}'. Using default 0.7.", temp_str);
        }

        // ── Configuration ──────────────────────────────────────────
        AgentConfig config;
        config.id = "toar-agent";
        config.llm_model_name = model_name;
        config.llm_base_url = llm_url_str; // httplib parses the URL natively
        config.llm_api_key = api_key;
        config.system_prompt =
            "You are TOAR, an autonomous tool-oriented agentic runtime.\n"
            "You have access to external tools via the Model Context Protocol.\n"
            "When you need information or want to perform an action, call a tool.\n"
            "When you have the answer, respond with text (no tool call).\n"
            "Be concise and precise.";

        config.mcp_config_file = "tool_servers.json";
        config.temperature = temperature;
        config.max_cycles = 30;

        std::println("  Agent ID    : {}", config.id);
        std::println("  LLM Model   : {}", config.llm_model_name);
        std::println("  LLM URL     : {}", config.llm_base_url);
        std::println("  API Key     : {}", config.llm_api_key.empty() ? "(none)" : "****");
        std::println("  MCP Config  : {}", config.mcp_config_file);
        std::println("  Max Cycles  : {}", config.max_cycles);
        std::println("  Temperature : {}", config.temperature);
        std::println("\n  Initializing...");

        // ── Thread Pool ─────────────────────────────────────────────
        ThreadPool pool{4};

        // ── Create Agent ───────────────────────────────────────────
        Agent agent{pool, std::move(config)};

        // ── Setup: Connect to MCP servers, discover tools ─────────
        std::println("\n  Connecting to MCP servers...");

        auto setup_fut = pool.run(agent.setup());
        auto setup_result = setup_fut.get();

        if (!setup_result)
        {
            std::println("\n  ⚠ Setup error: {}", setup_result.error());
            std::println("  Continuing without MCP tools...");
        }

        if (agent.tool_count() > 0)
        {
            std::println("\n  ✅ {} tools discovered:", agent.tool_count());
            for (const auto& tool : agent.get_discovered_tools())
            {
                std::println("     • {} — {}", tool.name, tool.description);
            }
        }
        else
        {
            std::println("\n  Note: No MCP tools available. Agent will use LLM-only mode.");
        }

        // ── Interactive Chat Loop ──────────────────────────────────
        std::println("\n{}", std::string(50, '='));
        std::println("  TOAR is ready. Type your message and press Enter.");
        std::println("  Commands:  /quit  /clear  /tools  /history");
        std::println("{}", std::string(50, '='));

        std::string user_input{};

        while (true)
        {
            std::print("\n  You> ");
            std::cout << std::flush;

            if (!std::getline(std::cin, user_input) || user_input == "/quit")
            {
                std::println("\n  Shutting down. Goodbye!");
                break;
            }

            if (user_input == "/clear")
            {
                agent.clear_history();
                std::println("  ✅ History cleared.");
                continue;
            }

            if (user_input == "/tools")
            {
                std::println("\n  Discovered tools ({}):", agent.tool_count());
                for (const auto& tool : agent.get_discovered_tools())
                {
                    std::println("     • {} — {}", tool.name, tool.description);
                }
                continue;
            }

            if (user_input == "/history")
            {
                std::println("\n  Conversation history ({} messages):",
                             agent.get_history().size());
                for (const auto& msg : agent.get_history())
                {
                    std::string role = msg.contains("role") && msg["role"].is_string()
                        ? msg["role"].get_string() : "unknown";

                    std::string content = msg.contains("content") && msg["content"].is_string()
                        ? msg["content"].get_string() : "(no content)";

                    if (content.size() > 120)
                    {
                        content = content.substr(0, 117) + "...";
                    }

                    std::println("     [{}] {}", role, content);
                }
                continue;
            }

            if (user_input.empty())
            {
                continue;
            }

            // ── Run the ReAct loop ────────────────────────────────
            std::print("\n  TOAR> ");

            auto chat_fut = pool.run(agent.chat(user_input));
            std::string response = chat_fut.get();

            std::println("{}", response);
        }

        exit_code = EXIT_SUCCESS;
    };

    try
    {
        run();
    }
    catch (const std::exception& xxx)
    {
        std::cerr << xxx.what() << "\n\nProgram exits in 5 seconds: ";
        for (auto i : std::ranges::views::iota(1, 6))
        {
            std::this_thread::sleep_for(std::chrono::seconds(1));
            std::cout << (6 - i) << ' ' << std::flush;
        }
        std::cout << std::endl;
        exit_code = EXIT_FAILURE;
    }

    return exit_code;
}