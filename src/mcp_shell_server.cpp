// ============================================================================
//  shell_server.cpp — MCP STDIO Server: Shell Command Execution
//  Developed by: Pooria Yousefi
//  License: Apache 2.0
//
//  A standalone MCP server that runs shell commands.
//  Communicates via newline-delimited JSON-RPC 2.0 over stdin/stdout.
//
//  Security: Set TOAR_SHELL_ENABLED=1 to enable execution.
//  A denylist blocks catastrophic commands (rm -rf /, mkfs, etc.).
//
//  Build: clang++ -std=c++23 -O3 -I include src/mcp_shell_server.cpp -o bin/mcp_shell_server
//  Config: {"transport":"stdio","command":["./bin/mcp_shell_server"],"enabled":true}
// ============================================================================
#include "poorijson.hpp"
#include "poorijsonrpc.hpp"

#include <array>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
#include <string_view>
#include <algorithm>
#include <cctype>

using namespace pooriayousefi::json;

#ifdef _WIN32
    #define POPEN _popen
    #define PCLOSE _pclose
#else
    #define POPEN popen
    #define PCLOSE pclose
#endif

// ---- Shell utilities --------------------------------------------------------

std::string escape_shell_arg(const std::string& arg)
{
    std::string out = "'";
    for (char c : arg)
    {
        if (c == '\'')
        {
            out += "'\\''";
        }
        else
        {
            out += c;
        }
    }
    out += "'";
    return out;
}

bool shell_execution_enabled()
{
    const char* flag = std::getenv("TOAR_SHELL_ENABLED");
    return flag != nullptr && std::string(flag) == "1";
}

std::string to_lower(std::string s)
{
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c)
                   {
                       return static_cast<char>(std::tolower(c));
                   });
    return s;
}

bool is_denylisted(const std::string& command)
{
    static const std::array<std::string, 10> denylist = {
        "rm -rf /",
        "rm -rf --no-preserve-root",
        "mkfs",
        "dd if=/dev/zero",
        "dd of=/dev/sd",
        ":(){ :|:& };:",
        "shutdown",
        "reboot",
        "halt",
        "init 0"
    };

    std::string lowered = to_lower(command);
    for (const auto& pattern : denylist)
    {
        if (lowered.find(pattern) != std::string::npos)
        {
            return true;
        }
    }
    return false;
}

std::string exec_command(const std::string& cmd, const std::string& input_str)
{
    std::string result{};

    std::string safe_cmd{};
    if (!input_str.empty())
    {
        safe_cmd = "printf '%s' " + escape_shell_arg(input_str) + " | " + cmd + " 2>&1";
    }
    else
    {
        safe_cmd = cmd + " 2>&1";
    }

    std::unique_ptr<FILE, decltype(&PCLOSE)> pipe(POPEN(safe_cmd.c_str(), "r"), PCLOSE);
    if (!pipe)
    {
        result = "Error: Failed to open pipe for command execution.";
    }
    else
    {
        std::array<char, 128> buffer{};
        while (fgets(buffer.data(), buffer.size(), pipe.get()) != nullptr)
        {
            result += buffer.data();
        }
    }

    return result;
}

// ---- MCP tool schema --------------------------------------------------------

JSON get_tool_schema()
{
    JSON schema;
    schema["type"] = "object";
    schema["properties"]["command"]["type"] = "string";
    schema["properties"]["command"]["description"] = "The exact shell command to execute (e.g., 'ls -la' or 'python main.py').";
    schema["properties"]["input"]["type"] = "string";
    schema["properties"]["input"]["description"] = "Optional. Standard input to feed to the command.";
    schema["required"] = JSONArray{"command"};
    return schema;
}

// ---- MCP tool handler -------------------------------------------------------

JSON handle_run_command(const JSON& args)
{
    JSON result{};

    if (!shell_execution_enabled())
    {
        result["error"] = "Shell execution is disabled. Set TOAR_SHELL_ENABLED=1 to enable.";
        return result;
    }

    if (!args.contains("command") || !args["command"].is_string())
    {
        result["error"] = "Missing 'command' in arguments.";
        return result;
    }

    std::string command = args["command"].get_string();
    std::string input_str{};
    if (args.contains("input") && args["input"].is_string())
    {
        input_str = args["input"].get_string();
    }

    if (is_denylisted(command))
    {
        result["error"] = "Command rejected: matches a denylisted catastrophic pattern.";
        return result;
    }

    std::cerr << "[shell_server] Executing: " << command << std::endl;

    std::string output = exec_command(command, input_str);

    if (output.size() > 8000)
    {
        output = output.substr(0, 8000) + "\n...[Output truncated]...";
    }

    result["output"] = output;
    return result;
}

// ---- MCP JSON-RPC handler ---------------------------------------------------

JSON handle_rpc(const JSON& req)
{
    JSON resp{};

    // Notifications (no id) — don't respond.
    if (!req.contains("id"))
    {
        return JSON(nullptr);
    }

    if (!req.contains("method"))
    {
        return rpc::make_error(rpc::ErrorCode::INVALID_REQUEST,
                               "Missing method", req["id"]);
    }

    std::string method = req["method"].get_string();

    if (method == "initialize")
    {
        JSON server_info;
        server_info["name"] = "shell-server";
        server_info["version"] = "1.0.0";

        JSON caps;
        caps["tools"] = JSONObject{};

        JSON result_obj;
        result_obj["protocolVersion"] = "2026-07-24";
        result_obj["serverInfo"] = server_info;
        result_obj["capabilities"] = caps;

        return rpc::make_response(std::move(result_obj), req["id"]);
    }

    if (method == "tools/list")
    {
        JSON tool;
        tool["name"] = "run_command";
        tool["description"] = "Executes an arbitrary shell command locally and returns combined stdout/stderr.";
        tool["inputSchema"] = get_tool_schema();

        JSONArray tools_arr;
        tools_arr.push_back(tool);

        JSON result_obj;
        result_obj["tools"] = std::move(tools_arr);

        return rpc::make_response(std::move(result_obj), req["id"]);
    }

    if (method == "tools/call")
    {
        if (!req.contains("params") || !req["params"].contains("name"))
        {
            return rpc::make_error(rpc::ErrorCode::INVALID_PARAMS,
                                   "Missing tool name", req["id"]);
        }

        std::string tool_name = req["params"]["name"].get_string();
        JSON args = req["params"].contains("arguments")
                      ? req["params"]["arguments"]
                      : JSON(nullptr);

        if (tool_name != "run_command")
        {
            return rpc::make_error(rpc::ErrorCode::METHOD_NOT_FOUND,
                                   "Unknown tool: " + tool_name, req["id"]);
        }

        JSON tool_result = handle_run_command(args);
        std::string text = tool_result.dump();

        JSON text_content;
        text_content["type"] = "text";
        text_content["text"] = text;

        JSONArray content_arr;
        content_arr.push_back(text_content);

        JSON result_obj;
        result_obj["content"] = std::move(content_arr);

        return rpc::make_response(std::move(result_obj), req["id"]);
    }

    return rpc::make_error(rpc::ErrorCode::METHOD_NOT_FOUND,
                           "Method not found: " + method, req["id"]);
}

// ---- Main STDIO loop --------------------------------------------------------

int main()
{
    std::cerr << "shell_server (MCP STDIO) running...\n";

    if (!shell_execution_enabled())
    {
        std::cerr << "Shell execution is DISABLED. Set TOAR_SHELL_ENABLED=1 to enable.\n";
    }

    std::string line;

    while (std::getline(std::cin, line))
    {
        // Trim trailing \r.
        if (!line.empty() && line.back() == '\r')
        {
            line.pop_back();
        }

        if (line.empty())
        {
            continue;
        }

        auto parsed = pooriayousefi::json::parse(line);
        if (!parsed)
        {
            continue;
        }

        JSON resp = handle_rpc(*parsed);

        // Don't respond to notifications (resp is null).
        if (!resp.is_null())
        {
            std::cout << resp.dump() << "\n" << std::flush;
        }
    }

    return EXIT_SUCCESS;
}