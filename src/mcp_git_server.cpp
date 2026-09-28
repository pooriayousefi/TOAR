// ============================================================================
//  mcp_git_server.cpp — MCP STDIO Server: Git Version Control
//  Developed by: Pooria Yousefi
//  License: Apache 2.0
//
//  A standalone MCP server that manages Git repositories.
//  Uses pooriprocess.hpp for safe, shell-injection-free command execution.
//  Communicates via newline-delimited JSON-RPC 2.0 over stdin/stdout.
//
//  Build: clang++ -std=c++23 -O3 -I include src/mcp_git_server.cpp -o bin/mcp_git_server
// ============================================================================
#include "poorimcp.hpp"

#include <iostream>
#include <string>
#include <vector>
#include <span>
#include <array>
#include <memory>
#include <system_error>

using namespace pooriayousefi::mcp;
using namespace pooriayousefi::core;
using namespace pooriayousefi::json;
using namespace pooriayousefi::process;

// ---- Git Command Helper -----------------------------------------------------

std::string run_git_command(const std::vector<std::string>& args)
{
    std::string result{};
    
    // Build the shell command safely by quoting arguments
    std::string cmd = "git";
    for (const auto& arg : args)
    {
        cmd += " \"";
        for (char c : arg)
        {
            if (c == '"')
            {
                cmd += "\\\""; // Escape double quotes
            }
            else if (c == '\\')
            {
                cmd += "\\\\"; // Escape backslashes
            }
            else
            {
                cmd += c;
            }
        }
        cmd += "\"";
    }
    cmd += " 2>&1"; // Redirect stderr to stdout to capture error messages

#ifdef _WIN32
    #define POPEN _popen
    #define PCLOSE _pclose
#else
    #define POPEN popen
    #define PCLOSE pclose
#endif

    // Rule #3: Use smart pointers instead of raw pointers.
    std::unique_ptr<FILE, decltype(&PCLOSE)> pipe(POPEN(cmd.c_str(), "r"), PCLOSE);
    if (!pipe)
    {
        result = "Error: Failed to open pipe for git command.";
    }
    else
    {
        std::array<char, 4096> buffer{};
        while (fgets(buffer.data(), buffer.size(), pipe.get()) != nullptr)
        {
            result += buffer.data();
        }
    }

    return result;
}

// ---- Git Tool Handlers ------------------------------------------------------

AsyncTask<JSON> handle_git_status(const JSON& args)
{
    JSON result{};
    std::string output = run_git_command({"status", "--porcelain"});
    result["result"] = output;
    co_return result;
}

AsyncTask<JSON> handle_git_diff(const JSON& args)
{
    JSON result{};
    bool staged = args.contains("staged") && args["staged"].is_boolean() && args["staged"].get_bool();
    
    if (staged)
    {
        std::string output = run_git_command({"diff", "--staged"});
        result["result"] = output;
    }
    else
    {
        std::string output = run_git_command({"diff"});
        result["result"] = output;
    }
    co_return result;
}

AsyncTask<JSON> handle_git_log(const JSON& args)
{
    JSON result{};
    int limit = 10;
    if (args.contains("limit") && args["limit"].is_number())
    {
        limit = static_cast<int>(args["limit"].get_number());
        if (limit <= 0)
        {
            limit = 10;
        }
    }

    std::string output = run_git_command({"log", "--oneline", "-n", std::to_string(limit)});
    result["result"] = output;
    co_return result;
}

AsyncTask<JSON> handle_git_add(const JSON& args)
{
    JSON result{};
    if (!args.contains("files") || !args["files"].is_array())
    {
        result["error"] = "Missing or invalid 'files' array in arguments.";
    }
    else
    {
        std::vector<std::string> git_args = {"add"};
        for (const auto& file : args["files"].get_array())
        {
            if (file.is_string())
            {
                git_args.push_back(file.get_string());
            }
        }

        if (git_args.size() <= 1)
        {
            result["error"] = "No valid file paths provided.";
        }
        else
        {
            std::string output = run_git_command(git_args);
            if (output.empty())
            {
                result["result"] = "Files staged successfully.";
            }
            else
            {
                result["result"] = output;
            }
        }
    }
    co_return result;
}

AsyncTask<JSON> handle_git_commit(const JSON& args)
{
    JSON result{};
    if (!args.contains("message") || !args["message"].is_string())
    {
        result["error"] = "Missing or non-string 'message' in arguments.";
    }
    else
    {
        std::string message = args["message"].get_string();
        std::string output = run_git_command({"commit", "-m", message});
        
        if (output.find("nothing to commit") != std::string::npos || output.find("no changes added") != std::string::npos)
        {
            result["error"] = output;
        }
        else
        {
            result["result"] = output;
        }
    }
    co_return result;
}

AsyncTask<JSON> handle_git_branch(const JSON& args)
{
    JSON result{};
    std::string output = run_git_command({"branch", "-a"});
    result["result"] = output;
    co_return result;
}

AsyncTask<JSON> handle_git_checkout(const JSON& args)
{
    JSON result{};
    if (!args.contains("branch") || !args["branch"].is_string())
    {
        result["error"] = "Missing or non-string 'branch' in arguments.";
    }
    else
    {
        std::string branch = args["branch"].get_string();
        std::string output = run_git_command({"checkout", branch});
        
        if (output.find("error") != std::string::npos || output.find("did not match") != std::string::npos)
        {
            result["error"] = output;
        }
        else
        {
            result["result"] = output;
        }
    }
    co_return result;
}

// ---- Main entry point -------------------------------------------------------

int main()
{
    int result{EXIT_SUCCESS};

    std::cerr << "git_server (MCP STDIO) running...\n";

    MCPServer server;

    // Tool 1: git_status
    MCPTool tool1;
    tool1.name = "git_status";
    tool1.description = "Gets the current working tree status (porcelain format).";
    tool1.parameters_schema = pooriayousefi::json::parse(R"({"type":"object","properties":{}})").value_or(JSON{});
    server.register_tool(std::move(tool1), handle_git_status);

    // Tool 2: git_diff
    MCPTool tool2;
    tool2.name = "git_diff";
    tool2.description = "Shows unstaged or staged changes.";
    tool2.parameters_schema = pooriayousefi::json::parse(R"({"type":"object","properties":{"staged":{"type":"boolean","description":"If true, show staged changes. Defaults to false."}}})").value_or(JSON{});
    server.register_tool(std::move(tool2), handle_git_diff);

    // Tool 3: git_log
    MCPTool tool3;
    tool3.name = "git_log";
    tool3.description = "Views commit history.";
    tool3.parameters_schema = pooriayousefi::json::parse(R"({"type":"object","properties":{"limit":{"type":"integer","description":"Maximum number of commits to return. Defaults to 10."}}})").value_or(JSON{});
    server.register_tool(std::move(tool3), handle_git_log);

    // Tool 4: git_add
    MCPTool tool4;
    tool4.name = "git_add";
    tool4.description = "Stages files for commit.";
    tool4.parameters_schema = pooriayousefi::json::parse(R"({"type":"object","properties":{"files":{"type":"array","items":{"type":"string"},"description":"Array of file paths to stage."}},"required":["files"]})").value_or(JSON{});
    server.register_tool(std::move(tool4), handle_git_add);

    // Tool 5: git_commit
    MCPTool tool5;
    tool5.name = "git_commit";
    tool5.description = "Commits staged changes with a message.";
    tool5.parameters_schema = pooriayousefi::json::parse(R"({"type":"object","properties":{"message":{"type":"string","description":"The commit message."}},"required":["message"]})").value_or(JSON{});
    server.register_tool(std::move(tool5), handle_git_commit);

    // Tool 6: git_branch
    MCPTool tool6;
    tool6.name = "git_branch";
    tool6.description = "Lists all local and remote branches.";
    tool6.parameters_schema = pooriayousefi::json::parse(R"({"type":"object","properties":{}})").value_or(JSON{});
    server.register_tool(std::move(tool6), handle_git_branch);

    // Tool 7: git_checkout
    MCPTool tool7;
    tool7.name = "git_checkout";
    tool7.description = "Switches to a specified branch.";
    tool7.parameters_schema = pooriayousefi::json::parse(R"({"type":"object","properties":{"branch":{"type":"string","description":"The name of the branch to checkout."}},"required":["branch"]})").value_or(JSON{});
    server.register_tool(std::move(tool7), handle_git_checkout);

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