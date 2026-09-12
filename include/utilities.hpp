// ============================================================================
//  utilities.hpp — TOAR Shared Utilities
//  Developed by: Pooria Yousefi
//  License: Apache 2.0
// ============================================================================
#pragma once

#include <algorithm>
#include <chrono>
#include <ctime>
#include <expected>
#include <format>
#include <fstream>
#include <functional>
#include <iterator>
#include <print>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include "poorijson.hpp"
#include "poorijsonrpc.hpp"

namespace pooriayousefi::toar
{
    // ---- Type aliases (re-export from foundation) ---------------------------

    using URL = std::string;
    using ID = std::string;
    using JSON = pooriayousefi::json::JSON;
    using JSONArray = pooriayousefi::json::JSONArray;
    using JSONObject = pooriayousefi::json::JSONObject;

    namespace rpc = pooriayousefi::json::rpc;

    // ---- Cross-platform datetime --------------------------------------------

    /// @brief Returns the current local date and time as a formatted string.
    /// @return "YYYY-MM-DD HH:MM:SS" or an error message on failure.
    inline std::string get_current_date_time()
    {
        std::string result{};

        auto now = std::chrono::system_clock::now();
        std::time_t tt = std::chrono::system_clock::to_time_t(now);
        struct tm local_time{};

        bool success = false;

#ifdef _WIN32
        if (localtime_s(&local_time, &tt) == 0)
        {
            success = true;
        }
#else
        if (localtime_r(&tt, &local_time) != nullptr)
        {
            success = true;
        }
#endif

        if (success)
        {
            result = std::format("{:04d}-{:02d}-{:02d} {:02d}:{:02d}:{:02d}",
                                 local_time.tm_year + 1900,
                                 local_time.tm_mon + 1,
                                 local_time.tm_mday,
                                 local_time.tm_hour,
                                 local_time.tm_min,
                                 local_time.tm_sec);
        }
        else
        {
            result = "Error: Could not retrieve local time.";
        }

        return result;
    }

    // ---- Logger --------------------------------------------------------------

    /// @brief Per-agent file logger with RAII file management.
    ///
    /// Each agent creates a unique log file named:
    ///   "<agent_id>-<thread_hash>-<datetime>"
    ///
    /// The log method writes a timestamped entry and returns the original
    /// string so the caller can optionally pipe it to stdout.
    struct Logger
    {
        ID id;
        std::ofstream stream;

        explicit Logger(const ID& agent_id)
            : id{agent_id}
            , stream{}
        {
        }

        ~Logger()
        {
            if (stream.is_open())
            {
                stream.flush();
                stream.close();
            }
        }

        Logger(const Logger&) = delete;
        Logger& operator=(const Logger&) = delete;
        Logger(Logger&&) = delete;
        Logger& operator=(Logger&&) = delete;

        /// @brief Opens the log file.  Must be called before log().
        /// @return true if the file was opened successfully.
        bool open_stream()
        {
            bool result = false;

            std::hash<std::thread::id> thread_hasher{};
            auto datetime = get_current_date_time();

            // Replace characters that are invalid in filenames.
            std::replace(datetime.begin(), datetime.end(), ' ', '-');
            std::replace(datetime.begin(), datetime.end(), ':', '-');

            auto filename = std::format("{}-{}-{}",
                                        id,
                                        thread_hasher(std::this_thread::get_id()),
                                        datetime);

            stream.open(filename, std::ios::out);

            if (stream.is_open())
            {
                result = true;
            }
            else
            {
                std::println("Cannot open chronicle file of the agent {}", id);
            }

            return result;
        }

        /// @brief Writes a timestamped entry to the log file.
        /// @param data  the text to log.
        /// @return the original string (for optional stdout piping).
        std::string log(const std::string& data)
        {
            if (stream.is_open())
            {
                stream << std::format("\n\t{}:\n{}", get_current_date_time(), data)
                        << std::endl;
            }
            return data;
        }
    };

    // ---- Server configuration (parsed from tool_servers.json) ---------------

    /// @brief Describes a single MCP server entry from the config file.
    struct ServerConfig
    {
        std::string name{};
        std::string transport{};                   ///< "stdio" or "http"
        std::vector<std::string> command{};        ///< for stdio: ["npx", "-y", ...]
        std::string url{};                         ///< for http: "http://host:port/path"
        bool enabled{false};
        std::string description{};
    };

    // ---- URL parsing --------------------------------------------------------

    /// @brief Parsed components of an HTTP URL.
    struct ParsedURL
    {
        std::string host{};
        int port{80};
        std::string path{"/"};
    };

    /// @brief Parses an HTTP URL into host, port, and path.
    /// @param url  e.g. "http://localhost:8931/mcp"
    /// @return the parsed components.
    [[nodiscard]] inline ParsedURL parse_url(std::string_view url)
    {
        ParsedURL result{};

        // Strip the scheme.
        std::string_view rest = url;
        if (rest.starts_with("http://"))
        {
            rest.remove_prefix(7);
        }
        else if (rest.starts_with("https://"))
        {
            rest.remove_prefix(8);
        }

        // Split host[:port] from path at the first '/'.
        auto path_pos = rest.find('/');
        if (path_pos != std::string_view::npos)
        {
            result.path = std::string{rest.substr(path_pos)};
            rest = rest.substr(0, path_pos);
        }

        // Split host from port at the first ':'.
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

    // ---- Config file parser -------------------------------------------------

    /// @brief Reads and parses a tool_servers.json configuration file.
    ///
    /// Expected format:
    /// @code
    /// {
    ///   "mcp_servers": [
    ///     {
    ///       "name": "filesystem",
    ///       "transport": "stdio",
    ///       "command": ["npx", "-y", "@modelcontextprotocol/server-filesystem"],
    ///       "enabled": true,
    ///       "description": "Filesystem MCP server"
    ///     },
    ///     {
    ///       "name": "playwright",
    ///       "transport": "http",
    ///       "url": "http://localhost:8931/mcp",
    ///       "enabled": true,
    ///       "description": "Playwright MCP server"
    ///     }
    ///   ]
    /// }
    /// @endcode
    ///
    /// @param filename  path to the JSON config file.
    /// @return a vector of ServerConfig on success, or an error message.
    [[nodiscard]] inline std::expected<std::vector<ServerConfig>, std::string>
    parse_server_config(std::string_view filename)
    {
        std::expected<std::vector<ServerConfig>, std::string> result{};

        std::ifstream file{std::string{filename}};
        if (!file.is_open())
        {
            result = std::unexpected(
                std::format("Cannot open config file: {}", std::string{filename})
            );
        }
        else
        {
            std::string content{
                std::istreambuf_iterator<char>{file},
                std::istreambuf_iterator<char>{}
            };

            auto parsed = pooriayousefi::json::parse(content);
            if (!parsed)
            {
                result = std::unexpected("Failed to parse config JSON.");
            }
            else
            {
                JSON& root = *parsed;

                if (!root.contains("mcp_servers") || !root["mcp_servers"].is_array())
                {
                    result = std::unexpected("Config missing 'mcp_servers' array.");
                }
                else
                {
                    std::vector<ServerConfig> servers{};

                    for (const auto& server_json : root["mcp_servers"].get_array())
                    {
                        ServerConfig config{};

                        if (server_json.contains("name") && server_json["name"].is_string())
                        {
                            config.name = server_json["name"].get_string();
                        }

                        if (server_json.contains("transport") && server_json["transport"].is_string())
                        {
                            config.transport = server_json["transport"].get_string();
                        }

                        if (server_json.contains("enabled") && server_json["enabled"].is_boolean())
                        {
                            config.enabled = server_json["enabled"].get_bool();
                        }

                        if (server_json.contains("description") && server_json["description"].is_string())
                        {
                            config.description = server_json["description"].get_string();
                        }

                        if (server_json.contains("url") && server_json["url"].is_string())
                        {
                            config.url = server_json["url"].get_string();
                        }

                        if (server_json.contains("command") && server_json["command"].is_array())
                        {
                            for (const auto& cmd : server_json["command"].get_array())
                            {
                                if (cmd.is_string())
                                {
                                    config.command.push_back(cmd.get_string());
                                }
                            }
                        }

                        servers.push_back(std::move(config));
                    }

                    result = std::move(servers);
                }
            }
        }

        return result;
    }
}