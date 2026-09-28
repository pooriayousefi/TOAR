// ============================================================================
//  mcp_web_fetch_server.cpp — MCP STDIO Server: Web Fetch & Research
//  Developed by: Pooria Yousefi
//  License: Apache 2.0
//
//  A standalone MCP server that fetches URLs and extracts clean text or JSON.
//  Communicates via newline-delimited JSON-RPC 2.0 over stdin/stdout.
//
//  Build: clang++ -std=c++23 -O3 -I include src/mcp_web_fetch_server.cpp -o bin/mcp_web_fetch_server
// ============================================================================
#include "poorimcp.hpp"

#include <algorithm>
#include <cctype>
#include <iostream>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#ifdef _WIN32
#  define WIN32_LEAN_AND_MEAN
#  define NOMINMAX
#  include <winsock2.h>
#  include <ws2tcpip.h>
#  pragma comment(lib, "ws2_32.lib")
#else
#  include <sys/socket.h>
#  include <netinet/in.h>
#  include <unistd.h>
#  include <arpa/inet.h>
#  include <netdb.h>
#endif

using namespace pooriayousefi::mcp;
using namespace pooriayousefi::core;
using namespace pooriayousefi::json;

namespace pooriayousefi
{
    struct ParsedUrl
    {
        std::string host;
        int port;
        std::string path;
    };

    ParsedUrl parse_url(std::string_view url)
    {
        ParsedUrl result{"", 80, "/"};
        std::string_view rest = url;

        if (rest.starts_with("http://"))
        {
            rest.remove_prefix(7);
        }
        else if (rest.starts_with("https://"))
        {
            rest.remove_prefix(8);
            result.port = 443; // Note: Native socket does not support TLS. We will attempt port 443 but it will fail unless plain HTTP. We leave default 80 if not specified.
        }

        auto path_pos = rest.find('/');
        if (path_pos != std::string_view::npos)
        {
            result.path = std::string{rest.substr(path_pos)};
            rest = rest.substr(0, path_pos);
        }

        auto port_pos = rest.find(':');
        if (port_pos != std::string_view::npos)
        {
            result.host = std::string{rest.substr(0, port_pos)};
            std::string_view port_sv = rest.substr(port_pos + 1);
            int port = 0;
            for (char c : port_sv)
            {
                if (c >= '0' && c <= '9')
                {
                    port = port * 10 + (c - '0');
                }
            }
            result.port = port;
        }
        else
        {
            result.host = std::string{rest};
        }

        return result;
    }

    std::string to_lower(std::string s)
    {
        std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c)
        {
            return static_cast<char>(std::tolower(c));
        });
        return s;
    }

    std::string url_decode(std::string s)
    {
        std::string result;
        for (size_t i = 0; i < s.length(); ++i)
        {
            if (s[i] == '%' && i + 2 < s.length())
            {
                std::string hex = s.substr(i + 1, 2);
                char c = static_cast<char>(std::stoi(hex, nullptr, 16));
                result += c;
                i += 2;
            }
            else if (s[i] == '+')
            {
                result += ' ';
            }
            else
            {
                result += s[i];
            }
        }
        return result;
    }

    // Fetches raw HTTP response body, following up to 5 redirects.
    std::expected<std::string, std::string> fetch_raw_url(const std::string& url, int max_redirects = 5)
    {
        std::string current_url = url;
        int redirects = 0;

        while (redirects <= max_redirects)
        {
            ParsedUrl parsed = parse_url(current_url);
            if (parsed.host.empty())
            {
                return std::unexpected("Invalid URL");
            }

            struct addrinfo hints{};
            hints.ai_family = AF_UNSPEC;
            hints.ai_socktype = SOCK_STREAM;
            struct addrinfo* addr_result = nullptr;

            if (getaddrinfo(parsed.host.c_str(), std::to_string(parsed.port).c_str(), &hints, &addr_result) != 0 || !addr_result)
            {
                return std::unexpected("Failed to resolve host: " + parsed.host);
            }

            auto deleter = [](addrinfo* p) { if (p) freeaddrinfo(p); };
            std::unique_ptr<addrinfo, decltype(deleter)> addr_guard(addr_result, deleter);

            int sock = -1;
            for (struct addrinfo* rp = addr_result; rp != nullptr; rp = rp->ai_next)
            {
                sock = ::socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
                if (sock < 0) continue;

                if (::connect(sock, rp->ai_addr, static_cast<int>(rp->ai_addrlen)) == 0)
                {
                    break; // Success
                }

#ifdef _WIN32
                closesocket(sock);
#else
                ::close(sock);
#endif
                sock = -1;
            }

            if (sock < 0)
            {
                return std::unexpected("Failed to connect to " + parsed.host + ":" + std::to_string(parsed.port));
            }

            std::string request = "GET " + parsed.path + " HTTP/1.1\r\n";
            request += "Host: " + parsed.host + "\r\n";
            request += "User-Agent: TOAR-MCP/1.0\r\n";
            request += "Accept: text/html, application/json, */*\r\n";
            request += "Connection: close\r\n\r\n";

            std::size_t total_sent = 0;
            while (total_sent < request.size())
            {
                int n = ::send(sock, request.data() + total_sent, static_cast<int>(request.size() - total_sent), 0);
                if (n <= 0)
                {
#ifdef _WIN32
                    closesocket(sock);
#else
                    ::close(sock);
#endif
                    return std::unexpected("Failed to send HTTP request");
                }
                total_sent += n;
            }

            std::string raw;
            char chunk[4096];
            while (true)
            {
                int n = ::recv(sock, chunk, sizeof(chunk), 0);
                if (n <= 0) break;
                raw.append(chunk, n);
            }

#ifdef _WIN32
            closesocket(sock);
#else
            ::close(sock);
#endif

            auto header_end = raw.find("\r\n\r\n");
            if (header_end == std::string::npos)
            {
                return std::unexpected("Invalid HTTP response (no header end)");
            }

            std::string headers = to_lower(raw.substr(0, header_end));
            std::string body = raw.substr(header_end + 4);

            // Check for redirect
            if (headers.find("http/1.1 301") != std::string::npos ||
                headers.find("http/1.1 302") != std::string::npos ||
                headers.find("http/1.1 307") != std::string::npos ||
                headers.find("http/1.0 301") != std::string::npos ||
                headers.find("http/1.0 302") != std::string::npos)
            {
                auto loc_pos = headers.find("location:");
                if (loc_pos != std::string::npos)
                {
                    auto line_end = headers.find("\r\n", loc_pos);
                    std::string location = raw.substr(loc_pos + 9, line_end - (loc_pos + 9));
                    // Trim whitespace
                    location.erase(location.find_last_not_of(" \t\r\n") + 1);
                    location.erase(0, location.find_first_not_of(" \t"));

                    if (location.starts_with("http://") || location.starts_with("https://"))
                    {
                        current_url = location;
                    }
                    else if (location.starts_with("/"))
                    {
                        current_url = "http://" + parsed.host + ":" + std::to_string(parsed.port) + location;
                    }
                    else
                    {
                        current_url = "http://" + parsed.host + ":" + std::to_string(parsed.port) + "/" + location;
                    }
                    redirects++;
                    continue;
                }
            }

            return body;
        }

        return std::unexpected("Too many redirects");
    }

    // Strips HTML tags and decodes basic entities to produce clean text
    std::string html_to_text(const std::string& html)
    {
        std::string text;
        bool in_tag = false;
        bool in_script = false;
        bool in_style = false;

        for (size_t i = 0; i < html.length(); ++i)
        {
            char c = html[i];

            // Check for script/style blocks
            if (!in_tag && i + 7 < html.length() && to_lower(html.substr(i, 7)) == "<script")
            {
                in_script = true;
                continue;
            }
            if (in_script && i + 8 < html.length() && to_lower(html.substr(i, 8)) == "</script")
            {
                in_script = false;
                i += 8; // Skip the tag start
                while (i < html.length() && html[i] != '>') i++; // Skip rest of tag
                continue;
            }
            if (!in_tag && i + 6 < html.length() && to_lower(html.substr(i, 6)) == "<style")
            {
                in_style = true;
                continue;
            }
            if (in_style && i + 7 < html.length() && to_lower(html.substr(i, 7)) == "</style")
            {
                in_style = false;
                i += 7;
                while (i < html.length() && html[i] != '>') i++;
                continue;
            }

            if (in_script || in_style)
            {
                continue;
            }

            if (c == '<')
            {
                in_tag = true;
                // Add newline for block tags
                if (i + 4 < html.length() && 
                    (to_lower(html.substr(i, 4)) == "<br>" || to_lower(html.substr(i, 4)) == "</p>" || 
                     to_lower(html.substr(i, 4)) == "</d" || to_lower(html.substr(i, 4)) == "</h" ||
                     to_lower(html.substr(i, 4)) == "</l"))
                {
                    text += "\n";
                }
                continue;
            }
            if (c == '>')
            {
                in_tag = false;
                continue;
            }
            if (in_tag)
            {
                continue;
            }

            // Decode basic HTML entities
            if (c == '&')
            {
                if (html.compare(i, 6, "&amp;") == 0)
                {
                    text += '&';
                    i += 5;
                }
                else if (html.compare(i, 4, "&lt;") == 0)
                {
                    text += '<';
                    i += 3;
                }
                else if (html.compare(i, 4, "&gt;") == 0)
                {
                    text += '>';
                    i += 3;
                }
                else if (html.compare(i, 6, "&quot;") == 0)
                {
                    text += '"';
                    i += 5;
                }
                else if (html.compare(i, 6, "&#39;") == 0)
                {
                    text += '\'';
                    i += 5;
                }
                else
                {
                    text += c;
                }
            }
            else
            {
                text += c;
            }
        }

        // Collapse multiple whitespaces and newlines
        std::string cleaned;
        cleaned.reserve(text.size());
        bool prev_space = false;
        for (char ch : text)
        {
            if (std::isspace(static_cast<unsigned char>(ch)))
            {
                if (!prev_space)
                {
                    cleaned += ' ';
                    prev_space = true;
                }
            }
            else
            {
                cleaned += ch;
                prev_space = false;
            }
        }
        return cleaned;
    }
}

// ---- Web Fetch Tool Handlers ------------------------------------------------

AsyncTask<JSON> handle_fetch_url(const JSON& args)
{
    JSON result{};
    if (!args.contains("url") || !args["url"].is_string())
    {
        result["error"] = "Missing or non-string 'url' in arguments.";
    }
    else
    {
        std::string url = args["url"].get_string();
        int max_length = 8000;
        if (args.contains("max_length") && args["max_length"].is_number())
        {
            max_length = static_cast<int>(args["max_length"].get_number());
        }

        auto fetch_res = pooriayousefi::fetch_raw_url(url);
        if (!fetch_res)
        {
            result["error"] = fetch_res.error();
        }
        else
        {
            std::string text = pooriayousefi::html_to_text(*fetch_res);
            if (static_cast<int>(text.size()) > max_length)
            {
                text = text.substr(0, max_length) + "\n...[Content truncated at " + std::to_string(max_length) + " chars]...";
            }
            result["result"] = text;
        }
    }
    co_return result;
}

AsyncTask<JSON> handle_download_json(const JSON& args)
{
    JSON result{};
    if (!args.contains("url") || !args["url"].is_string())
    {
        result["error"] = "Missing or non-string 'url' in arguments.";
    }
    else
    {
        std::string url = args["url"].get_string();
        auto fetch_res = pooriayousefi::fetch_raw_url(url);
        if (!fetch_res)
        {
            result["error"] = fetch_res.error();
        }
        else
        {
            auto parsed = pooriayousefi::json::parse(*fetch_res);
            if (!parsed)
            {
                result["error"] = "Failed to parse JSON from URL. Raw content: " + fetch_res->substr(0, 500);
            }
            else
            {
                result["result"] = std::move(*parsed);
            }
        }
    }
    co_return result;
}

// ---- Main entry point -------------------------------------------------------

int main()
{
    int result{EXIT_SUCCESS};

    std::cerr << "web_fetch_server (MCP STDIO) running...\n";

    MCPServer server;

    // Tool 1: fetch_url
    MCPTool tool1;
    tool1.name = "fetch_url";
    tool1.description = "Fetches a URL, downloads the HTML, strips all tags and scripts, and returns clean plain text. Ideal for reading web pages or documentation. Truncates to 8000 chars by default.";
    tool1.parameters_schema = pooriayousefi::json::parse(R"({"type":"object","properties":{"url":{"type":"string","description":"The absolute URL to fetch (must start with http://)."},"max_length":{"type":"integer","description":"Maximum number of characters to return. Defaults to 8000."}},"required":["url"]})").value_or(JSON{});
    server.register_tool(std::move(tool1), handle_fetch_url);

    // Tool 2: download_json
    MCPTool tool2;
    tool2.name = "download_json";
    tool2.description = "Fetches a URL and parses the response body as JSON. Ideal for interacting with public REST APIs.";
    tool2.parameters_schema = pooriayousefi::json::parse(R"({"type":"object","properties":{"url":{"type":"string","description":"The absolute URL to fetch (must start with http://)."}},"required":["url"]})").value_or(JSON{});
    server.register_tool(std::move(tool2), handle_download_json);

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