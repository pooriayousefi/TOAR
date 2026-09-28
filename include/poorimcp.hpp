// ============================================================================
//  poorimcp.hpp — Async Model Context Protocol (Official Spec)
//  Developed by: Pooria Yousefi
//  License: Apache 2.0
// ============================================================================
#pragma once

#include "asyncore.hpp"
#include "io_thread_pool.hpp"
#include "poorijson.hpp"
#include "poorijsonrpc.hpp"
#include "process.hpp"

#include <string>
#include <vector>
#include <unordered_map>
#include <iostream>
#include <functional>
#include <memory>
#include <optional>
#include <expected>
#include <variant>
#include <span>
#include <system_error>
#include <cstdint>
#include <thread>
#include <atomic>

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

namespace pooriayousefi::mcp
{
    using namespace core;
    using namespace io_bound;
    using namespace json;
    using namespace process;
    using namespace rpc;

    namespace detail
    {
#ifdef _WIN32
        struct WinsockInitializer
        {
            WinsockInitializer()
            {
                WSADATA data;
                WSAStartup(MAKEWORD(2, 2), &data);
            }
            ~WinsockInitializer()
            {
                WSACleanup();
            }
            WinsockInitializer(const WinsockInitializer&) = delete;
            WinsockInitializer& operator=(const WinsockInitializer&) = delete;
        };
        using socket_type = SOCKET;
        constexpr socket_type invalid_socket = INVALID_SOCKET;
        inline void close_socket(socket_type s) { closesocket(s); }
#else
        using socket_type = int;
        constexpr socket_type invalid_socket = -1;
        inline void close_socket(socket_type s) { ::close(s); }
#endif
    }

    struct MCPTool
    {
        std::string name;
        std::string description;
        JSON parameters_schema;
    };

    class ToolHandler
    {
        struct Base
        {
            virtual ~Base() = default;
            virtual AsyncTask<JSON> invoke(const JSON& args) = 0;
        };

        template <typename F>
        struct Model : Base
        {
            F func;
            explicit Model(F&& f) : func(std::move(f)) {}
            AsyncTask<JSON> invoke(const JSON& args) override { return func(args); }
        };

        std::unique_ptr<Base> impl_;

    public:
        ToolHandler() : impl_{nullptr} {}
        template <typename F>
            requires(!std::is_same_v<std::decay_t<F>, ToolHandler>)
        ToolHandler(F&& f) : impl_{std::make_unique<Model<std::decay_t<F>>>(std::forward<F>(f))} {}

        ToolHandler(ToolHandler&&) noexcept = default;
        ToolHandler& operator=(ToolHandler&&) noexcept = default;
        ToolHandler(const ToolHandler&) = delete;
        ToolHandler& operator=(const ToolHandler&) = delete;

        AsyncTask<JSON> operator()(const JSON& args)
        {
            if (impl_)
            {
                return impl_->invoke(args);
            }
            auto fallback = []() -> AsyncTask<JSON>
            {
                JSON err;
                err["error"] = "empty tool handler";
                co_return err;
            };
            return fallback();
        }
    };

    class MCPTransport
    {
    public:
        enum class Type { STDIO, HTTP };
    private:
        Type type_;
        std::string executable_;
        std::vector<std::string> args_;
        std::string host_;
        int port_;
        std::string path_;

    public:
        MCPTransport() : type_{Type::STDIO}, port_{80}, path_{"/mcp"} {}
        MCPTransport(MCPTransport&&) noexcept = default;
        MCPTransport& operator=(MCPTransport&&) noexcept = default;
        MCPTransport(const MCPTransport&) = delete;
        MCPTransport& operator=(const MCPTransport&) = delete;

        static MCPTransport create_stdio(std::string executable, std::vector<std::string> args)
        {
            MCPTransport t;
            t.type_ = Type::STDIO;
            t.executable_ = std::move(executable);
            t.args_ = std::move(args);
            return t;
        }

        static MCPTransport create_http(std::string host, int port, std::string path = "/mcp")
        {
            MCPTransport t;
            t.type_ = Type::HTTP;
            t.host_ = std::move(host);
            t.port_ = port;
            t.path_ = std::move(path);
            return t;
        }

        [[nodiscard]] bool is_http() const noexcept { return type_ == Type::HTTP; }
        [[nodiscard]] bool is_stdio() const noexcept { return type_ == Type::STDIO; }
        [[nodiscard]] const std::string& get_executable() const noexcept { return executable_; }
        [[nodiscard]] const std::vector<std::string>& get_args() const noexcept { return args_; }
        [[nodiscard]] const std::string& get_host() const noexcept { return host_; }
        [[nodiscard]] int get_port() const noexcept { return port_; }
        [[nodiscard]] const std::string& get_path() const noexcept { return path_; }
    };

    class MCPServer
    {
    public:
        enum class Type { STDIO, HTTP };
        using ToolMap = std::unordered_map<std::string, std::pair<MCPTool, ToolHandler>, JSONStringHash, JSONStringEqual>;

    private:
        Type type_;
        int port_;
        ToolMap tools_;
        CancellationToken token_;
#ifdef _WIN32
        detail::WinsockInitializer winsock_init_;
#endif

    public:
        MCPServer() : type_{Type::STDIO}, port_{-1} {}
        explicit MCPServer(int port) : type_{Type::HTTP}, port_{port} {}
        ~MCPServer() { stop(); }
        MCPServer(const MCPServer&) = delete;
        MCPServer& operator=(const MCPServer&) = delete;
        MCPServer(MCPServer&&) = delete;
        MCPServer& operator=(MCPServer&&) = delete;

        void register_tool(MCPTool tool, ToolHandler handler)
        {
            std::string name = tool.name;
            tools_.try_emplace(std::move(name), std::move(tool), std::move(handler));
        }

        void stop() noexcept { token_.cancel(); }

        AsyncTask<void> start()
        {
            if (type_ == Type::STDIO) { co_await start_stdio(); }
            else { co_await start_http(); }
            co_return;
        }

    private:
        AsyncTask<void> start_stdio()
        {
            std::string line;
            while (!token_.is_cancelled() && std::getline(std::cin, line))
            {
                if (!line.empty() && line.back() == '\r') { line.pop_back(); }
                if (line.empty()) { continue; }

                auto parsed_req = parse(line);
                if (parsed_req)
                {
                    JSON resp = co_await handle_rpc(*parsed_req);
                    std::cout << resp.dump() << "\n" << std::flush;
                }
            }
            co_return;
        }

        AsyncTask<void> start_http()
        {
            detail::socket_type listen_fd = ::socket(AF_INET, SOCK_STREAM, 0);
            if (listen_fd == detail::invalid_socket) { std::cerr << "Failed to create socket\n"; co_return; }

            int opt = 1;
            setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&opt), sizeof(opt));

            struct sockaddr_in address{};
            address.sin_family = AF_INET;
            address.sin_addr.s_addr = INADDR_ANY;
            address.sin_port = htons(static_cast<uint16_t>(port_));

            if (::bind(listen_fd, reinterpret_cast<struct sockaddr*>(&address), sizeof(address)) < 0)
            {
                std::cerr << "Failed to bind to port " << port_ << "\n";
                detail::close_socket(listen_fd);
                co_return;
            }

            if (::listen(listen_fd, 3) < 0)
            {
                std::cerr << "Failed to listen on socket\n";
                detail::close_socket(listen_fd);
                co_return;
            }

            std::cout << "MCP Server (HTTP) listening on port " << port_ << "...\n";

            while (!token_.is_cancelled())
            {
                struct sockaddr_in client_addr{};
                socklen_t addrlen = sizeof(client_addr);
                detail::socket_type client_fd = ::accept(listen_fd, reinterpret_cast<struct sockaddr*>(&client_addr), &addrlen);

                if (client_fd == detail::invalid_socket) { continue; }

                std::thread([this, client_fd]() -> void {
                    handle_http_client_blocking(client_fd);
                }).detach();
            }

            detail::close_socket(listen_fd);
            co_return;
        }

        void handle_http_client_blocking(detail::socket_type client_fd)
        {
            std::string buffer;
            char chunk[4096];
            bool request_complete = false;

            while (!request_complete)
            {
                int n = ::recv(client_fd, chunk, sizeof(chunk), 0);
                if (n <= 0) { request_complete = true; }
                else
                {
                    buffer.append(chunk, n);
                    auto header_end = buffer.find("\r\n\r\n");
                    if (header_end != std::string::npos)
                    {
                        std::string_view headers{buffer.data(), header_end};
                        auto cl_pos = headers.find("Content-Length:");
                        if (cl_pos == std::string_view::npos) { cl_pos = headers.find("content-length:"); }

                        if (cl_pos != std::string_view::npos)
                        {
                            std::size_t value_start = cl_pos + 16;
                            while (value_start < headers.size() && (headers[value_start] == ' ' || headers[value_start] == '\t')) { value_start++; }
                            auto line_end = headers.find("\r\n", cl_pos);
                            if (line_end == std::string_view::npos) { line_end = headers.size(); }
                            
                            std::string cl_str{headers.substr(value_start, line_end - value_start)};
                            std::size_t content_length = 0;
                            for (char c : cl_str) { if (c >= '0' && c <= '9') { content_length = content_length * 10 + (c - '0'); } }

                            std::size_t body_start = header_end + 4;
                            if (buffer.size() >= body_start + content_length) { request_complete = true; }
                        }
                        else { request_complete = true; }
                    }
                }
            }

            auto header_end = buffer.find("\r\n\r\n");
            if (header_end != std::string::npos)
            {
                std::string_view json_sv{buffer.data() + header_end + 4};
                auto parsed_req = parse(json_sv);
                if (parsed_req)
                {
                    JSON resp = sync_wait(handle_rpc(*parsed_req));
                    std::string body = resp.dump();

                    std::string http_resp;
                    http_resp += "HTTP/1.1 200 OK\r\n";
                    http_resp += "Content-Type: application/json\r\n";
                    http_resp += "Content-Length: " + std::to_string(body.size()) + "\r\n";
                    http_resp += "Connection: close\r\n\r\n";
                    http_resp += body;

                    ::send(client_fd, http_resp.data(), static_cast<int>(http_resp.size()), 0);
                }
            }
            detail::close_socket(client_fd);
        }

        AsyncTask<JSON> handle_rpc(const JSON& req)
        {
            JSON resp{};
            if (!req.contains("id"))
            {
                resp = make_error(ErrorCode::INVALID_REQUEST, "Invalid Request: missing id", JSON(nullptr));
            }
            else
            {
                if (!req.contains("method"))
                {
                    resp = make_error(ErrorCode::INVALID_REQUEST, "Missing method", req["id"]);
                }
                else
                {
                    std::string method = req["method"].get_string();
                    if (method == "initialize")
                    {
                        JSON server_info; server_info["name"] = "poorimcp-server"; server_info["version"] = "1.0.0";
                        JSON caps; caps["tools"] = JSONObject{};
                        JSON result_obj; result_obj["protocolVersion"] = "2026-07-24"; result_obj["serverInfo"] = server_info; result_obj["capabilities"] = caps;
                        resp = make_response(std::move(result_obj), req["id"]);
                    }
                    else if (method == "tools/list")
                    {
                        JSONArray tools_arr;
                        for (const auto& [name, pair] : tools_)
                        {
                            JSON tool_json;
                            tool_json["name"] = pair.first.name;
                            tool_json["description"] = pair.first.description;
                            tool_json["inputSchema"] = pair.first.parameters_schema;
                            tools_arr.push_back(tool_json);
                        }
                        JSON result_obj; result_obj["tools"] = std::move(tools_arr);
                        resp = make_response(std::move(result_obj), req["id"]);
                    }
                    else if (method == "tools/call")
                    {
                        if (!req.contains("params") || !req["params"].contains("name"))
                        {
                            resp = make_error(ErrorCode::INVALID_PARAMS, "Missing tool name", req["id"]);
                        }
                        else
                        {
                            std::string tool_name = req["params"]["name"].get_string();
                            JSON args = req["params"].contains("arguments") ? req["params"]["arguments"] : JSON(nullptr);

                            auto it = tools_.find(tool_name);
                            if (it == tools_.end())
                            {
                                resp = make_error(ErrorCode::METHOD_NOT_FOUND, "Tool not found", req["id"]);
                            }
                            else
                            {
                                try
                                {
                                    JSON tool_result = co_await it->second.second(args);
                                    JSON text_content; text_content["type"] = "text"; text_content["text"] = tool_result.dump();
                                    JSONArray content_arr; content_arr.push_back(text_content);
                                    JSON result_obj; result_obj["content"] = std::move(content_arr);
                                    resp = make_response(std::move(result_obj), req["id"]);
                                }
                                catch (const std::exception& e)
                                {
                                    resp = make_error(ErrorCode::INTERNAL_ERROR, e.what(), req["id"]);
                                }
                            }
                        }
                    }
                    else
                    {
                        resp = make_error(ErrorCode::METHOD_NOT_FOUND, "Method not found", req["id"]);
                    }
                }
            }
            co_return resp;
        }
    };

    class MCPClient
    {
        MCPTransport transport_;
        std::reference_wrapper<ThreadPool> pool_;
        int request_id_;
        std::vector<MCPTool> discovered_tools_;
        std::unique_ptr<Process> process_;
#ifdef _WIN32
        detail::WinsockInitializer winsock_init_;
#endif

    public:
        MCPClient(MCPTransport transport, std::reference_wrapper<ThreadPool> pool)
            : transport_{std::move(transport)}, pool_{pool}, request_id_{0}, process_{nullptr}
#ifdef _WIN32
            , winsock_init_{}
#endif
        {}

        ~MCPClient()
        {
            if (process_)
            {
                process_->close_stdin();
                process_->terminate();
            }
        }

        MCPClient(const MCPClient&) = delete;
        MCPClient& operator=(const MCPClient&) = delete;
        MCPClient(MCPClient&&) = default;
        MCPClient& operator=(MCPClient&&) = default;

        AsyncTask<std::expected<void, std::error_code>> connect_async()
        {
            std::expected<void, std::error_code> result{};
            try
            {
                if (transport_.is_stdio())
                {
                    process_ = std::make_unique<Process>();
                    process_->start(transport_.get_executable(), transport_.get_args());
                    result = {};
                }
                else if (transport_.is_http())
                {
                    result = {};
                }
                else
                {
                    result = std::unexpected(std::make_error_code(std::errc::not_supported));
                }
            }
            catch (const std::exception&)
            {
                result = std::unexpected(std::make_error_code(std::errc::no_such_process));
            }
            co_return result;
        }

        AsyncTask<std::expected<void, std::string>> initialize()
        {
            std::expected<void, std::string> result{};
            JSON init_params;
            init_params["protocolVersion"] = "2026-07-24";
            init_params["capabilities"] = JSONObject{};
            init_params["clientInfo"]["name"] = "poorimcp-client";
            init_params["clientInfo"]["version"] = "1.0.0";
            JSON init_req = make_request("initialize", init_params, request_id_++);

            auto init_resp = co_await send_rpc_async(init_req);
            if (!init_resp)
            {
                result = std::unexpected(init_resp.error());
            }
            else
            {
                JSON list_req = make_request("tools/list", JSON(nullptr), request_id_++);
                auto list_resp = co_await send_rpc_async(list_req);
                if (!list_resp)
                {
                    result = std::unexpected(list_resp.error());
                }
                else
                {
                    if (list_resp->contains("tools") && (*list_resp)["tools"].is_array())
                    {
                        for (const auto& tool_json : (*list_resp)["tools"].get_array())
                        {
                            MCPTool t;
                            t.name = tool_json["name"].get_string();
                            t.description = tool_json["description"].get_string();
                            t.parameters_schema = tool_json["inputSchema"];
                            discovered_tools_.push_back(t);
                        }
                    }
                    result = {};
                }
            }
            co_return result;
        }

        [[nodiscard]] const std::vector<MCPTool>& get_tools() const noexcept { return discovered_tools_; }

        AsyncTask<std::expected<std::string, std::string>> call_tool_async(const std::string& tool_name, const JSON& args)
        {
            std::expected<std::string, std::string> result{};
            JSON call_params;
            call_params["name"] = tool_name;
            call_params["arguments"] = args;
            JSON req = make_request("tools/call", call_params, request_id_++);

            auto resp = co_await send_rpc_async(req);
            if (!resp)
            {
                result = std::unexpected(resp.error());
            }
            else
            {
                if (resp->contains("content") && (*resp)["content"].is_array())
                {
                    const auto& content = (*resp)["content"].get_array();
                    if (!content.empty() && content[0].contains("text"))
                    {
                        result = content[0]["text"].get_string();
                    }
                    else
                    {
                        result = resp->dump();
                    }
                }
                else
                {
                    result = resp->dump();
                }
            }
            co_return result;
        }

    private:
        AsyncTask<std::expected<JSON, std::string>> send_rpc_async(const JSON& request)
        {
            std::expected<JSON, std::string> result{};
            if (transport_.is_http())
            {
                std::string body = request.dump();
                auto http_res = co_await pool_.get().run_blocking(
                    [this, &body]() -> std::expected<std::string, std::error_code> { return send_http_blocking(body); }
                );

                if (!http_res)
                {
                    result = std::unexpected(http_res.error().message());
                }
                else
                {
                    auto parsed = parse(*http_res);
                    if (!parsed) { result = std::unexpected("Invalid JSON from HTTP server"); }
                    else if (parsed->contains("error") && (*parsed)["error"].contains("message")) { result = std::unexpected((*parsed)["error"]["message"].get_string()); }
                    else { result = (*parsed)["result"]; }
                }
            }
            else if (transport_.is_stdio())
            {
                if (!process_)
                {
                    result = std::unexpected("Process is not initialized");
                }
                else
                {
                    std::string req_str = request.dump() + "\n";
                    auto send_res = co_await pool_.get().run_blocking(
                        [this, &req_str]() -> std::expected<std::size_t, std::error_code> { return process_->write_stdin(std::as_bytes(std::span{req_str})); }
                    );

                    if (!send_res)
                    {
                        result = std::unexpected("Failed to send RPC request to subprocess");
                    }
                    else
                    {
                        auto recv_res = co_await pool_.get().run_blocking(
                            [this]() -> std::expected<std::string, std::error_code>
                            {
                                std::string buf;
                                char chunk[4096];
                                bool done = false;
                                while (!done)
                                {
                                    auto r = process_->read_stdout(std::as_writable_bytes(std::span{chunk}));
                                    if (!r) { return std::unexpected(r.error()); }
                                    if (*r == 0) { done = true; }
                                    else
                                    {
                                        buf.append(chunk, *r);
                                        if (buf.find('\n') != std::string::npos) { done = true; }
                                    }
                                }
                                return buf;
                            }
                        );

                        if (!recv_res)
                        {
                            result = std::unexpected("Failed to read RPC response from subprocess");
                        }
                        else
                        {
                            std::string& buffer = *recv_res;
                            if (buffer.empty())
                            {
                                result = std::unexpected("MCP process closed connection (no response)");
                            }
                            else
                            {
                                auto newline_pos = buffer.find('\n');
                                std::string_view json_sv{};
                                if (newline_pos != std::string::npos) { json_sv = std::string_view{buffer.data(), newline_pos}; }
                                else { json_sv = std::string_view{buffer.data(), buffer.size()}; }

                                if (!json_sv.empty() && json_sv.back() == '\r') { json_sv.remove_suffix(1); }

                                auto parsed = parse(json_sv);
                                if (!parsed) { result = std::unexpected("Invalid JSON from MCP Server"); }
                                else if (parsed->contains("error") && (*parsed)["error"].contains("message")) { result = std::unexpected((*parsed)["error"]["message"].get_string()); }
                                else { result = (*parsed)["result"]; }
                            }
                        }
                    }
                }
            }
            else
            {
                result = std::unexpected("Unsupported transport");
            }
            co_return result;
        }

        std::expected<std::string, std::error_code> send_http_blocking(const std::string& body)
        {
            std::expected<std::string, std::error_code> result{};
            struct addrinfo hints{};
            hints.ai_family = AF_UNSPEC;
            hints.ai_socktype = SOCK_STREAM;
            struct addrinfo* addr_result = nullptr;

            if (getaddrinfo(transport_.get_host().c_str(), std::to_string(transport_.get_port()).c_str(), &hints, &addr_result) != 0 || !addr_result)
            {
                result = std::unexpected(std::make_error_code(std::errc::address_not_available));
            }
            else
            {
                auto deleter = [](addrinfo* p) { if (p) { freeaddrinfo(p); } };
                std::unique_ptr<addrinfo, decltype(deleter)> addr_guard(addr_result, deleter);

                detail::socket_type sock = -1;
                for (struct addrinfo* rp = addr_result; rp != nullptr; rp = rp->ai_next)
                {
                    sock = ::socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
                    if (sock == detail::invalid_socket) { continue; }

                    if (::connect(sock, rp->ai_addr, static_cast<int>(rp->ai_addrlen)) == 0)
                    {
                        break; // Success
                    }

                    detail::close_socket(sock);
                    sock = -1;
                }

                if (sock < 0)
                {
                    result = std::unexpected(std::make_error_code(std::errc::connection_refused));
                }
                else
                {
                    std::string request;
                    request.reserve(256 + body.size());
                    request += "POST ";
                    request += transport_.get_path();
                    request += " HTTP/1.1\r\nHost: ";
                    request += transport_.get_host();
                    request += "\r\nContent-Type: application/json\r\nAccept: application/json, text/event-stream\r\nContent-Length: ";
                    request += std::to_string(body.size());
                    request += "\r\nConnection: close\r\n\r\n";
                    request += body;

                    std::size_t total_sent = 0;
                    bool send_error = false;
                    while (total_sent < request.size())
                    {
                        int n = ::send(sock, request.data() + total_sent, static_cast<int>(request.size() - total_sent), 0);
                        if (n <= 0) { send_error = true; break; }
                        total_sent += n;
                    }

                    if (send_error)
                    {
                        detail::close_socket(sock);
                        result = std::unexpected(std::make_error_code(std::errc::broken_pipe));
                    }
                    else
                    {
                        std::string raw;
                        char chunk[4096];
                        bool is_sse = false;
                        bool headers_parsed = false;
                        std::string body_accumulator;

                        while (true)
                        {
                            int n = ::recv(sock, chunk, sizeof(chunk), 0);
                            if (n < 0) { break; } // Error
                            if (n == 0) { break; } // EOF

                            raw.append(chunk, n);

                            if (!headers_parsed)
                            {
                                auto header_end = raw.find("\r\n\r\n");
                                if (header_end != std::string::npos)
                                {
                                    headers_parsed = true;
                                    std::string headers = raw.substr(0, header_end);
                                    if (headers.find("text/event-stream") != std::string::npos)
                                    {
                                        is_sse = true;
                                    }
                                    body_accumulator = raw.substr(header_end + 4);
                                }
                            }
                            else
                            {
                                body_accumulator.append(chunk, n);
                            }

                            if (is_sse && headers_parsed)
                            {
                                // Parse SSE stream to find the JSON-RPC response
                                std::size_t data_pos = body_accumulator.find("data: ");
                                while (data_pos != std::string::npos)
                                {
                                    std::size_t line_end = body_accumulator.find("\n", data_pos);
                                    if (line_end == std::string::npos) { break; } // Incomplete line, wait for more data

                                    std::string data_str = body_accumulator.substr(data_pos + 6, line_end - (data_pos + 6));
                                    if (!data_str.empty() && data_str.back() == '\r') { data_str.pop_back(); }

                                    if (!data_str.empty())
                                    {
                                        auto parsed = parse(data_str);
                                        if (parsed && (parsed->contains("jsonrpc") || parsed->contains("result") || parsed->contains("error")))
                                        {
                                            detail::close_socket(sock);
                                            return data_str; // Found the MCP JSON-RPC response!
                                        }
                                    }
                                    body_accumulator.erase(0, line_end + 1);
                                    data_pos = body_accumulator.find("data: ");
                                }
                            }
                        }

                        detail::close_socket(sock);

                        if (!headers_parsed)
                        {
                            result = std::unexpected(std::make_error_code(std::errc::bad_message));
                        }
                        else if (is_sse)
                        {
                            result = std::unexpected(std::make_error_code(std::errc::bad_message)); // Didn't find JSON-RPC in SSE
                        }
                        else
                        {
                            auto header_end = raw.find("\r\n\r\n");
                            if (header_end == std::string::npos) { result = std::unexpected(std::make_error_code(std::errc::bad_message)); }
                            else
                            {
                                std::string status_line = raw.substr(0, raw.find("\r\n"));
                                if (status_line.find("200 OK") == std::string::npos)
                                {
                                    result = std::unexpected(std::make_error_code(std::errc::bad_message));
                                }
                                else
                                {
                                    result = raw.substr(header_end + 4);
                                }
                            }
                        }
                    }
                }
            }
            return result;
        }
    };
}