
# TOAR — Tool-Oriented Agentic Runtime

A production-grade, fully asynchronous C++23 agentic AI runtime that orchestrates Reason-Act-Tool (ReAct) loops with local and cloud LLMs. Built entirely from first principles with **zero external dependencies** — no boost, no asio, no nlohmann, no libcurl, no cpp-httplib. Every layer, from JSON parsing to coroutine scheduling to MCP transport, is hand-written in modern C++23.

```
  You> Create a Python project with a venv, write a script, run it.

  TOAR> CYCLE 1: run_command("mkdir -p /tmp/GPT-Projects")
        CYCLE 2: run_command("python3 -m venv .venv")
        CYCLE 3: write_file("hello.py", "print('Hello, AI World!')")
        CYCLE 4: Final answer with project structure + instructions

        The project has been set up. Run it with:
          cd /tmp/GPT-Projects && source .venv/bin/activate && python hello.py

  You> Can you run it?

  TOAR> CYCLE 1: run_command("python3 /tmp/GPT-Projects/hello.py")
        Output: Hello, AI World!
```

---

## Table of Contents

- [TOAR — Tool-Oriented Agentic Runtime](#toar--tool-oriented-agentic-runtime)
  - [Table of Contents](#table-of-contents)
  - [What is TOAR?](#what-is-toar)
  - [Why TOAR?](#why-toar)
  - [Architecture](#architecture)
  - [Features](#features)
    - [Agentic Engine](#agentic-engine)
    - [LLM Integration](#llm-integration)
    - [MCP Transport](#mcp-transport)
    - [Foundation (6 headers, zero dependencies)](#foundation-6-headers-zero-dependencies)
    - [Safety \& Robustness](#safety--robustness)
  - [Requirements](#requirements)
  - [Project Structure](#project-structure)
  - [Building](#building)
  - [Configuration](#configuration)
    - [tool\_servers.json](#tool_serversjson)
    - [CLI Arguments](#cli-arguments)
    - [Environment Variables](#environment-variables)
  - [Usage](#usage)
    - [Interactive Chat](#interactive-chat)
    - [Commands](#commands)
    - [Programmatic API](#programmatic-api)
  - [Foundation Libraries](#foundation-libraries)
  - [The ReAct Loop](#the-react-loop)
  - [Comparison with Other Agentic Frameworks](#comparison-with-other-agentic-frameworks)
  - [Limitations and Gotchas](#limitations-and-gotchas)
  - [License](#license)

---

## What is TOAR?

TOAR is an **autonomous agentic runtime** — a C++ program that:

1. **Connects** to multiple MCP (Model Context Protocol) servers via stdio or HTTP
2. **Discovers** their tools automatically
3. **Builds** an OpenAI-compatible tool schema from the discovered tools
4. **Sends** the conversation + tool schema to an LLM (llama-server, Ollama, OpenAI, etc.)
5. **Dispatches** tool calls requested by the LLM to the appropriate MCP server
6. **Loops** (ReAct) until the LLM produces a final text answer
7. **Maintains** multi-turn conversation history across cycles

All I/O is fully asynchronous using C++23 coroutines — LLM API calls, MCP transport, and tool dispatch all use `co_await`. No callbacks, no threads manually managed, no blocking calls on the reactor thread.

---

## Why TOAR?

Most agentic AI frameworks are written in Python (LangChain, AutoGen, CrewAI) and carry the weight of the Python ecosystem — GIL, dynamic typing, pip dependencies, slow startup. TOAR is built in C++23 from the ground up:

| Aspect | Python Frameworks | TOAR |
|--------|------------------|------|
| **Language** | Python (interpreted, GIL) | C++23 (compiled, native) |
| **Startup time** | 1-5 seconds (import overhead) | <100ms |
| **Dependencies** | 10-100+ pip packages | Zero |
| **Async model** | asyncio (single-threaded event loop) | C++23 coroutines + multi-threaded reactor |
| **JSON** | `json` module (slow, exception-based) | `poorijson` (std::expected, transparent hash, zero-alloc) |
| **HTTP** | `requests` or `aiohttp` (heavy deps) | `AsyncHTTPClient` (built on AsyncSocket) |
| **MCP** | `mcp` Python SDK (requires Python runtime) | Native C++ (subprocess or HTTP) |
| **Memory** | GC + reference counting | RAII (deterministic, no GC pauses) |
| **Binary size** | Requires Python interpreter | Single binary (~1MB) |
| **Deployment** | `pip install` + virtualenv | Copy one binary |
| **Error handling** | Exceptions everywhere | `std::expected<T, E>` (no exceptions for control flow) |

**TOAR proves that a fully-functional agentic AI runtime can be built in pure C++23 with zero external dependencies.**

---

## Architecture

```
┌──────────────────────────────────────────────────────────────────┐
│                         TOAR Runtime                             │
│                                                                  │
│  ┌────────────────────────────────────────────────────────────┐  │
│  │                      agent.hpp                             │  │
│  │                                                            │  │
│  │   Agent                                                    │  │
│  │   ├── setup()          → reads tool_servers.json           │  │
│  │   ├── build_tools_schema() → OpenAI format from MCP tools  │  │
│  │   ├── dispatch_tool()  → routes to correct MCPClient       │  │
│  │   ├── reason_act_loop() → ReAct engine (co_await)          │  │
│  │   └── chat()           → multi-turn stateful conversation  │  │
│  │                                                            │  │
│  │   ToolMap: tool_name → client_index (transparent hash)     │  │
│  │   Logger: per-agent file logging (std::format)             │  │
│  │   History: persistent JSON conversation array              │  │
│  └───────────┬──────────────────────────┬─────────────────────┘  │
│              │                          │                        │
│   ┌──────────▼───────────┐    ┌─────────▼───────────────┐        │
│   │     llm.hpp          │    │    poorimcp.hpp         │        │
│   │                      │    │                         │        │
│   │  AsyncLLMClient      │    │  MCPClient              │        │
│   │   co_await post()    │    │   connect_async()       │        │
│   │   parse response     │    │   initialize()          │        │
│   │   extract message    │    │   call_tool_async()     │        │
│   │   return tool_calls  │    │                         │        │
│   │                      │    │  MCPTransport           │        │
│   │  Uses AsyncHTTPClient│    │   STDIO (pipes)         │        │
│   │  (from poorimcp.hpp) │    │   HTTP (POST)           │        │
│   │                      │    │                         │        │
│   │  OpenAI-compatible:  │    │  AsyncHTTPClient        │        │
│   │  /v1/chat/completions│    │   (built on AsyncSocket)│        │
│   └──────────┬───────────┘    └───────────┬─────────────┘        │
│              │                            │                      │
│              │         ┌──────────────────┘                      │
│              │         │                                         │
│              ▼         ▼                                         │
│   ┌───────────────────────────────────────────────────────┐      │
│   │              pooriasync (foundation)                  │      │
│   │                                                       │      │
│   │  asyncore.hpp         io_thread_pool.hpp              │      │
│   │  ├─ AsyncTask<T>      ├─ NetworkReactor (epoll/kqueue)│      │
│   │  ├─ DetachedTask      ├─ AsyncSocket (co_await)       │      │
│   │  ├─ FireAndForget     ├─ AsyncPipe (co_await)         │      │
│   │  ├─ CancellationToken ├─ ThreadPool (round-robin)     │      │
│   │  └─ MoveOnlyFunction  └─ DetachedTask                 │      │
│   │                                                       │      │
│   │  cpu_thread_pool.hpp   process.hpp                    │      │
│   │  ├─ ChaseLevDeque     ├─ Process (RAII)               │      │
│   │  ├─ ThreadPool        ├─ fork/exec/waitpid            │      │
│   │  ├─ TaskGroup         ├─ close_stdin()                │      │
│   │  └─ submit/spawn      └─ noexcept terminate           │      │
│   └───────────────────────────────────────────────────────┘      │
│              │                                                   │
│              ▼                                                   │
│   ┌──────────────────────────────────────────────────────┐       │
│   │              poorijson (foundation)                  │       │
│   │                                                      │       │
│   │  poorijson.hpp           poorijsonrpc.hpp            │       │
│   │  ├─ JSON (variant)       ├─ make_request()           │       │
│   │  ├─ parse()              ├─ make_response()          │       │
│   │  ├─ parse_lenient()      ├─ make_error()             │       │
│   │  ├─ transparent hash     ├─ make_notification()      │       │
│   │  ├─ at() checked access  ├─ make_batch()             │       │
│   │  └─ std::expected        └─ classify()               │       │
│   └──────────────────────────────────────────────────────┘       │
└──────────────────────────────────────────────────────────────────┘
                    │                        │
                    ▼                        ▼
          ┌─────────────────┐    ┌──────────────────────┐
          │   LLM Server    │    │   MCP Servers        │
          │                 │    │                      │
          │  llama-server   │    │  mcp-filesystem      │
          │  Ollama         │    │  sequential-thinking │
          │  OpenAI API     │    │  python-interpreter  │
          │  vLLM           │    │  mcp-shell-server    │
          │  (HTTP POST)    │    │  playwright (HTTP)   │
          │                 │    │  (stdio / HTTP)      │
          └─────────────────┘    └──────────────────────┘
```

---

## Features

### Agentic Engine
- **ReAct Loop** — Reason → Act → Tool Call → Observe → Loop until final answer
- **Multi-turn chat** — persistent conversation history across user messages
- **Tool dispatch** — routes tool calls to the correct MCP client by tool name
- **Max cycle limit** — prevents infinite loops (configurable)
- **Error recovery** — tool errors are fed back to the LLM as observations

### LLM Integration
- **OpenAI-compatible** — works with llama-server, Ollama, vLLM, OpenAI API
- **Function calling** — sends tool schema, receives `tool_calls`, dispatches them
- **Lenient JSON parsing** — handles LLM-generated malformed JSON arguments
- **Assistant message normalization** — ensures `content` field exists (llama.cpp compatibility)
- **Async HTTP** — `co_await http_client.post()` via `AsyncHTTPClient` on `AsyncSocket`

### MCP Transport
- **STDIO** — subprocess + newline-delimited JSON-RPC over pipes
- **HTTP** — Streamable HTTP POST to an MCP endpoint
- **Auto-dispatch** — `send_rpc_async()` detects transport type automatically
- **Spec-compliant initialize** — `protocolVersion`, `capabilities`, `clientInfo`

### Foundation (6 headers, zero dependencies)
- **poorijson** — `std::variant`-backed JSON, transparent hash/eq, `std::expected` errors, `parse_lenient()`
- **poorijsonrpc** — typed JSON-RPC 2.0 builders, `classify()`, `ErrorCode` enum
- **pooriasync** — C++23 coroutines, epoll/kqueue reactor, Chase-Lev work-stealing, RAII process management
- **io_thread_pool.hpp** — I/O Reactor + Thread Pool
- **process.hpp** — Process Manager
- **poorimcp** — `MCPServer` (STDIO+HTTP), `MCPClient`, `MCPTransport`, `AsyncHTTPClient`, `ToolHandler`

### Safety & Robustness
- **`TOAR_SHELL_ENABLED` env var** — explicit opt-in for shell execution
- **Command denylist** — blocks catastrophic commands (`rm -rf /`, `mkfs`, etc.)
- **Output truncation** — limits tool output to 8000 chars (prevents context overflow)
- **`CancellationToken`** — graceful server shutdown
- **`std::expected<T, E>`** — no exceptions for control flow
- **RAII everywhere** — no resource leaks, no zombies, no dangling handles

---

## Requirements

- **C++23** compiler (Clang 16+, GCC 13+)
- **Unix** (Linux with epoll, or Mac/BSD with kqueue)
- **Local LLM server** (llama-server, Ollama, vLLM) or cloud API (OpenAI)
- **Node.js + npx** (for official MCP servers like filesystem, sequential-thinking)
- **No external C++ dependencies**

## Project Structure

```text
toar/
├── bin/
├── include/
│   ├── poorijson.hpp           # JSON foundation
│   ├── poorijsonrpc.hpp        # JSON-RPC 2.0 helpers
│   ├── asyncore.hpp            # Coroutine primitives
│   ├── io_thread_pool.hpp      # I/O reactor + thread pool
│   ├── cpu_thread_pool.hpp     # CPU work-stealing pool
│   ├── process.hpp             # Process management
│   ├── poorimcp.hpp            # MCP layer
│   ├── utilities.hpp           # Logger, config parser, URL parser
│   ├── llm.hpp                 # Async LLM client
│   └── agent.hpp               # Agentic runtime
├── src/
│   ├── toar.cpp                # Interactive CLI
│   ├── mcp_shell_server.cpp    # An MCP server
│   └── toar_test.cpp           # Regression tests
├── tool_servers.json           # MCP server configuration
└── README.md
```

## Building

```bash
mkdir -p bin
clang++ -std=c++23 -O3 -I include src/toar.cpp -o bin/toar
```

---

## Configuration

### tool_servers.json

Place in the project root. TOAR reads this at startup and connects to each enabled server.

```json
{
  "mcp_servers": [
    {
      "name": "filesystem",
      "transport": "stdio",
      "command": ["npx", "-y", "@modelcontextprotocol/server-filesystem", "/tmp"],
      "enabled": true,
      "description": "File system access"
    },
    {
      "name": "sequential-thinking",
      "transport": "stdio",
      "command": ["npx", "-y", "@modelcontextprotocol/server-sequential-thinking"],
      "enabled": true,
      "description": "Reflective problem-solving"
    },
    {
      "name": "python-interpreter",
      "transport": "stdio",
      "command": ["bash", "-c", "cd /path/to/python-mcp-server && clj -M:run"],
      "enabled": true,
      "description": "Python code execution"
    },
    {
      "name": "shell",
      "transport": "stdio",
      "command": ["/path/to/mcp-shell-server/bin/mcp-shell-server"],
      "enabled": true,
      "description": "Shell command execution"
    },
    {
      "name": "playwright",
      "transport": "http",
      "url": "http://localhost:8931/mcp",
      "enabled": true,
      "description": "Browser automation"
    }
  ]
}
```

### CLI Arguments

```bash
./bin/toar <llm_url> <model_name> <temperature>
```

| Argument | Example | Description |
|----------|---------|-------------|
| `llm_url` | `http://127.0.0.1:8080` | LLM server URL (must include `http://`) |
| `model_name` | `gpt-oss-20b` | Model name passed to the API |
| `temperature` | `0.7` | Sampling temperature (0.0 - 2.0) |

### Environment Variables

| Variable | Default | Description |
|----------|---------|-------------|
| `TOAR_SHELL_ENABLED` | (unset) | Set to `1` to enable shell command execution in the shell MCP server |

---

## Usage

### Interactive Chat

```bash
# Start llama-server with your model
llama-server -m gpt-oss-20b --port 8080

# Start TOAR
TOAR_SHELL_ENABLED=1 ./bin/toar http://127.0.0.1:8080 gpt-oss-20b 0.7
```

```
  ==========================================================
  ||    T O A R  —  Tool-Oriented Agentic AI Runtime      ||
  ||         C++23 | Zero-Dep | MCP | JSON-RPC            ||
  ==========================================================

  Agent ID    : toar-agent
  LLM Model   : gpt-oss-20b
  LLM Server  : 127.0.0.1:8080
  MCP Config  : tool_servers.json
  Max Cycles  : 15
  Temperature : 0.7

  ✅ 17 tools discovered across 4 servers.

  ==================================================
  TOAR is ready. Type your message and press Enter.
  Commands:  /quit  /clear  /tools  /history
  ==================================================

  You> What files are in /tmp?
  TOAR> CYCLE 1: list_directory({"path":"/tmp"})
        → [DIR] powerlog

        Contents of /tmp: powerlog/ (directory)

  You> Create a Python project in /tmp/GPT-Projects with a venv and a hello script
  TOAR> CYCLE 1: run_command("mkdir -p /tmp/GPT-Projects")
        CYCLE 2: run_command("python3 -m venv /tmp/GPT-Projects/.venv")
        CYCLE 3: write_file("/tmp/GPT-Projects/hello.py", "print('Hello, AI World!')")
        CYCLE 4: Final answer with structure + instructions

  You> Run it
  TOAR> CYCLE 1: run_command("python3 /tmp/GPT-Projects/hello.py")
        Output: Hello, AI World!

  You> /quit
  Shutting down. Goodbye!
```

### Commands

| Command | Description |
|---------|-------------|
| `/quit` | Exit TOAR |
| `/clear` | Clear conversation history |
| `/tools` | List all discovered tools |
| `/history` | Show conversation history |

### Programmatic API

```cpp
#include "agent.hpp"

using namespace pooriayousefi::toar;
using namespace pooriayousefi::io_bound;

int main() {
    ThreadPool pool{4};

    AgentConfig config;
    config.id = "my-agent";
    config.llm_model_name = "gpt-oss-20b";
    config.llm_host = "127.0.0.1";
    config.llm_port = 8080;
    config.system_prompt = "You are a helpful coding assistant.";
    config.mcp_config_file = "tool_servers.json";
    config.temperature = 0.7;
    config.max_cycles = 15;

    Agent agent{std::move(config)};

    // Setup: connect to MCP servers, discover tools
    auto setup_fut = pool.run(agent.setup());
    setup_fut.get();

    // Chat (returns future<string>)
    auto fut = pool.run(agent.chat("List all Python files in /tmp"));
    std::string response = fut.get();
    std::println("{}", response);
}
```

---

## Foundation Libraries

TOAR is built on a vertically integrated stack of zero-dependency header-only libraries:

| Library | Repo | Headers | Description |
|---------|------|---------|-------------|
| poorijson | [GitHub](https://github.com/pooriayousefi/poorijson) | `poorijson.hpp`, `poorijsonrpc.hpp` | JSON + JSON-RPC 2.0 (std::expected, transparent hash) |
| pooriasync | [GitHub](https://github.com/pooriayousefi/pooriasync) | `asyncore.hpp`, `io_thread_pool.hpp`, `cpu_thread_pool.hpp`, `process.hpp` | Coroutines + epoll/kqueue + work-stealing + process mgmt |
| poorimcp | [GitHub](https://github.com/pooriayousefi/poorimcp) | `poorimcp.hpp` | MCP layer (server + client + transport + HTTP) |
| mcp-shell-server | [GitHub](https://github.com/pooriayousefi/mcp-shell-server) | `main.cpp` | Standalone MCP server for shell command execution |

All headers are copied into TOAR's `include/` directory. No CMakeLists, no package manager, no build system — just `clang++ -std=c++23`.

---

## The ReAct Loop

```
┌─────────────────────────────────────────────────────────────────┐
│                                                                 │
│  ┌─────────────┐                                                │
│  │ User Prompt │                                                │
│  └──────┬──────┘                                                │
│         ▼                                                       │
│  ┌─────────────────┐    ┌──────────────────────────────┐        │
│  │ Build messages  │───▶│ Send to LLM (co_await)       │        │
│  │ system + history│    │ POST /v1/chat/completions    │        │
│  └─────────────────┘    └──────────────┬───────────────┘        │
│                                        │                        │
│                                        ▼                        │
│                              ┌─────────────────┐                │
│                              │ LLM Response    │                │
│                              │ (assistant msg) │                │
│                              └────────┬────────┘                │
│                                       │                         │
│                          ┌────────────┴────────────┐            │
│                          │                         │            │
│                          ▼                         ▼            │
│                   ┌────────────┐          ┌──────────────┐      │
│                   │ Has        │   YES    │ Final text   │      │
│                   │ tool_calls?├────────▶ │ answer       │      │
│                   └─────┬──────┘          └──────┬───────┘      │
│                         │ NO                     │              │
│                         ▼                        ▼              │
│              ┌──────────────────┐         ┌──────────┐          │
│              │ For each call:   │         │  Return  │          │
│              │ dispatch_tool()  │         │  answer  │          │
│              │ (co_await)       │         └──────────┘          │
│              │                  │                               │
│              │ Parse arguments  │                               │
│              │ (parse_lenient)  │                               │
│              │                  │                               │
│              │ Route to correct │                               │
│              │ MCPClient        │                               │
│              │                  │                               │
│              │ Append result as │                               │
│              │ "tool" message   │                               │
│              └────────┬─────────┘                               │
│                       │                                         │
│                       └─────────────────────────────────────────┘
│                              (loop back to LLM)                 │
│                                                                 │
└─────────────────────────────────────────────────────────────────┘
```

The loop continues until:
- The LLM produces a final text answer (no `tool_calls`), or
- `max_cycles` is reached (default 15)

---

## Comparison with Other Agentic Frameworks

| Feature | TOAR | LangChain | AutoGen | CrewAI | PydanticAI |
|---------|------|-----------|---------|--------|------------|
| **Language** | C++23 | Python | Python | Python | Python |
| **Dependencies** | Zero | 50+ pip | 30+ pip | 20+ pip | 10+ pip |
| **Startup** | <100ms | 2-5s | 2-5s | 2-5s | 2-5s |
| **MCP support** | Native (C++) | Python SDK | Python SDK | No | No |
| **Async I/O** | C++23 coroutines (epoll/kqueue) | asyncio | asyncio | asyncio | asyncio |
| **Thread pool** | Work-stealing (Chase-Lev) | No | No | No | No |
| **JSON** | Custom (std::expected) | stdlib json | stdlib json | stdlib json | stdlib json |
| **HTTP client** | AsyncHTTPClient (zero-dep) | requests/aiohttp | requests | requests | httpx |
| **Error handling** | std::expected | Exceptions | Exceptions | Exceptions | Exceptions |
| **Process mgmt** | RAII (no zombies) | subprocess | subprocess | subprocess | subprocess |
| **Binary** | Single ~1MB binary | Requires Python | Requires Python | Requires Python | Requires Python |
| **Deployment** | Copy binary | pip + venv | pip + venv | pip + venv | pip + venv |
| **Structured concurrency** | TaskGroup | No | No | No | No |
| **Cancellation** | CancellationToken | ad-hoc | ad-hoc | No | No |

---

## Limitations and Gotchas

1. **Unix only.** Linux (epoll) and Mac/BSD (kqueue). No Windows native — use WSL2.
2. **No streaming.** LLM responses are received in full (no SSE streaming). Each request waits for the complete response.
3. **No TLS/SSL.** HTTP transport is plaintext. For production over network, add TLS.
4. **Object key order is unspecified.** JSON objects use `std::unordered_map` — key order varies between runs.
5. **`sync_wait` deadlocks.** Never call `sync_wait()` inside a reactor thread. Use `ThreadPool::run()` instead.
6. **Tool output truncated at 8000 chars.** Prevents LLM context overflow. Adjust if needed.
7. **Shell execution requires opt-in.** Set `TOAR_SHELL_ENABLED=1` to enable `run_command` tool.
8. **`max_cycles` defaults to 15.** Complex multi-step tasks may need more. Set in `AgentConfig`.
9. **History grows unbounded.** Long conversations will eventually exceed the LLM context window. Use `/clear` to reset.
10. **No concurrent tool calls.** The ReAct loop dispatches tool calls sequentially. Future versions could dispatch in parallel.
11. **MCP server processes must be started by TOAR.** The `process.hpp` layer spawns subprocesses — they must be executable and in PATH or specified by absolute path.
12. **No SSE for MCP HTTP transport.** Each MCP HTTP request is a single POST/response. For streaming MCP servers, SSE support would be needed.

---

## License

Apache License 2.0 — see the headers of each `.hpp` file.

---

**Author:** Pooria Yousefi

---