
<img width="1254" height="1254" alt="ChatGPT Image Sep 12, 2026, 10_42_38 PM" src="https://github.com/user-attachments/assets/b343166f-3ac4-4ef3-8772-a4b5d514f288" />


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
    - [Suite of Custom C++ MCP Servers](#suite-of-custom-c-mcp-servers)
    - [Foundation (Zero Dependencies)](#foundation-zero-dependencies)
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

All I/O is fully asynchronous using C++23 coroutines — LLM API calls, MCP transport, and tool dispatch all use `co_await`. No callbacks, no manually managed threads, no blocking calls on the main thread.

---

## Why TOAR?

Most agentic AI frameworks are written in Python (LangChain, AutoGen, CrewAI) and carry the weight of the Python ecosystem — GIL, dynamic typing, pip dependencies, slow startup. TOAR is built in C++23 from the ground up:

| Aspect | Python Frameworks | TOAR |
|--------|------------------|------|
| **Language** | Python (interpreted, GIL) | C++23 (compiled, native) |
| **Startup time** | 1-5 seconds (import overhead) | <100ms |
| **Dependencies** | 10-100+ pip packages | Zero |
| **Async model** | asyncio (single-threaded event loop) | C++23 coroutines + Thread Pool bridging |
| **JSON** | `json` module (slow, exception-based) | `poorijson` (std::expected, transparent hash, zero-alloc) |
| **HTTP** | `requests` or `aiohttp` (heavy deps) | Native Cross-Platform Sockets (no libcurl) |
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
│   │   parse SSE stream   │    │   initialize()          │        │
│   │   extract message    │    │   call_tool_async()     │        │
│   │   return tool_calls  │    │                         │        │
│   │                      │    │  MCPTransport           │        │
│   │  Uses Native Sockets │    │   STDIO (pipes)         │        │
│   │  (Cross-platform)    │    │   HTTP (POST + SSE)     │        │
│   │                      │    │                         │        │
│   │  OpenAI-compatible:  │    │  AsyncHTTPClient        │        │
│   │  /v1/chat/completions│    │   (Native Sockets)      │        │
│   └──────────┬───────────┘    └───────────┬─────────────┘        │
│              │                            │                      │
│              │         ┌──────────────────┘                      │
│              │         │                                         │
│              ▼         ▼                                         │
│   ┌───────────────────────────────────────────────────────┐      │
│   │              pooriasync (foundation)                  │      │
│   │                                                       │      │
│   │  asyncore.hpp         io_thread_pool.hpp              │      │
│   │  ├─ AsyncTask<T>      ├─ ThreadPool (co_await bridge) │      │
│   │  ├─ DetachedTask      ├─ run_blocking (park coroutines)│     │
│   │  ├─ FireAndForget     ├─ AsyncSocket (co_await)       │      │
│   │  ├─ CancellationToken ├─ AsyncPipe (co_await)         │      │
│   │  └─ MoveOnlyFunction  └─ DetachedTask                 │      │
│   │                                                       │      │
│   │  cpu_thread_pool.hpp   process.hpp                    │      │
│   │  ├─ ChaseLevDeque     ├─ Process (RAII)               │      │
│   │  ├─ ThreadPool        ├─ fork/execvp (POSIX)          │      │
│   │  ├─ TaskGroup         ├─ CreateProcessA (Windows)     │      │
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
          │  llama-server   │    │  mcp-shell-server    │
          │  Ollama         │    │  mcp-directory-server│
          │  OpenAI API     │    │  mcp-file-server     │
          │  vLLM           │    │  mcp-math-server     │
          │  (HTTP POST)    │    │  mcp-utility-server  │
          │                 │    │  mcp-git-server      │
          └─────────────────┘    │  mcp-time-server     │
                                 │  mcp-sysinfo-server  │
                                 │  mcp-memory-server   │
                                 │  mcp-web-fetch-server│
                                 │  (stdio / HTTP)      │
                                 └──────────────────────┘
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
- **Native Async HTTP with SSE** — `co_await http_client.post()` via cross-platform native sockets. Streams LLM tokens to the console in real-time and reconstructs the final assistant message.

### MCP Transport
- **STDIO** — subprocess + newline-delimited JSON-RPC over pipes
- **HTTP** — Streamable HTTP POST to an MCP endpoint
- **SSE Support** — Natively parses `text/event-stream` responses for MCP servers that stream
- **Auto-dispatch** — `send_rpc_async()` detects transport type automatically
- **Spec-compliant initialize** — `protocolVersion`, `capabilities`, `clientInfo`

### Suite of Custom C++ MCP Servers
TOAR includes a high-performance, zero-dependency suite of MCP servers written in C++23:
- **`mcp-shell-server`**: Executes arbitrary shell commands safely (with denylists and env-var opt-in).
- **`mcp-directory-server`**: Cross-platform directory creation, listing, recursive iteration, and deletion.
- **`mcp-file-server`**: File reading, writing, appending, deletion, and metadata retrieval.
- **`mcp-math-server`**: 44 tools for arithmetic, statistics, and random number generation (Normal, Poisson, Binomial, etc.).
- **`mcp-utility-server`**: String tokenization, word counting, JSON extraction from messy text, and UUID generation.
- **`mcp-git-server`**: Safe, shell-injection-free Git operations (status, diff, log, add, commit, branch, checkout).
- **`mcp-time-server`**: Current UTC/Local time, ISO 8601 time difference calculation, and custom date formatting.
- **`mcp-system-info-server`**: CPU core count, CPU usage percentage, total/available RAM, disk space, and environment variables.
- **`mcp-memory-server`**: Persistent key-value long-term memory (saves to `toar_memory.json` to survive across sessions).
- **`mcp-web-fetch-server`**: Native HTTP client to fetch URLs, strip HTML tags to clean text, and download JSON from REST APIs.

### Foundation (Zero Dependencies)
- **poorijson** — `std::variant`-backed JSON, transparent hash/eq, `std::expected` errors, `parse_lenient()`
- **poorijsonrpc** — typed JSON-RPC 2.0 builders, `classify()`, `ErrorCode` enum
- **pooriasync** — C++23 coroutines, Chase-Lev work-stealing CPU pool, RAII cross-platform process management
- **io_thread_pool.hpp** — Standard Thread Pool with `run_blocking()` coroutine bridging for cross-platform async I/O
- **process.hpp** — Cross-platform Process Manager (POSIX `fork/exec` and Windows `CreateProcessA`)
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

- **C++23** compiler (Clang 16+, GCC 13+, MSVC 19.34+)
- **Cross-Platform**: Mac, Linux, and Windows Native
- **Local LLM server** (llama-server, Ollama, vLLM) or cloud API (OpenAI)
- **No external C++ dependencies**

## Project Structure

```text
toar/
├── bin/
├── include/
│   ├── poorijson.hpp           # JSON foundation
│   ├── poorijsonrpc.hpp        # JSON-RPC 2.0 helpers
│   ├── asyncore.hpp            # Coroutine primitives
│   ├── io_thread_pool.hpp      # Thread pool + coroutine bridging
│   ├── cpu_thread_pool.hpp     # CPU work-stealing pool
│   ├── process.hpp             # Cross-platform process management
│   ├── poorimcp.hpp            # MCP layer
│   ├── utilities.hpp           # Logger, config parser, URL parser
│   ├── llm.hpp                 # Async LLM client
│   └── agent.hpp               # Agentic runtime
├── src/
│   ├── toar.cpp                # Interactive CLI
│   ├── toar_test.cpp           # Regression tests
│   ├── mcp_shell_server.cpp    # Shell command server
│   ├── mcp_directory_server.cpp# Directory operations server
│   ├── mcp_file_server.cpp     # File operations server
│   ├── mcp_math_server.cpp     # Math & statistics server
│   ├── mcp_utility_server.cpp  # String & UUID utility server
│   ├── mcp_git_server.cpp      # Git version control server
│   ├── mcp_time_server.cpp     # Time & date utilities server
│   ├── mcp_system_info_server.cpp # System resources & env server
│   ├── mcp_memory_server.cpp   # Persistent long-term memory server
│   └── mcp_web_fetch_server.cpp# Web fetching & research server
├── tool_servers.json           # MCP server configuration
└── README.md
```

## Building

**Mac/Linux:**
```bash
mkdir -p bin
clang++ -std=c++23 -O3 -I include src/toar.cpp -o bin/toar

# Build MCP Servers
clang++ -std=c++23 -O3 -I include src/mcp_shell_server.cpp -o bin/mcp_shell_server
clang++ -std=c++23 -O3 -I include src/mcp_directory_server.cpp -o bin/mcp_directory_server
clang++ -std=c++23 -O3 -I include src/mcp_file_server.cpp -o bin/mcp_file_server
clang++ -std=c++23 -O3 -I include src/mcp_math_server.cpp -o bin/mcp_math_server
clang++ -std=c++23 -O3 -I include src/mcp_utility_server.cpp -o bin/mcp_utility_server
clang++ -std=c++23 -O3 -I include src/mcp_git_server.cpp -o bin/mcp_git_server
clang++ -std=c++23 -O3 -I include src/mcp_time_server.cpp -o bin/mcp_time_server
clang++ -std=c++23 -O3 -I include src/mcp_system_info_server.cpp -o bin/mcp_system_info_server
clang++ -std=c++23 -O3 -I include src/mcp_memory_server.cpp -o bin/mcp_memory_server
clang++ -std=c++23 -O3 -I include src/mcp_web_fetch_server.cpp -o bin/mcp_web_fetch_server
```

**Windows (MSVC):**
```powershell
cl /std:c++latest /EHsc /I include src\toar.cpp /out:bin\toar.exe
```

---

## Configuration

### tool_servers.json

Place in the project root. TOAR reads this at startup and connects to each enabled server.

```json
{
  "mcp_servers": [
    {
      "name": "shell",
      "transport": "stdio",
      "command": ["./bin/mcp_shell_server"],
      "enabled": true,
      "description": "Shell command execution server"
    },
    {
      "name": "directory",
      "transport": "stdio",
      "command": ["./bin/mcp_directory_server"],
      "enabled": true,
      "description": "Directory management server"
    },
    {
      "name": "file",
      "transport": "stdio",
      "command": ["./bin/mcp_file_server"],
      "enabled": true,
      "description": "File management server"
    },
    {
      "name": "math",
      "transport": "stdio",
      "command": ["./bin/mcp_math_server"],
      "enabled": true,
      "description": "Math and statistics server"
    },
    {
      "name": "utility",
      "transport": "stdio",
      "command": ["./bin/mcp_utility_server"],
      "enabled": true,
      "description": "Utility tools server"
    },
    {
      "name": "git",
      "transport": "stdio",
      "command": ["./bin/mcp_git_server"],
      "enabled": true,
      "description": "Git version control server"
    },
    {
      "name": "time",
      "transport": "stdio",
      "command": ["./bin/mcp_time_server"],
      "enabled": true,
      "description": "Time and date utilities server"
    },
    {
      "name": "system_info",
      "transport": "stdio",
      "command": ["./bin/mcp_system_info_server"],
      "enabled": true,
      "description": "System information and environment server"
    },
    {
      "name": "memory",
      "transport": "stdio",
      "command": ["./bin/mcp_memory_server"],
      "enabled": true,
      "description": "Persistent long-term memory server"
    },
    {
      "name": "web_fetch",
      "transport": "stdio",
      "command": ["./bin/mcp_web_fetch_server"],
      "enabled": true,
      "description": "Web fetching and research server"
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

  ✅ 50+ tools discovered across 10 servers.

  ==================================================
  TOAR is ready. Type your message and press Enter.
  Commands:  /quit  /clear  /tools  /history
  ==================================================

  You> What files are in /tmp?
  TOAR> CYCLE 1: list_directory({"path":"/tmp"})
        → [DIR] powerlog

        Contents of /tmp: powerlog/ (directory)

  You> Fetch the latest commit message from this git repo.
  TOAR> CYCLE 1: git_log({"limit":1})
        → a1b2c3d Added new feature

  You> /quit
  Shutting down. Goodbye!
```

### Commands

| Command | Description |
|---------|-------------|
| `/quit` or `quit` or `exit` | Exit TOAR |
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

    Agent agent{pool, std::move(config)};

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
| pooriasync | [GitHub](https://github.com/pooriayousefi/pooriasync) | `asyncore.hpp`, `io_thread_pool.hpp`, `cpu_thread_pool.hpp`, `process.hpp` | Coroutines + Thread Pool bridging + work-stealing + process mgmt |
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
| **Platform** | Mac/Linux/Windows | Cross-platform | Cross-platform | Cross-platform | Cross-platform |
| **MCP support** | Native (C++) | Python SDK | Python SDK | No | No |
| **Async I/O** | C++23 coroutines + Thread Pool | asyncio | asyncio | asyncio | asyncio |
| **Thread pool** | Work-stealing (Chase-Lev) + Bridge | No | No | No | No |
| **JSON** | Custom (std::expected) | stdlib json | stdlib json | stdlib json | stdlib json |
| **HTTP client** | Native Sockets (zero-dep) | requests/aiohttp | requests | requests | httpx |
| **Error handling** | std::expected | Exceptions | Exceptions | Exceptions | Exceptions |
| **Process mgmt** | RAII Cross-platform | subprocess | subprocess | subprocess | subprocess |
| **Binary** | Single ~1MB binary | Requires Python | Requires Python | Requires Python | Requires Python |
| **Deployment** | Copy binary | pip + venv | pip + venv | pip + venv | pip + venv |
| **Structured concurrency** | TaskGroup | No | No | No | No |
| **Cancellation** | CancellationToken | ad-hoc | ad-hoc | No | No |

---

## Limitations and Gotchas

1. **No TLS/SSL.** HTTP transport is plaintext. For production over public networks, add TLS (OpenSSL or platform APIs).
2. **Object key order is unspecified.** JSON objects use `std::unordered_map` for O(1) zero-allocation lookups. Key order varies between runs and should not be relied upon for exact text diffs.
3. **`sync_wait` deadlocks.** Never call `sync_wait()` inside a `ThreadPool` worker thread. It will block the worker, preventing the coroutines scheduled on that pool from ever resuming. Use `ThreadPool::run()` from the main thread instead.
4. **Tool output truncation.** The `mcp-shell-server` and `mcp-web-fetch-server` truncate output at 8,000 characters by default to prevent LLM context overflow. Adjust this limit in the server source files if your specific LLM supports larger contexts.
5. **Shell execution requires opt-in.** Set `TOAR_SHELL_ENABLED=1` in your environment to enable the `run_command` tool in the shell MCP server.
6. **`max_cycles` defaults to 30.** Complex, multi-step tasks may require more cycles. Set this value in `AgentConfig` or via the CLI arguments.
7. **History grows unbounded.** Long conversations will eventually exceed the LLM's context window. Use the `/clear` command in the CLI to reset the conversation history.
8. **Sequential tool dispatch.** If the LLM requests multiple tool calls in a single cycle, the ReAct loop dispatches them sequentially. Parallel concurrent dispatch is not yet implemented.
9. **Lightweight argument validation only.** TOAR intercepts missing `required` arguments before dispatching to MCP servers, saving a network/IPC round-trip. However, it does not perform full JSON Schema validation (e.g., type checking, enums, regex patterns) prior to execution.

---

## License

Apache License 2.0 — see the headers of each `.hpp` file.

---

**Author:** Pooria Yousefi

---
