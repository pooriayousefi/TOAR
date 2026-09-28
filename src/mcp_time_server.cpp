// ============================================================================
//  mcp_time_server.cpp — MCP STDIO Server: Time & Date Utilities
//  Developed by: Pooria Yousefi
//  License: Apache 2.0
//
//  A standalone MCP server that provides time and date tools.
//  Communicates via newline-delimited JSON-RPC 2.0 over stdin/stdout.
//
//  Build: clang++ -std=c++23 -O3 -I include src/mcp_time_server.cpp -o bin/mcp_time_server
// ============================================================================
#include "poorimcp.hpp"

#include <chrono>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>

using namespace pooriayousefi::mcp;
using namespace pooriayousefi::core;
using namespace pooriayousefi::json;

namespace pooriayousefi
{
    // Cross-platform helper to convert tm to time_t assuming UTC
    inline time_t timegm_cross_platform(tm* tm_ptr)
    {
#ifdef _WIN32
        return _mkgmtime(tm_ptr);
#else
        return timegm(tm_ptr);
#endif
    }

    // Helper to parse ISO 8601 strings (e.g., "2023-10-01T12:00:00Z" or "2023-10-01 12:00:00")
    bool parse_iso8601(const std::string& str, time_t& out_time)
    {
        bool result = false;
        std::string cleaned = str;
        
        // Replace 'T' with space for std::get_time
        size_t t_pos = cleaned.find('T');
        if (t_pos != std::string::npos)
        {
            cleaned[t_pos] = ' ';
        }
        
        // Remove trailing 'Z'
        if (!cleaned.empty() && cleaned.back() == 'Z')
        {
            cleaned.pop_back();
        }

        std::tm tm_struct{};
        std::istringstream ss(cleaned);
        ss >> std::get_time(&tm_struct, "%Y-%m-%d %H:%M:%S");
        
        if (!ss.fail())
        {
            out_time = timegm_cross_platform(&tm_struct);
            result = true;
        }
        return result;
    }

    // Helper to format time_t to a string
    std::string format_time(time_t time, const std::string& format)
    {
        std::string result;
        std::tm* tm_ptr = std::gmtime(&time);
        if (tm_ptr)
        {
            std::ostringstream ss;
            ss << std::put_time(tm_ptr, format.c_str());
            result = ss.str();
        }
        return result;
    }
}

// ---- Time Tool Handlers ----------------------------------------------------

AsyncTask<JSON> handle_get_current_time(const JSON& args)
{
    JSON result{};
    
    auto now = std::chrono::system_clock::now();
    std::time_t now_time = std::chrono::system_clock::to_time_t(now);
    
    JSON time_obj;
    time_obj["utc"] = pooriayousefi::format_time(now_time, "%Y-%m-%d %H:%M:%S");
    
    std::tm* local_tm = std::localtime(&now_time);
    if (local_tm)
    {
        std::ostringstream ss;
        ss << std::put_time(local_tm, "%Y-%m-%d %H:%M:%S %Z");
        time_obj["local"] = ss.str();
    }
    else
    {
        time_obj["local"] = "Error: Could not retrieve local time.";
    }
    
    time_obj["unix_timestamp"] = static_cast<double>(now_time);
    
    result["result"] = std::move(time_obj);
    co_return result;
}

AsyncTask<JSON> handle_calculate_time_difference(const JSON& args)
{
    JSON result{};
    
    if (!args.contains("start_time") || !args["start_time"].is_string() ||
        !args.contains("end_time") || !args["end_time"].is_string())
    {
        result["error"] = "Missing or non-string 'start_time' or 'end_time' in arguments.";
    }
    else
    {
        std::string start_str = args["start_time"].get_string();
        std::string end_str = args["end_time"].get_string();
        
        time_t start_t;
        time_t end_t;
        
        if (!pooriayousefi::parse_iso8601(start_str, start_t))
        {
            result["error"] = "Invalid 'start_time' format. Use ISO 8601 (YYYY-MM-DD HH:MM:SS).";
        }
        else if (!pooriayousefi::parse_iso8601(end_str, end_t))
        {
            result["error"] = "Invalid 'end_time' format. Use ISO 8601 (YYYY-MM-DD HH:MM:SS).";
        }
        else
        {
            double diff_seconds = std::difftime(end_t, start_t);
            
            if (diff_seconds < 0)
            {
                result["error"] = "'end_time' is before 'start_time'. Difference is negative.";
            }
            else
            {
                JSON diff_obj;
                diff_obj["seconds"] = diff_seconds;
                diff_obj["minutes"] = diff_seconds / 60.0;
                diff_obj["hours"] = diff_seconds / 3600.0;
                diff_obj["days"] = diff_seconds / 86400.0;
                
                result["result"] = std::move(diff_obj);
            }
        }
    }
    co_return result;
}

AsyncTask<JSON> handle_format_date(const JSON& args)
{
    JSON result{};
    
    if (!args.contains("date_time") || !args["date_time"].is_string())
    {
        result["error"] = "Missing or non-string 'date_time' in arguments.";
    }
    else
    {
        std::string date_str = args["date_time"].get_string();
        std::string output_format = "%Y-%m-%d %H:%M:%S";
        
        if (args.contains("output_format") && args["output_format"].is_string())
        {
            output_format = args["output_format"].get_string();
        }
        
        time_t t;
        if (!pooriayousefi::parse_iso8601(date_str, t))
        {
            result["error"] = "Invalid 'date_time' format. Use ISO 8601 (YYYY-MM-DD HH:MM:SS).";
        }
        else
        {
            std::string formatted = pooriayousefi::format_time(t, output_format);
            if (formatted.empty())
            {
                result["error"] = "Failed to format date with the provided format string.";
            }
            else
            {
                result["result"] = formatted;
            }
        }
    }
    co_return result;
}

// ---- Main entry point -------------------------------------------------------

int main()
{
    int result{EXIT_SUCCESS};

    std::cerr << "time_server (MCP STDIO) running...\n";

    MCPServer server;

    // Tool 1: get_current_time
    MCPTool tool1;
    tool1.name = "get_current_time";
    tool1.description = "Gets the current UTC time, Local time, and Unix timestamp.";
    tool1.parameters_schema = pooriayousefi::json::parse(R"({"type":"object","properties":{}})").value_or(JSON{});
    server.register_tool(std::move(tool1), handle_get_current_time);

    // Tool 2: calculate_time_difference
    MCPTool tool2;
    tool2.name = "calculate_time_difference";
    tool2.description = "Calculates the exact difference between two ISO 8601 date-times (e.g., '2023-10-01 12:00:00'). Returns difference in seconds, minutes, hours, and days.";
    tool2.parameters_schema = pooriayousefi::json::parse(R"({"type":"object","properties":{"start_time":{"type":"string","description":"The start date-time (YYYY-MM-DD HH:MM:SS)."},"end_time":{"type":"string","description":"The end date-time (YYYY-MM-DD HH:MM:SS)."}},"required":["start_time","end_time"]})").value_or(JSON{});
    server.register_tool(std::move(tool2), handle_calculate_time_difference);

    // Tool 3: format_date
    MCPTool tool3;
    tool3.name = "format_date";
    tool3.description = "Parses an ISO 8601 date-time and formats it into a custom string format (using std::put_time conventions, e.g., '%Y/%m/%d').";
    tool3.parameters_schema = pooriayousefi::json::parse(R"({"type":"object","properties":{"date_time":{"type":"string","description":"The date-time string to format (YYYY-MM-DD HH:MM:SS)."},"output_format":{"type":"string","description":"The desired output format (e.g., '%Y/%m/%d %H:%M'). Defaults to '%Y-%m-%d %H:%M:%S'."}},"required":["date_time"]})").value_or(JSON{});
    server.register_tool(std::move(tool3), handle_format_date);

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