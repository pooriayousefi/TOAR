// ============================================================================
//  poorimcp.hpp — Async Model Context Protocol (Official Spec)
//  Developed by: Pooria Yousefi
//  License: Apache 2.0
//
//  Supports the two official MCP transports:
//    1. STDIO  — subprocess + newline-delimited JSON-RPC over pipes
//    2. HTTP   — Streamable HTTP (POST JSON-RPC to an HTTP endpoint)
// ============================================================================
#pragma once

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

#include "asyncore.hpp"
#include "io_thread_pool.hpp"
#include "poorijson.hpp"
#include "poorijsonrpc.hpp"
#include "process.hpp"

#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <arpa/inet.h>

namespace pooriayousefi::mcp
{
    using namespace core;
    using namespace io_bound;
    using namespace json;
    using namespace process;
    using namespace rpc;

    /// @brief Describes an MCP tool's metadata and schema.
    struct MCPTool
    {
        std::string name{};
        std::string description{};
        JSON parameters_schema{};
    };

    /// @brief Type-erased, move-only handler for MCP tool invocations.
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

            explicit Model(F&& f) : func(std::move(f))
            {
            }

            AsyncTask<JSON> invoke(const JSON& args) override
            {
                return func(args);
            }
        };

        std::unique_ptr<Base> impl_;

    public:
        ToolHandler() = default;

        template <typename F>
            requires(!std::is_same_v<std::decay_t<F>, ToolHandler>)
        ToolHandler(F&& f)
            : impl_(std::make_unique<Model<std::decay_t<F>>>(std::forward<F>(f)))
        {
        }

        ToolHandler(ToolHandler&&) = default;
        ToolHandler& operator=(ToolHandler&&) = default;
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

    /// @brief Awaitable that suspends until a socket becomes readable/writable.
    struct SocketAwaitable
    {
        NetworkReactor* reactor;
        socket_t fd;
        NetworkReactor::EventType type;

        bool await_ready() noexcept
        {
            return false;
        }

        void await_suspend(std::coroutine_handle<> h)
        {
            reactor->register_socket(
                fd,
                type,
                [h]()
                {
                    h.resume();
                }
            );
        }

        void await_resume() noexcept
        {
        }
    };

    // ----------------------------------------------------------------
    //  AsyncHTTPClient — minimal HTTP/1.1 client for MCP HTTP transport
    //  and LLM API calls.  Built directly on AsyncSocket.
    // ----------------------------------------------------------------

    class AsyncHTTPClient
    {
        std::string host_{};
        int port_{80};
        NetworkReactor* reactor_{nullptr};

    public:
        AsyncHTTPClient() = default;

        AsyncHTTPClient(std::string host, int port)
            : host_{std::move(host)}
            , port_{port}
            , reactor_{NetworkReactor::current}
        {
        }

        AsyncTask<std::expected<std::string, std::error_code>>
        post(std::string_view path, std::string_view body,
             std::string_view content_type = "application/json")
        {
            std::expected<std::string, std::error_code> result{};

            if (!reactor_)
            {
                result = std::unexpected(std::make_error_code(std::errc::operation_not_permitted));
            }
            else
            {
                AsyncSocket sock{reactor_};

                auto connect_result = co_await sock.async_connect(host_, port_);
                if (!connect_result)
                {
                    result = std::unexpected(connect_result.error());
                }
                else
                {
                    // Build the HTTP/1.1 request.
                    std::string request;
                    request.reserve(256 + body.size());
                    request += "POST ";
                    request += path;
                    request += " HTTP/1.1\r\n";
                    request += "Host: ";
                    request += host_;
                    request += "\r\n";
                    request += "Content-Type: ";
                    request += content_type;
                    request += "\r\n";
                    request += "Content-Length: ";
                    request += std::to_string(body.size());
                    request += "\r\n";
                    request += "Connection: close\r\n\r\n";
                    request += body;

                    auto send_result = co_await sock.send(std::as_bytes(std::span{request}));
                    if (!send_result)
                    {
                        result = std::unexpected(send_result.error());
                    }
                    else
                    {
                        // Read the full response.
                        std::string raw;
                        char chunk[4096];
                        bool done = false;

                        while (!done)
                        {
                            auto recv_result = co_await sock.recv(std::as_writable_bytes(std::span{chunk}));
                            if (!recv_result || *recv_result == 0)
                            {
                                done = true;
                            }
                            else
                            {
                                raw.append(chunk, *recv_result);
                            }
                        }

                        // Parse HTTP response: status line + headers + \r\n\r\n + body.
                        auto header_end = raw.find("\r\n\r\n");
                        if (header_end == std::string::npos)
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

            co_return result;
        }

        AsyncTask<std::expected<std::string, std::error_code>>
        get(std::string_view path)
        {
            std::expected<std::string, std::error_code> result{};

            if (!reactor_)
            {
                result = std::unexpected(std::make_error_code(std::errc::operation_not_permitted));
            }
            else
            {
                AsyncSocket sock{reactor_};

                auto connect_result = co_await sock.async_connect(host_, port_);
                if (!connect_result)
                {
                    result = std::unexpected(connect_result.error());
                }
                else
                {
                    std::string request;
                    request.reserve(256);
                    request += "GET ";
                    request += path;
                    request += " HTTP/1.1\r\n";
                    request += "Host: ";
                    request += host_;
                    request += "\r\n";
                    request += "Connection: close\r\n\r\n";

                    auto send_result = co_await sock.send(std::as_bytes(std::span{request}));
                    if (!send_result)
                    {
                        result = std::unexpected(send_result.error());
                    }
                    else
                    {
                        std::string raw;
                        char chunk[4096];
                        bool done = false;

                        while (!done)
                        {
                            auto recv_result = co_await sock.recv(std::as_writable_bytes(std::span{chunk}));
                            if (!recv_result || *recv_result == 0)
                            {
                                done = true;
                            }
                            else
                            {
                                raw.append(chunk, *recv_result);
                            }
                        }

                        auto header_end = raw.find("\r\n\r\n");
                        if (header_end == std::string::npos)
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

            co_return result;
        }
    };

    // ----------------------------------------------------------------
    //  MCPTransport — unified STDIO / HTTP transport layer
    // ----------------------------------------------------------------

    class MCPTransport
    {
    public:
        enum class Type
        {
            STDIO,
            HTTP
        };

    private:
        Type type_{Type::STDIO};
        // STDIO fields
        std::string executable_{};
        std::vector<std::string> args_{};
        std::unique_ptr<Process> process_{};
        std::optional<AsyncPipe> pipe_{};
        // HTTP fields
        std::string host_{};
        int port_{80};
        std::string path_{"/mcp"};
        std::optional<AsyncHTTPClient> http_client_{};

    public:
        MCPTransport() = default;

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

        AsyncTask<std::expected<void, std::error_code>> connect()
        {
            std::expected<void, std::error_code> result{};

            if (type_ == Type::STDIO)
            {
                try
                {
                    process_ = std::make_unique<Process>();
                    process_->start(executable_, args_);
                    pipe_.emplace(NetworkReactor::current);
                    pipe_->assign_write(process_->get_stdin_write());
                    pipe_->assign_read(process_->get_stdout_read());
                }
                catch (const std::exception&)
                {
                    result = std::unexpected(std::make_error_code(std::errc::no_such_process));
                }
            }
            else if (type_ == Type::HTTP)
            {
                http_client_.emplace(host_, port_);
                result = {};
            }

            co_return result;
        }

        AsyncTask<std::expected<std::size_t, std::error_code>> send(std::span<const std::byte> buf)
        {
            std::expected<std::size_t, std::error_code> result{
                std::unexpected(std::make_error_code(std::errc::operation_not_permitted))
            };

            if (type_ == Type::STDIO && pipe_)
            {
                result = co_await pipe_->send(buf);
            }
            else if (type_ == Type::HTTP && http_client_)
            {
                result = buf.size();
            }

            co_return result;
        }

        AsyncTask<std::expected<std::size_t, std::error_code>> recv(std::span<std::byte> buf)
        {
            std::expected<std::size_t, std::error_code> result{
                std::unexpected(std::make_error_code(std::errc::operation_not_permitted))
            };

            if (type_ == Type::STDIO && pipe_)
            {
                result = co_await pipe_->recv(buf);
            }
            else if (type_ == Type::HTTP && http_client_)
            {
                result = 0;
            }

            co_return result;
        }

        // ----------------------------------------------------------------
        //  HTTP-specific: send a JSON-RPC message and receive the response.
        // ----------------------------------------------------------------

        AsyncTask<std::expected<JSON, std::error_code>> send_http_rpc(const JSON& request)
        {
            std::expected<JSON, std::error_code> result{};

            if (type_ != Type::HTTP || !http_client_)
            {
                result = std::unexpected(std::make_error_code(std::errc::operation_not_permitted));
            }
            else
            {
                std::string body = request.dump();
                auto resp = co_await http_client_->post(path_, body);

                if (!resp)
                {
                    result = std::unexpected(resp.error());
                }
                else
                {
                    auto parsed = parse(*resp);
                    if (!parsed)
                    {
                        result = std::unexpected(std::make_error_code(std::errc::bad_message));
                    }
                    else
                    {
                        result = std::move(*parsed);
                    }
                }
            }

            co_return result;
        }

        [[nodiscard]] bool is_http() const noexcept
        {
            return type_ == Type::HTTP;
        }

        [[nodiscard]] bool is_stdio() const noexcept
        {
            return type_ == Type::STDIO;
        }
    };

    // ----------------------------------------------------------------
    //  MCPServer — async MCP server (STDIO + HTTP)
    // ----------------------------------------------------------------

    class MCPServer
    {
    public:
        enum class Type
        {
            STDIO,
            HTTP
        };

        using ToolMap = std::unordered_map<std::string, std::pair<MCPTool, ToolHandler>,
                                           JSONStringHash, JSONStringEqual>;

    private:
        Type type_{Type::STDIO};
        int port_{-1};
        ToolMap tools_{};
        socket_t listen_fd_{INVALID_SOCK};
        CancellationToken token_{};

    public:
        // STDIO mode — reads from stdin, writes to stdout.
        MCPServer() : type_{Type::STDIO}
        {
        }

        // HTTP mode — listens on a TCP port for HTTP POST requests.
        explicit MCPServer(int port) : type_{Type::HTTP}, port_{port}
        {
        }

        ~MCPServer()
        {
            stop();

            if (listen_fd_ != INVALID_SOCK)
            {
                close_socket_impl(listen_fd_);
                listen_fd_ = INVALID_SOCK;
            }
        }

        MCPServer(const MCPServer&) = delete;
        MCPServer& operator=(const MCPServer&) = delete;
        MCPServer(MCPServer&&) = delete;
        MCPServer& operator=(MCPServer&&) = delete;

        void register_tool(MCPTool tool, ToolHandler handler)
        {
            std::string name = tool.name;
            tools_.try_emplace(std::move(name), std::move(tool), std::move(handler));
        }

        void stop() noexcept
        {
            token_.cancel();
        }

        AsyncTask<void> start()
        {
            if (type_ == Type::STDIO)
            {
                co_await start_stdio();
            }
            else
            {
                co_await start_http();
            }

            co_return;
        }

    private:
        // ── STDIO mode: newline-delimited JSON-RPC over stdin/stdout ──

        AsyncTask<void> start_stdio()
        {
            std::cout << "MCP Server (STDIO) running...\n";

            std::string line;

            while (!token_.is_cancelled() && std::getline(std::cin, line))
            {
                if (!line.empty() && line.back() == '\r')
                {
                    line.pop_back();
                }

                if (line.empty())
                {
                    continue;
                }

                auto parsed_req = parse(line);
                if (parsed_req)
                {
                    JSON resp = co_await handle_rpc(*parsed_req);
                    std::cout << resp.dump() << "\n" << std::flush;
                }
            }

            co_return;
        }

        // ── HTTP mode: HTTP POST server on a TCP port ──

        AsyncTask<void> start_http()
        {
            NetworkReactor* reactor = NetworkReactor::current;

            if (!reactor)
            {
                std::cerr << "MCPServer started without a reactor context!\n"
                          << "Call start() from inside ThreadPool::run().\n";
            }
            else
            {
                listen_fd_ = socket(AF_INET, SOCK_STREAM, 0);
                if (listen_fd_ == INVALID_SOCK)
                {
                    // Cannot create socket.
                }
                else
                {
                    int opt = 1;
                    setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR,
                               reinterpret_cast<const char*>(&opt), sizeof(opt));
                    set_nonblocking(listen_fd_);

                    struct sockaddr_in address{};
                    address.sin_family = AF_INET;
                    address.sin_addr.s_addr = INADDR_ANY;
                    address.sin_port = htons(static_cast<uint16_t>(port_));

                    if (bind(listen_fd_, reinterpret_cast<struct sockaddr*>(&address),
                             sizeof(address)) < 0)
                    {
                        std::cerr << "Failed to bind to port " << port_ << "\n";
                    }
                    else if (listen(listen_fd_, 3) < 0)
                    {
                        std::cerr << "Failed to listen on socket\n";
                    }
                    else
                    {
                        std::cout << "MCP Server (HTTP) listening on port "
                                  << port_ << "...\n";

                        while (!token_.is_cancelled())
                        {
                            struct sockaddr_in client_addr{};
                            socklen_t addrlen = sizeof(client_addr);

                            socket_t client_fd = accept(listen_fd_,
                                reinterpret_cast<struct sockaddr*>(&client_addr), &addrlen);

                            if (client_fd == INVALID_SOCK)
                            {
                                co_await SocketAwaitable{
                                    reactor, listen_fd_,
                                    NetworkReactor::EventType::READABLE
                                };
                                continue;
                            }

                            auto handler = [this, client_fd]() -> DetachedTask
                            {
                                co_await handle_client(client_fd);
                                co_return;
                            };

                            auto dt = handler();
                            auto h = dt.handle;
                            dt.detach();
                            h.resume();
                        }
                    }

                    close_socket_impl(listen_fd_);
                    listen_fd_ = INVALID_SOCK;
                }
            }

            co_return;
        }

        AsyncTask<void> handle_client(socket_t client_fd)
        {
            AsyncSocket sock;
            sock.assign(client_fd);

            std::string buffer;
            char chunk[4096];
            bool request_complete = false;

            while (!request_complete)
            {
                auto recv_result = co_await sock.recv(std::as_writable_bytes(std::span{chunk}));

                if (!recv_result || *recv_result == 0)
                {
                    request_complete = true;
                }
                else
                {
                    buffer.append(chunk, *recv_result);

                    auto header_end = buffer.find("\r\n\r\n");

                    if (header_end != std::string::npos)
                    {
                        std::string_view headers{buffer.data(), header_end};
                        auto cl_pos = headers.find("Content-Length:");
                        if (cl_pos == std::string_view::npos)
                        {
                            cl_pos = headers.find("content-length:");
                        }

                        if (cl_pos != std::string_view::npos)
                        {
                            std::size_t value_start = cl_pos + 16;
                            while (value_start < headers.size() &&
                                   (headers[value_start] == ' ' || headers[value_start] == '\t'))
                            {
                                value_start++;
                            }
                            auto line_end = headers.find("\r\n", cl_pos);
                            if (line_end == std::string_view::npos)
                            {
                                line_end = headers.size();
                            }
                            std::string cl_str{headers.substr(value_start, line_end - value_start)};

                            std::size_t content_length = 0;
                            for (char c : cl_str)
                            {
                                if (c >= '0' && c <= '9')
                                {
                                    content_length = content_length * 10 + (c - '0');
                                }
                            }

                            std::size_t body_start = header_end + 4;

                            if (buffer.size() >= body_start + content_length)
                            {
                                request_complete = true;
                            }
                        }
                        else
                        {
                            request_complete = true;
                        }
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
                    JSON resp = co_await handle_rpc(*parsed_req);
                    std::string body = resp.dump();

                    std::string http_resp;
                    http_resp += "HTTP/1.1 200 OK\r\n";
                    http_resp += "Content-Type: application/json\r\n";
                    http_resp += "Content-Length: ";
                    http_resp += std::to_string(body.size());
                    http_resp += "\r\n";
                    http_resp += "Connection: close\r\n\r\n";
                    http_resp += body;

                    co_await sock.send(std::as_bytes(std::span{http_resp}));
                }
            }

            sock.close();
            co_return;
        }

        AsyncTask<JSON> handle_rpc(const JSON& req)
        {
            JSON resp{};

            if (!req.contains("id"))
            {
                resp = make_error(ErrorCode::INVALID_REQUEST,
                                  "Invalid Request: missing id", JSON(nullptr));
            }
            else
            {
                if (!req.contains("method"))
                {
                    resp = make_error(ErrorCode::INVALID_REQUEST,
                                      "Missing method", req["id"]);
                }
                else
                {
                    std::string method = req["method"].get_string();

                    if (method == "initialize")
                    {
                        JSON server_info;
                        server_info["name"] = "poorimcp-server";
                        server_info["version"] = "1.0.0";

                        JSON caps;
                        caps["tools"] = JSONObject{};

                        JSON result_obj;
                        result_obj["protocolVersion"] = "2026-07-24";
                        result_obj["serverInfo"] = server_info;
                        result_obj["capabilities"] = caps;
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

                        JSON result_obj;
                        result_obj["tools"] = std::move(tools_arr);
                        resp = make_response(std::move(result_obj), req["id"]);
                    }
                    else if (method == "tools/call")
                    {
                        if (!req.contains("params") || !req["params"].contains("name"))
                        {
                            resp = make_error(ErrorCode::INVALID_PARAMS,
                                              "Missing tool name", req["id"]);
                        }
                        else
                        {
                            std::string tool_name = req["params"]["name"].get_string();
                            JSON args = req["params"].contains("arguments")
                                          ? req["params"]["arguments"]
                                          : JSON(nullptr);

                            auto it = tools_.find(tool_name);
                            if (it == tools_.end())
                            {
                                resp = make_error(ErrorCode::METHOD_NOT_FOUND,
                                                  "Tool not found", req["id"]);
                            }
                            else
                            {
                                try
                                {
                                    JSON tool_result = co_await it->second.second(args);

                                    JSON text_content;
                                    text_content["type"] = "text";
                                    text_content["text"] = tool_result.dump();

                                    JSONArray content_arr;
                                    content_arr.push_back(text_content);

                                    JSON result_obj;
                                    result_obj["content"] = std::move(content_arr);
                                    resp = make_response(std::move(result_obj), req["id"]);
                                }
                                catch (const std::exception& e)
                                {
                                    resp = make_error(ErrorCode::INTERNAL_ERROR,
                                                      e.what(), req["id"]);
                                }
                            }
                        }
                    }
                    else
                    {
                        resp = make_error(ErrorCode::METHOD_NOT_FOUND,
                                          "Method not found", req["id"]);
                    }
                }
            }

            co_return resp;
        }
    };

    // ----------------------------------------------------------------
    //  MCPClient — async MCP client (stdio + HTTP transports)
    // ----------------------------------------------------------------

    class MCPClient
    {
        MCPTransport transport_{};
        int request_id_{0};
        std::vector<MCPTool> discovered_tools_{};

    public:
        explicit MCPClient(MCPTransport transport)
            : transport_{std::move(transport)}
        {
        }

        MCPClient(const MCPClient&) = delete;
        MCPClient& operator=(const MCPClient&) = delete;
        MCPClient(MCPClient&&) = default;
        MCPClient& operator=(MCPClient&&) = default;

        AsyncTask<std::expected<void, std::error_code>> connect_async()
        {
            auto result = co_await transport_.connect();
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

        [[nodiscard]] const std::vector<MCPTool>& get_tools() const noexcept
        {
            return discovered_tools_;
        }

        AsyncTask<std::expected<std::string, std::string>> call_tool_async(
            const std::string& tool_name,
            const JSON& args
        )
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
                // HTTP transport — POST the request and get the response.
                auto http_result = co_await transport_.send_http_rpc(request);
                if (!http_result)
                {
                    result = std::unexpected(http_result.error().message());
                }
                else if (http_result->contains("error") &&
                         (*http_result)["error"].contains("message"))
                {
                    result = std::unexpected((*http_result)["error"]["message"].get_string());
                }
                else
                {
                    result = (*http_result)["result"];
                }
            }
            else
            {
                // STDIO transport — newline-delimited JSON-RPC.
                std::string req_str = request.dump() + "\n";
                auto send_res = co_await transport_.send(std::as_bytes(std::span{req_str}));
                if (!send_res)
                {
                    result = std::unexpected("Failed to send RPC request");
                }
                else
                {
                    std::string buffer;
                    char chunk[4096];
                    bool done = false;

                    while (!done)
                    {
                        auto recv_res = co_await transport_.recv(std::as_writable_bytes(std::span{chunk}));
                        if (!recv_res || *recv_res == 0)
                        {
                            done = true;
                        }
                        else
                        {
                            buffer.append(chunk, *recv_res);
                            if (buffer.find('\n') != std::string::npos)
                            {
                                done = true;
                            }
                        }
                    }

                    auto newline_pos = buffer.find('\n');
                    if (newline_pos == std::string::npos)
                    {
                        result = std::unexpected("No newline in response");
                    }
                    else
                    {
                        std::string_view json_sv{buffer.data(), newline_pos};
                        if (!json_sv.empty() && json_sv.back() == '\r')
                        {
                            json_sv.remove_suffix(1);
                        }

                        auto parsed = parse(json_sv);
                        if (!parsed)
                        {
                            result = std::unexpected("Invalid JSON from MCP Server");
                        }
                        else if (parsed->contains("error") &&
                                 (*parsed)["error"].contains("message"))
                        {
                            result = std::unexpected((*parsed)["error"]["message"].get_string());
                        }
                        else
                        {
                            result = (*parsed)["result"];
                        }
                    }
                }
            }

            co_return result;
        }
    };
}