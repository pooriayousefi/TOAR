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
#include "poorimcp.hpp"

#include <array>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
#include <string_view>
#include <algorithm>
#include <cctype>

using namespace pooriayousefi::mcp;
using namespace pooriayousefi::core;
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
    bool result = flag != nullptr && std::string(flag) == "1";
    return result;
}

std::string to_lower(std::string s)
{
    std::transform(
        s.begin(), 
        s.end(), 
        s.begin(),
        [](unsigned char c)
        {
            return static_cast<char>(std::tolower(c));
        }
    );
    return s;
}

bool is_denylisted(const std::string& command)
{
    bool result = false;
    
    // Static const local variable complies with Rule #2 (no global variables)
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
            result = true;
        }
    }
    return result;
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

    // Rule #3: Use smart pointers instead of raw pointers.
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

AsyncTask<JSON> handle_run_command(const JSON& args)
{
    JSON result{};
    
    if (!shell_execution_enabled())
    {
        result["error"] = "Shell execution is disabled. Set TOAR_SHELL_ENABLED=1 to enable.";
    }
    else
    {
        if (!args.contains("command") || !args["command"].is_string())
        {
            result["error"] = "Missing 'command' in arguments.";
        }
        else
        {
            std::string command = args["command"].get_string();
            std::string input_str{};
            
            if (args.contains("input") && args["input"].is_string())
            {
                input_str = args["input"].get_string();
            }
            
            if (is_denylisted(command))
            {
                result["error"] = "Command rejected: matches a denylisted catastrophic pattern.";
            }
            else
            {
                std::cerr << "[shell_server] Executing: " << command << std::endl;
                std::string output = exec_command(command, input_str);
                
                if (output.size() > 8000)
                {
                    output = output.substr(0, 8000) + "\n...[Output truncated]...";
                }
                result["output"] = output;
            }
        }
    }
    
    co_return result;
}

// ---- Main entry point -------------------------------------------------------

int main()
{
    int result{EXIT_SUCCESS};

    std::cerr << "shell_server (MCP STDIO) running...\n";

    if (!shell_execution_enabled())
    {
        std::cerr << "Shell execution is DISABLED. Set TOAR_SHELL_ENABLED=1 to enable.\n";
    }

    // Instantiate the async MCP server
    MCPServer server;

    MCPTool tool;
    tool.name = "run_command";
    tool.description = "Executes an arbitrary shell command locally and returns combined stdout/stderr.";
    tool.parameters_schema = get_tool_schema();

    server.register_tool(std::move(tool), handle_run_command);

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