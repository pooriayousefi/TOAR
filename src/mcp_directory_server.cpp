// ============================================================================
//  mcp_directory_server.cpp — MCP STDIO Server: Directory Operations
//  Developed by: Pooria Yousefi
//  License: Apache 2.0
//
//  A standalone MCP server that manages directory operations.
//  Communicates via newline-delimited JSON-RPC 2.0 over stdin/stdout.
//
//  Build: clang++ -std=c++23 -O3 -I include src/mcp_directory_server.cpp -o bin/mcp_directory_server
// ============================================================================
#include "poorimcp.hpp"

#include <array>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
#include <string_view>
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <system_error>

using namespace pooriayousefi::mcp;
using namespace pooriayousefi::core;
using namespace pooriayousefi::json;

// ---- Directory Tool Handlers ------------------------------------------------

AsyncTask<JSON> handle_create_directory(const JSON& args)
{
    JSON result{};
    if (!args.contains("path") || !args["path"].is_string())
    {
        result["error"] = "Missing or non-string 'path' in arguments";
    }
    else
    {
        std::string path = args["path"].get_string();
        std::error_code ec;
        std::filesystem::create_directories(path, ec);
        if (ec)
        {
            result["error"] = "Failed to create directory: " + ec.message();
        }
        else
        {
            result["result"] = "Directory created successfully (or already existed) at " + path;
        }
    }
    co_return result;
}

AsyncTask<JSON> handle_list_directory(const JSON& args)
{
    JSON result{};
    if (!args.contains("path") || !args["path"].is_string())
    {
        result["error"] = "Missing or non-string 'path' in arguments";
    }
    else
    {
        std::string path = args["path"].get_string();
        std::error_code ec;
        if (!std::filesystem::is_directory(path, ec) || ec)
        {
            result["error"] = "Directory not found: " + path;
        }
        else
        {
            JSONArray content = JSONArray{};
            std::filesystem::directory_iterator it(path, std::filesystem::directory_options::skip_permission_denied, ec);
            if (ec)
            {
                result["error"] = "Failed to open directory: " + ec.message();
            }
            else
            {
                for (const auto& entry : it)
                {
                    std::error_code entry_ec;
                    bool is_dir = entry.is_directory(entry_ec);
                    std::uintmax_t size = 0;
                    if (!is_dir)
                    {
                        size = entry.file_size(entry_ec);
                    }

                    JSON item;
                    item["name"] = entry.path().filename().string();
                    item["is_directory"] = is_dir;
                    item["size"] = static_cast<double>(size); // JSON numbers are doubles
                    content.push_back(item);
                }
                result["result"] = std::move(content);
            }
        }
    }
    co_return result;
}

AsyncTask<JSON> handle_iterate_directories_recursively(const JSON& args)
{
    JSON result{};
    if (!args.contains("path") || !args["path"].is_string())
    {
        result["error"] = "Missing or non-string 'path' in arguments";
    }
    else
    {
        std::string path = args["path"].get_string();
        std::error_code ec;
        if (!std::filesystem::is_directory(path, ec) || ec)
        {
            result["error"] = "Directory not found: " + path;
        }
        else
        {
            JSONArray content = JSONArray{};
            bool truncated = false;
            constexpr std::size_t MAX_RECURSIVE_ENTRIES = 50000;

            std::filesystem::recursive_directory_iterator it(path, std::filesystem::directory_options::skip_permission_denied, ec);
            std::filesystem::recursive_directory_iterator end;
            if (ec)
            {
                result["error"] = "Failed to open directory: " + ec.message();
            }
            else
            {
                for (; it != end; it.increment(ec))
                {
                    if (ec)
                    {
                        break;
                    }
                    if (content.size() >= MAX_RECURSIVE_ENTRIES)
                    {
                        truncated = true;
                        break;
                    }
                    content.push_back(it->path().string());
                }

                JSON payload;
                payload["entries"] = std::move(content);
                payload["truncated"] = truncated;
                result["result"] = std::move(payload);
            }
        }
    }
    co_return result;
}

AsyncTask<JSON> handle_delete_directory(const JSON& args)
{
    JSON result{};
    if (!args.contains("path") || !args["path"].is_string())
    {
        result["error"] = "Missing or non-string 'path' in arguments";
    }
    else
    {
        std::string path = args["path"].get_string();
        std::error_code ec;
        if (!std::filesystem::exists(path, ec))
        {
            result["error"] = "Directory not found: " + path;
        }
        else
        {
            auto removed_count = std::filesystem::remove_all(path, ec);
            if (ec)
            {
                result["error"] = "Failed to delete directory: " + ec.message();
            }
            else
            {
                result["result"] = "Successfully deleted " + std::to_string(removed_count) + " items at " + path;
            }
        }
    }
    co_return result;
}

AsyncTask<JSON> handle_get_last_modified_file(const JSON& args)
{
    JSON result{};
    if (!args.contains("path") || !args["path"].is_string())
    {
        result["error"] = "Missing or non-string 'path' in arguments";
    }
    else
    {
        std::string path = args["path"].get_string();
        std::error_code ec;
        if (!std::filesystem::is_directory(path, ec) || ec)
        {
            result["error"] = "Directory not found: " + path;
        }
        else
        {
            std::filesystem::path latest_file;
            std::filesystem::file_time_type latest_time{};
            bool found = false;

            std::filesystem::recursive_directory_iterator it(path, std::filesystem::directory_options::skip_permission_denied, ec);
            std::filesystem::recursive_directory_iterator end;
            if (ec)
            {
                result["error"] = "Failed to open directory: " + ec.message();
            }
            else
            {
                for (; it != end; it.increment(ec))
                {
                    if (ec)
                    {
                        break;
                    }
                    std::error_code file_ec;
                    if (it->is_regular_file(file_ec) && !file_ec)
                    {
                        std::error_code time_ec;
                        auto ftime = it->last_write_time(time_ec);
                        if (!time_ec && (!found || ftime > latest_time))
                        {
                            latest_time = ftime;
                            latest_file = it->path();
                            found = true;
                        }
                    }
                }

                if (!found)
                {
                    result["error"] = "No files found in directory: " + path;
                }
                else
                {
                    result["result"] = latest_file.string();
                }
            }
        }
    }
    co_return result;
}

// ---- Main entry point -------------------------------------------------------

int main()
{
    int result{EXIT_SUCCESS};

    std::cerr << "directory_server (MCP STDIO) running...\n";

    // Instantiate the async MCP server
    MCPServer server;

    // Tool 1: create_directory
    MCPTool tool1;
    tool1.name = "create_directory";
    tool1.description = "Creates a directory at the given absolute path. If it already exists, does nothing.";
    tool1.parameters_schema = pooriayousefi::json::parse(R"({"type":"object","properties":{"path":{"type":"string","description":"The absolute path of the directory to create."}},"required":["path"]})").value_or(JSON{});
    server.register_tool(std::move(tool1), handle_create_directory);

    // Tool 2: list_directory
    MCPTool tool2;
    tool2.name = "list_directory";
    tool2.description = "Lists the immediate files and folders inside a given directory (non-recursive).";
    tool2.parameters_schema = pooriayousefi::json::parse(R"({"type":"object","properties":{"path":{"type":"string","description":"The absolute path of the directory to list."}},"required":["path"]})").value_or(JSON{});
    server.register_tool(std::move(tool2), handle_list_directory);

    // Tool 3: iterate_directories_recursively
    MCPTool tool3;
    tool3.name = "iterate_directories_recursively";
    tool3.description = "Iterates a given directory recursively and returns a flat list of all files and folders inside it.";
    tool3.parameters_schema = pooriayousefi::json::parse(R"({"type":"object","properties":{"path":{"type":"string","description":"The absolute path of the directory which has to be iterated."}},"required":["path"]})").value_or(JSON{});
    server.register_tool(std::move(tool3), handle_iterate_directories_recursively);

    // Tool 4: delete_directory
    MCPTool tool4;
    tool4.name = "delete_directory";
    tool4.description = "Deletes a directory and all of its contents recursively. Use with caution.";
    tool4.parameters_schema = pooriayousefi::json::parse(R"({"type":"object","properties":{"path":{"type":"string","description":"The absolute path of the directory to delete."}},"required":["path"]})").value_or(JSON{});
    server.register_tool(std::move(tool4), handle_delete_directory);

    // Tool 5: get_last_modified_file
    MCPTool tool5;
    tool5.name = "get_last_modified_file";
    tool5.description = "Finds and returns the absolute path of the most recently modified file in a given directory.";
    tool5.parameters_schema = pooriayousefi::json::parse(R"({"type":"object","properties":{"path":{"type":"string","description":"The absolute path of the directory to search."}},"required":["path"]})").value_or(JSON{});
    server.register_tool(std::move(tool5), handle_get_last_modified_file);

    // Start the blocking STDIO loop. 
    // sync_wait is used to block the main thread until the server terminates.
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