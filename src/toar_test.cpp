#include "agent.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <print>
#include <string>
#include <thread>

using namespace pooriayousefi::core;
using namespace pooriayousefi::io_bound;
using namespace pooriayousefi::json;
using namespace pooriayousefi::mcp;
using namespace pooriayousefi::toar;

void print_test_header(const std::string& test_name)
{
    std::println("\n--- Running Test: {} ---", test_name);
}

bool test_url_parsing()
{
    bool success = true;
    print_test_header("URL Parsing");

    auto parsed = parse_url("http://localhost:8931/mcp");
    std::println("host='{}' port={} path='{}'", parsed.host, parsed.port, parsed.path);

    if (parsed.host != "localhost" || parsed.port != 8931 || parsed.path != "/mcp")
    {
        success = false;
    }

    // Test without path
    auto parsed2 = parse_url("http://192.168.1.100:8080");
    std::println("host='{}' port={} path='{}'", parsed2.host, parsed2.port, parsed2.path);

    if (parsed2.host != "192.168.1.100" || parsed2.port != 8080 || parsed2.path != "/")
    {
        success = false;
    }

    return success;
}

bool test_config_parsing()
{
    bool success = true;
    print_test_header("Config Parsing (tool_servers.json)");

    const char* filename = "test_config.json";

    {
        std::ofstream f{filename};
        f << R"({"mcp_servers": [
            {"name": "filesystem", "transport": "stdio", "command": ["npx", "-y", "@modelcontextprotocol/server-filesystem", "/tmp"], "enabled": true, "description": "FS server"},
            {"name": "playwright", "transport": "http", "url": "http://localhost:8931/mcp", "enabled": true, "description": "Playwright"},
            {"name": "disabled-srv", "transport": "stdio", "command": ["echo"], "enabled": false, "description": "Disabled"}
        ]})";
    }

    auto result = parse_server_config(filename);

    if (!result)
    {
        std::println("Parse error: {}", result.error());
        success = false;
    }
    else
    {
        std::println("Parsed {} server configs", result->size());

        if (result->size() != 3)
        {
            success = false;
        }

        // Verify stdio server
        if ((*result)[0].name != "filesystem" ||
            (*result)[0].transport != "stdio" ||
            (*result)[0].command.size() != 4 ||
            (*result)[0].command[0] != "npx")
        {
            std::println("stdio server mismatch");
            success = false;
        }

        // Verify http server
        if ((*result)[1].name != "playwright" ||
            (*result)[1].transport != "http" ||
            (*result)[1].url != "http://localhost:8931/mcp")
        {
            std::println("http server mismatch");
            success = false;
        }

        // Verify disabled server
        if ((*result)[2].enabled != false)
        {
            std::println("disabled server mismatch");
            success = false;
        }

        std::println("All config entries verified.");
    }

    std::filesystem::remove(filename);

    return success;
}

bool test_tool_discovery_and_dispatch()
{
    bool success = true;
    print_test_header("Tool Discovery + Dispatch (HTTP transport)");

    // Write test config pointing to our MCPServer.
    const char* config_filename = "test_tool_servers.json";
    {
        std::ofstream f{config_filename};
        f << R"({"mcp_servers": [
            {"name": "test-server", "transport": "http", "url": "http://127.0.0.1:9876/mcp", "enabled": true, "description": "Test MCP server"}
        ]})";
    }

    ThreadPool pool{4};

    // Start MCPServer with two tools.
    MCPServer server{9876};

    server.register_tool(
        MCPTool{"echo", "Echoes back input", JSON(nullptr)},
        [](const JSON& args) -> AsyncTask<JSON>
        {
            co_return args;
        }
    );

    server.register_tool(
        MCPTool{"calculator", "Simple calculator", JSON(nullptr)},
        [](const JSON& args) -> AsyncTask<JSON>
        {
            JSON result;
            double a = args.contains("a") ? args["a"].get_number() : 0.0;
            double b = args.contains("b") ? args["b"].get_number() : 0.0;
            result["result"] = a + b;
            co_return result;
        }
    );

    auto server_coro = [&server]() -> DetachedTask
    {
        co_await server.start();
        co_return;
    };

    auto dt = server_coro();
    auto h = dt.handle;
    dt.detach();
    pool.enqueue_raw([h]() { h.resume(); });

    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    // Create Agent.
    AgentConfig config;
    config.id = "test-agent";
    config.llm_model_name = "test-model";
    config.llm_host = "127.0.0.1";
    config.llm_port = 8080;
    config.system_prompt = "You are a test agent.";
    config.mcp_config_file = config_filename;
    config.temperature = 0.7;
    config.max_cycles = 5;

    Agent agent{std::move(config)};

    // Run setup — connects to server, discovers tools.
    auto setup_fut = pool.run(agent.setup());
    auto setup_result = setup_fut.get();

    if (!setup_result)
    {
        std::println("Setup failed: {}", setup_result.error());
        success = false;
    }
    else
    {
        std::println("Discovered {} tools", agent.tool_count());

        if (agent.tool_count() != 2)
        {
            success = false;
        }

        // Verify tools schema (OpenAI format).
        JSON schema = agent.build_tools_schema();
        std::println("Tools schema: {}", schema.dump());

        if (!schema.is_array() || schema.size() != 2)
        {
            success = false;
        }

        // Test echo dispatch.
        JSON echo_args;
        echo_args["message"] = "hello from toar!";

        auto echo_fut = pool.run(agent.dispatch_tool("echo", echo_args));
        auto echo_result = echo_fut.get();

        if (echo_result)
        {
            std::println("Echo result: {}", *echo_result);
            if (*echo_result != echo_args.dump())
            {
                success = false;
            }
        }
        else
        {
            std::println("Echo dispatch error: {}", echo_result.error());
            success = false;
        }

        // Test calculator dispatch.
        JSON calc_args;
        calc_args["a"] = 10;
        calc_args["b"] = 32;

        auto calc_fut = pool.run(agent.dispatch_tool("calculator", calc_args));
        auto calc_result = calc_fut.get();

        if (calc_result)
        {
            std::println("Calculator result: {}", *calc_result);
        }
        else
        {
            std::println("Calculator dispatch error: {}", calc_result.error());
            success = false;
        }

        // Test missing tool.
        JSON dummy_args;
        auto missing_fut = pool.run(agent.dispatch_tool("nonexistent", dummy_args));
        auto missing_result = missing_fut.get();

        if (missing_result)
        {
            std::println("Error: nonexistent tool should have failed");
            success = false;
        }
        else
        {
            std::println("Missing tool correctly returned error: {}", missing_result.error());
        }
    }

    server.stop();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    std::filesystem::remove(config_filename);

    return success;
}

bool test_chat_with_local_llm()
{
    bool success = true;
    print_test_header("Chat with Local LLM (optional — requires llama-server / Ollama)");

    // Write a minimal config with no MCP servers.
    const char* config_filename = "test_llm_config.json";
    {
        std::ofstream f{config_filename};
        f << R"({"mcp_servers": []})";
    }

    ThreadPool pool{4};

    AgentConfig config;
    config.id = "llm-test-agent";
    config.llm_model_name = "qwen2.5-coder:14b";
    config.llm_host = "127.0.0.1";
    config.llm_port = 8080;
    config.system_prompt = "You are a helpful assistant. Keep responses brief.";
    config.mcp_config_file = config_filename;
    config.temperature = 0.7;
    config.max_cycles = 3;

    Agent agent{std::move(config)};

    auto chat_fut = pool.run(agent.chat("Say hello in one word."));
    std::string response = chat_fut.get();

    std::println("LLM response: {}", response);

    if (response.empty() ||
        response.find("maximum reasoning cycles") != std::string::npos)
    {
        std::println("Note: LLM server may not be running on 127.0.0.1:8080.");
        std::println("Skipping (not a failure).");
        // Don't fail — this test is optional.
    }
    else
    {
        std::println("LLM responded successfully.");
    }

    std::filesystem::remove(config_filename);

    return success;
}

int main()
{
    int exit_code = EXIT_SUCCESS;

    try
    {
        bool all_passed = true;

        if (!test_url_parsing())
        {
            all_passed = false;
        }
        if (!test_config_parsing())
        {
            all_passed = false;
        }
        if (!test_tool_discovery_and_dispatch())
        {
            all_passed = false;
        }
        if (!test_chat_with_local_llm())
        {
            all_passed = false;
        }

        std::println("\n==============================");
        if (all_passed)
        {
            std::println("ALL TOAR TESTS PASSED!");
        }
        else
        {
            std::println("SOME TOAR TESTS FAILED!");
            exit_code = EXIT_FAILURE;
        }
        std::println("==============================");
    }
    catch (const std::exception& e)
    {
        std::println("Exception occurred: {}", e.what());
        exit_code = EXIT_FAILURE;
    }

    return exit_code;
}