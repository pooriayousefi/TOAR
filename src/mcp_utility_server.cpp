// ============================================================================
//  mcp_utility_server.cpp — MCP STDIO Server: Utility Tools
//  Developed by: Pooria Yousefi
//  License: Apache 2.0
//
//  A standalone MCP server that provides string and utility tools.
//  Communicates via newline-delimited JSON-RPC 2.0 over stdin/stdout.
//
//  Build: clang++ -std=c++23 -O3 -I include src/mcp_utility_server.cpp -o bin/mcp_utility_server
// ============================================================================
#include "poorimcp.hpp"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <random>
#include <string>
#include <string_view>
#include <vector>

using namespace pooriayousefi::mcp;
using namespace pooriayousefi::core;
using namespace pooriayousefi::json;

namespace pooriayousefi
{
    inline std::vector<std::string> tokenize(
        const std::string& sentence,
        const std::string& delimiters
    )
    {
        std::vector<std::string> tokens{};
        tokens.reserve(16);

        auto last_pos = sentence.find_first_not_of(delimiters, 0);
        auto pos = sentence.find_first_of(delimiters, last_pos);

        while (pos != std::string::npos || last_pos != std::string::npos)
        {
            tokens.emplace_back(sentence.substr(last_pos, pos - last_pos));
            last_pos = sentence.find_first_not_of(delimiters, pos);
            pos = sentence.find_first_of(delimiters, last_pos);
        }

        return tokens;
    }

    inline std::string generate_uuid()
    {
        thread_local std::mt19937 gen{std::random_device{}()};
        std::uniform_int_distribution<> dis(0, 15);
        std::uniform_int_distribution<> dis2(8, 11);

        std::string result = "xxxxxxxx-xxxx-4xxx-yxxx-xxxxxxxxxxxx";
        for (char& c : result)
        {
            if (c == 'x')
            {
                int val = dis(gen);
                c = static_cast<char>(val < 10 ? '0' + val : 'a' + (val - 10));
            }
            else if (c == 'y')
            {
                int val = dis2(gen);
                c = static_cast<char>(val < 10 ? '0' + val : 'a' + (val - 10));
            }
        }
        return result;
    }

    inline std::size_t find_balanced_end(const std::string& text, std::size_t start)
    {
        char open_char = text[start];
        char close_char = (open_char == '{') ? '}' : ']';

        int balance = 0;
        bool in_string = false;
        bool escaped = false;

        for (std::size_t i = start; i < text.length(); ++i)
        {
            char c = text[i];

            if (in_string)
            {
                if (escaped)
                {
                    escaped = false;
                }
                else if (c == '\\')
                {
                    escaped = true;
                }
                else if (c == '"')
                {
                    in_string = false;
                }
                continue;
            }

            if (c == '"')
            {
                in_string = true;
                continue;
            }
            if (c == open_char)
            {
                balance++;
            }
            else if (c == close_char)
            {
                balance--;
            }

            if (balance == 0)
            {
                return i;
            }
        }
        return std::string::npos;
    }
}

// ---- Utility Tool Handlers --------------------------------------------------

AsyncTask<JSON> handle_tokenize_string(const JSON& args)
{
    JSON result{};
    if (!args.contains("sentence") || !args["sentence"].is_string() ||
        !args.contains("delimiters") || !args["delimiters"].is_string())
    {
        result["error"] = "Missing or non-string 'sentence' or 'delimiters' in arguments";
    }
    else
    {
        std::string sentence = args["sentence"].get_string();
        std::string delimiters = args["delimiters"].get_string();
        auto tokens = pooriayousefi::tokenize(sentence, delimiters);

        JSONArray tokens_array{};
        for (const auto& token : tokens)
        {
            tokens_array.push_back(token);
        }
        result["result"] = std::move(tokens_array);
    }
    co_return result;
}

AsyncTask<JSON> handle_count_words(const JSON& args)
{
    JSON result{};
    if (!args.contains("text") || !args["text"].is_string())
    {
        result["error"] = "Missing or non-string 'text' in arguments";
    }
    else
    {
        std::string text = args["text"].get_string();
        std::string delimiters = " \t\n\r";
        auto tokens = pooriayousefi::tokenize(text, delimiters);

        JSON word_count;
        word_count["word_count"] = static_cast<double>(tokens.size());
        result["result"] = std::move(word_count);
    }
    co_return result;
}

AsyncTask<JSON> handle_extract_json_from_text(const JSON& args)
{
    JSON result{};
    if (!args.contains("text") || !args["text"].is_string())
    {
        result["error"] = "Missing or non-string 'text' in arguments";
    }
    else
    {
        std::string text = args["text"].get_string();
        std::size_t search_from = 0;
        bool done = false;

        while (!done)
        {
            std::size_t start = text.find_first_of("{[", search_from);
            if (start == std::string::npos)
            {
                result["error"] = "No valid JSON object or array found in text.";
                done = true;
            }
            else
            {
                std::size_t end = pooriayousefi::find_balanced_end(text, start);
                if (end != std::string::npos)
                {
                    std::string json_str = text.substr(start, end - start + 1);
                    auto parsed_json = pooriayousefi::json::parse(json_str);
                    if (parsed_json)
                    {
                        result["result"] = std::move(*parsed_json);
                        done = true;
                    }
                }
                search_from = start + 1;
            }
        }
    }
    co_return result;
}

AsyncTask<JSON> handle_generate_uuid(const JSON& args)
{
    JSON result{};
    std::string uuid = pooriayousefi::generate_uuid();
    result["result"] = uuid;
    co_return result;
}

// ---- Main entry point -------------------------------------------------------

int main()
{
    int result{EXIT_SUCCESS};

    std::cerr << "utility_server (MCP STDIO) running...\n";

    MCPServer server;

    // Tool 1: tokenize_string
    MCPTool tool1;
    tool1.name = "tokenize_string";
    tool1.description = "Splits a given string into a list of tokens based on specified delimiter characters.";
    tool1.parameters_schema = pooriayousefi::json::parse(R"({"type":"object","properties":{"sentence":{"type":"string","description":"The text to be tokenized."},"delimiters":{"type":"string","description":"A string containing all characters to be treated as delimiters."}},"required":["sentence","delimiters"]})").value_or(JSON{});
    server.register_tool(std::move(tool1), handle_tokenize_string);

    // Tool 2: count_words
    MCPTool tool2;
    tool2.name = "count_words";
    tool2.description = "Counts the exact number of words in a string.";
    tool2.parameters_schema = pooriayousefi::json::parse(R"({"type":"object","properties":{"text":{"type":"string","description":"The text to count words in."}},"required":["text"]})").value_or(JSON{});
    server.register_tool(std::move(tool2), handle_count_words);

    // Tool 3: extract_json_from_text
    MCPTool tool3;
    tool3.name = "extract_json_from_text";
    tool3.description = "Extracts the first valid JSON object or array found within a block of messy text.";
    tool3.parameters_schema = pooriayousefi::json::parse(R"({"type":"object","properties":{"text":{"type":"string","description":"The raw text that may contain a JSON string."}},"required":["text"]})").value_or(JSON{});
    server.register_tool(std::move(tool3), handle_extract_json_from_text);

    // Tool 4: generate_uuid
    MCPTool tool4;
    tool4.name = "generate_uuid";
    tool4.description = "Generates a random, unique UUID v4 string.";
    tool4.parameters_schema = pooriayousefi::json::parse(R"({"type":"object","properties":{},"required":[]})").value_or(JSON{});
    server.register_tool(std::move(tool4), handle_generate_uuid);

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