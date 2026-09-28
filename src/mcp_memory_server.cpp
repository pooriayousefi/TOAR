// ============================================================================
//  mcp_memory_server.cpp — MCP STDIO Server: Persistent Long-Term Memory
//  Developed by: Pooria Yousefi
//  License: Apache 2.0
//
//  A standalone MCP server that provides persistent key-value memory.
//  Saves state to 'toar_memory.json' in the working directory.
//  Communicates via newline-delimited JSON-RPC 2.0 over stdin/stdout.
//
//  Build: clang++ -std=c++23 -O3 -I include src/mcp_memory_server.cpp -o bin/mcp_memory_server
// ============================================================================
#include "poorimcp.hpp"

#include <fstream>
#include <iostream>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>

using namespace pooriayousefi::mcp;
using namespace pooriayousefi::core;
using namespace pooriayousefi::json;

namespace pooriayousefi
{
    /// @brief Thread-safe, persistent key-value memory store.
    class MemoryStore
    {
    private:
        std::mutex mtx_;
        JSONObject store_;
        std::string filename_;

    public:
        explicit MemoryStore(std::string filename = "toar_memory.json")
            : filename_{std::move(filename)}
        {
            load();
        }

        void save(const std::string& key, const JSON& value)
        {
            std::lock_guard<std::mutex> lock(mtx_);
            store_[key] = value;
            persist();
        }

        std::optional<JSON> recall(const std::string& key)
        {
            std::lock_guard<std::mutex> lock(mtx_);
            auto it = store_.find(key);
            if (it != store_.end())
            {
                return it->second;
            }
            return std::nullopt;
        }

        JSONArray list_keys()
        {
            std::lock_guard<std::mutex> lock(mtx_);
            JSONArray keys{};
            for (const auto& [k, v] : store_)
            {
                keys.push_back(k);
            }
            return keys;
        }

    private:
        void load()
        {
            std::ifstream file{filename_};
            if (file.is_open())
            {
                std::string content{
                    std::istreambuf_iterator<char>{file},
                    std::istreambuf_iterator<char>{}
                };

                if (!content.empty())
                {
                    auto parsed = pooriayousefi::json::parse(content);
                    if (parsed && parsed->is_object())
                    {
                        store_ = std::move(parsed->get_object());
                    }
                }
            }
        }

        void persist()
        {
            JSON root = store_;
            std::ofstream file{filename_};
            if (file.is_open())
            {
                file << root.dump();
            }
        }
    };
}

// ---- Main entry point -------------------------------------------------------

int main()
{
    int result{EXIT_SUCCESS};

    std::cerr << "memory_server (MCP STDIO) running...\n";

    // Instantiate the persistent memory store
    pooriayousefi::MemoryStore store;

    MCPServer server;

    // Tool 1: save_memory
    MCPTool tool1;
    tool1.name = "save_memory";
    tool1.description = "Saves a value (any JSON type) to long-term memory using a specific key. Persists across sessions.";
    tool1.parameters_schema = pooriayousefi::json::parse(R"({"type":"object","properties":{"key":{"type":"string","description":"The unique key for the memory."},"value":{"description":"The JSON value to save (can be string, number, object, array, etc.)."}},"required":["key","value"]})").value_or(JSON{});
    server.register_tool(std::move(tool1), [&store](const JSON& args) -> AsyncTask<JSON> {
        JSON result{};
        if (!args.contains("key") || !args["key"].is_string())
        {
            result["error"] = "Missing or non-string 'key' in arguments.";
        }
        else
        {
            if (!args.contains("value"))
            {
                result["error"] = "Missing 'value' in arguments.";
            }
            else
            {
                std::string key = args["key"].get_string();
                JSON value = args["value"];
                store.save(key, value);
                result["result"] = "Memory saved successfully.";
            }
        }
        co_return result;
    });

    // Tool 2: recall_memory
    MCPTool tool2;
    tool2.name = "recall_memory";
    tool2.description = "Recalls a value from long-term memory using its key. Returns null if the key is not found.";
    tool2.parameters_schema = pooriayousefi::json::parse(R"({"type":"object","properties":{"key":{"type":"string","description":"The unique key of the memory to recall."}},"required":["key"]})").value_or(JSON{});
    server.register_tool(std::move(tool2), [&store](const JSON& args) -> AsyncTask<JSON> {
        JSON result{};
        if (!args.contains("key") || !args["key"].is_string())
        {
            result["error"] = "Missing or non-string 'key' in arguments.";
        }
        else
        {
            std::string key = args["key"].get_string();
            auto val = store.recall(key);
            if (val)
            {
                result["result"] = std::move(*val);
            }
            else
            {
                result["result"] = JSON(nullptr); // Key not found
            }
        }
        co_return result;
    });

    // Tool 3: list_keys
    MCPTool tool3;
    tool3.name = "list_keys";
    tool3.description = "Lists all keys currently saved in long-term memory.";
    tool3.parameters_schema = pooriayousefi::json::parse(R"({"type":"object","properties":{}})").value_or(JSON{});
    server.register_tool(std::move(tool3), [&store](const JSON& args) -> AsyncTask<JSON> {
        JSON result{};
        JSONArray keys = store.list_keys();
        result["result"] = std::move(keys);
        co_return result;
    });

    try
    {
        sync_wait(server.start());
    }
    catch (const std::exception& e)
    {
        std::cerr << "Server crashed: " << e.what() << std::endl;
        result = EXIT_FAILURE;
    }

    return result;
}