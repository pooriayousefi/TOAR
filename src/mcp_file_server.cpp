// ============================================================================
//  mcp_file_server.cpp — MCP STDIO Server: File Operations
//  Developed by: Pooria Yousefi
//  License: Apache 2.0
//
//  A standalone MCP server that manages file operations.
//  Communicates via newline-delimited JSON-RPC 2.0 over stdin/stdout.
//
//  Build: clang++ -std=c++23 -O3 -I include src/mcp_file_server.cpp -o bin/mcp_file_server
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
#include <fstream>
#include <system_error>

using namespace pooriayousefi::mcp;
using namespace pooriayousefi::core;
using namespace pooriayousefi::json;

// ---- File Tool Handlers -----------------------------------------------------

AsyncTask<JSON> handle_write_text_file(const JSON& args)
{
    JSON result{};
    if (!args.contains("path") || !args["path"].is_string() ||
        !args.contains("content") || !args["content"].is_string())
    {
        result["error"] = "Missing or invalid 'path' or 'content' in arguments";
    }
    else
    {
        std::string path = args["path"].get_string();
        std::string content = args["content"].get_string();
        std::filesystem::path fs_path(path);
        std::filesystem::path parent = fs_path.parent_path();
        std::error_code ec;

        if (!parent.empty())
        {
            std::filesystem::create_directories(parent, ec);
            if (ec)
            {
                result["error"] = "Failed to create parent directories: " + ec.message();
            }
        }

        if (!result.contains("error"))
        {
            std::ofstream out(fs_path, std::ios::out | std::ios::binary | std::ios::trunc);
            if (!out)
            {
                result["error"] = "Failed to open file for writing.";
            }
            else
            {
                out << content;
                result["result"] = "File written successfully.";
            }
        }
    }
    co_return result;
}

AsyncTask<JSON> handle_read_text_file(const JSON& args)
{
    JSON result{};
    if (!args.contains("path") || !args["path"].is_string())
    {
        result["error"] = "Missing or invalid 'path' in arguments";
    }
    else
    {
        std::string path = args["path"].get_string();
        std::filesystem::path fs_path(path);

        if (!std::filesystem::is_regular_file(fs_path))
        {
            result["error"] = "Path is not a regular file.";
        }
        else
        {
            std::ifstream in(fs_path, std::ios::binary);
            if (!in)
            {
                result["error"] = "Failed to open file for reading.";
            }
            else
            {
                std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
                result["result"] = std::move(content);
            }
        }
    }
    co_return result;
}

AsyncTask<JSON> handle_append_text_file(const JSON& args)
{
    JSON result{};
    if (!args.contains("path") || !args["path"].is_string() ||
        !args.contains("content") || !args["content"].is_string())
    {
        result["error"] = "Missing or invalid 'path' or 'content' in arguments";
    }
    else
    {
        std::string path = args["path"].get_string();
        std::string content = args["content"].get_string();
        std::filesystem::path fs_path(path);
        std::filesystem::path parent = fs_path.parent_path();
        std::error_code ec;

        if (!parent.empty())
        {
            std::filesystem::create_directories(parent, ec);
            if (ec)
            {
                result["error"] = "Failed to create parent directories: " + ec.message();
            }
        }

        if (!result.contains("error"))
        {
            std::ofstream out(fs_path, std::ios::app | std::ios::binary);
            if (!out)
            {
                result["error"] = "Failed to open file for appending.";
            }
            else
            {
                out << content;
                result["result"] = "Content appended successfully.";
            }
        }
    }
    co_return result;
}

AsyncTask<JSON> handle_delete_file(const JSON& args)
{
    JSON result{};
    if (!args.contains("path") || !args["path"].is_string())
    {
        result["error"] = "Missing or invalid 'path' in arguments";
    }
    else
    {
        std::string path = args["path"].get_string();
        std::filesystem::path fs_path(path);

        if (!std::filesystem::is_regular_file(fs_path))
        {
            result["error"] = "Path is not a regular file.";
        }
        else
        {
            std::error_code ec;
            if (!std::filesystem::remove(fs_path, ec) || ec)
            {
                result["error"] = "Failed to delete file: " + ec.message();
            }
            else
            {
                result["result"] = "File deleted successfully.";
            }
        }
    }
    co_return result;
}

AsyncTask<JSON> handle_file_exists(const JSON& args)
{
    JSON result{};
    if (!args.contains("path") || !args["path"].is_string())
    {
        result["error"] = "Missing or invalid 'path' in arguments";
    }
    else
    {
        std::string path = args["path"].get_string();
        std::filesystem::path fs_path(path);
        result["result"] = std::filesystem::exists(fs_path);
    }
    co_return result;
}

AsyncTask<JSON> handle_get_file_info(const JSON& args)
{
    JSON result{};
    if (!args.contains("path") || !args["path"].is_string())
    {
        result["error"] = "Missing or invalid 'path' in arguments";
    }
    else
    {
        std::string path = args["path"].get_string();
        std::filesystem::path fs_path(path);
        std::error_code ec;
        
        bool exists = std::filesystem::exists(fs_path, ec);
        JSON info;
        info["path"] = path;
        info["exists"] = exists;
        info["is_directory"] = exists && std::filesystem::is_directory(fs_path);
        info["is_regular_file"] = exists && std::filesystem::is_regular_file(fs_path);

        if (exists && std::filesystem::is_regular_file(fs_path))
        {
            info["size_bytes"] = static_cast<double>(std::filesystem::file_size(fs_path));
        }
        result["result"] = std::move(info);
    }
    co_return result;
}

AsyncTask<JSON> handle_copy_file(const JSON& args, bool overwrite_existing)
{
    JSON result{};
    if (!args.contains("source") || !args["source"].is_string() ||
        !args.contains("destination") || !args["destination"].is_string())
    {
        result["error"] = "Missing or invalid 'source' or 'destination' in arguments";
    }
    else
    {
        std::string src_str = args["source"].get_string();
        std::string dest_str = args["destination"].get_string();
        std::filesystem::path src(src_str);
        std::filesystem::path dest(dest_str);

        if (!std::filesystem::is_regular_file(src))
        {
            result["error"] = "Source path is not a regular file.";
        }
        else
        {
            std::error_code ec;
            std::filesystem::copy_options options = overwrite_existing 
                ? std::filesystem::copy_options::overwrite_existing 
                : std::filesystem::copy_options::skip_existing;

            bool copied = std::filesystem::copy_file(src, dest, options, ec);

            if (ec)
            {
                result["error"] = "Failed to copy file: " + ec.message();
            }
            else if (!copied && !overwrite_existing && std::filesystem::exists(dest))
            {
                result["error"] = "Destination file already exists. Copy skipped.";
            }
            else if (!copied)
            {
                result["error"] = "Failed to copy file for an unknown reason.";
            }
            else
            {
                result["result"] = "File copied successfully from " + src_str + " to " + dest_str;
            }
        }
    }
    co_return result;
}

// ---- Main entry point -------------------------------------------------------

int main()
{
    int result{EXIT_SUCCESS};

    std::cerr << "file_server (MCP STDIO) running...\n";

    // Instantiate the async MCP server
    MCPServer server;

    // Tool 1: write_text_file
    MCPTool tool1;
    tool1.name = "write_text_file";
    tool1.description = "Writes text content to a file at the specified absolute path. Creates parent directories if needed. Overwrites if file exists.";
    tool1.parameters_schema = pooriayousefi::json::parse(R"({"type":"object","properties":{"path":{"type":"string","description":"The absolute file path."},"content":{"type":"string","description":"The text content to write."}},"required":["path","content"]})").value_or(JSON{});
    server.register_tool(std::move(tool1), handle_write_text_file);

    // Tool 2: read_text_file
    MCPTool tool2;
    tool2.name = "read_text_file";
    tool2.description = "Reads text content from a regular file at the specified absolute path.";
    tool2.parameters_schema = pooriayousefi::json::parse(R"({"type":"object","properties":{"path":{"type":"string","description":"The absolute file path to read."}},"required":["path"]})").value_or(JSON{});
    server.register_tool(std::move(tool2), handle_read_text_file);

    // Tool 3: append_text_file
    MCPTool tool3;
    tool3.name = "append_text_file";
    tool3.description = "Appends text content to a file at the specified absolute path.";
    tool3.parameters_schema = pooriayousefi::json::parse(R"({"type":"object","properties":{"path":{"type":"string","description":"The absolute file path to append to."},"content":{"type":"string","description":"The text content to add."}},"required":["path","content"]})").value_or(JSON{});
    server.register_tool(std::move(tool3), handle_append_text_file);

    // Tool 4: delete_file
    MCPTool tool4;
    tool4.name = "delete_file";
    tool4.description = "Deletes a regular file at the specified absolute path.";
    tool4.parameters_schema = pooriayousefi::json::parse(R"({"type":"object","properties":{"path":{"type":"string","description":"The absolute file path to delete."}},"required":["path"]})").value_or(JSON{});
    server.register_tool(std::move(tool4), handle_delete_file);

    // Tool 5: file_exists
    MCPTool tool5;
    tool5.name = "file_exists";
    tool5.description = "Checks whether a path exists at the specified absolute path.";
    tool5.parameters_schema = pooriayousefi::json::parse(R"({"type":"object","properties":{"path":{"type":"string","description":"The absolute path to check."}},"required":["path"]})").value_or(JSON{});
    server.register_tool(std::move(tool5), handle_file_exists);

    // Tool 6: get_file_info
    MCPTool tool6;
    tool6.name = "get_file_info";
    tool6.description = "Returns metadata for a path at the specified absolute path.";
    tool6.parameters_schema = pooriayousefi::json::parse(R"({"type":"object","properties":{"path":{"type":"string","description":"The absolute path to inspect."}},"required":["path"]})").value_or(JSON{});
    server.register_tool(std::move(tool6), handle_get_file_info);

    // Tool 7: copy_file_overwrite_existing
    MCPTool tool7;
    tool7.name = "copy_file_overwrite_existing";
    tool7.description = "Copies a file from a source path to a destination path, overwriting the destination file if it already exists.";
    tool7.parameters_schema = pooriayousefi::json::parse(R"({"type":"object","properties":{"source":{"type":"string","description":"The absolute path of the file to copy."},"destination":{"type":"string","description":"The absolute path of the destination file."}},"required":["source","destination"]})").value_or(JSON{});
    server.register_tool(std::move(tool7), [](const JSON& args) -> AsyncTask<JSON> {
        co_return co_await handle_copy_file(args, true);
    });

    // Tool 8: copy_file_skip_existing
    MCPTool tool8;
    tool8.name = "copy_file_skip_existing";
    tool8.description = "Copies a file from a source path to a destination path only if the destination file does not already exist. Fails if the destination exists.";
    tool8.parameters_schema = pooriayousefi::json::parse(R"({"type":"object","properties":{"source":{"type":"string","description":"The absolute path of the file to copy."},"destination":{"type":"string","description":"The absolute path of the destination file."}},"required":["source","destination"]})").value_or(JSON{});
    server.register_tool(std::move(tool8), [](const JSON& args) -> AsyncTask<JSON> {
        co_return co_await handle_copy_file(args, false);
    });

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