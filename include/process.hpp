// ============================================================================
//  process.hpp — Cross-Platform Process Management (Enhanced for Async IO)
//  Developed by: Pooria Yousefi
//  License: Apache 2.0
// ============================================================================
#pragma once

#include <cerrno>
#include <cstring>
#include <format>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
#include <expected>
#include <system_error>

#ifdef _WIN32
#  define WIN32_LEAN_AND_MEAN
#  define NOMINMAX
#  include <windows.h>
#else
#  include <csignal>
#  include <sys/types.h>
#  include <sys/wait.h>
#  include <unistd.h>
#endif

namespace pooriayousefi::process
{
    // ----------------------------------------------------------------
    //  Platform-specific type aliases
    // ----------------------------------------------------------------
#ifdef _WIN32
    /// Native OS handle type for pipe endpoints (HANDLE on Windows).
    using native_handle_type = HANDLE;
    /// Native process identifier type (DWORD on Windows, pid_t on POSIX).
    using process_id_type   = DWORD;
#else
    /// Native OS handle type for pipe endpoints (int fd on POSIX).
    using native_handle_type = int;
    /// Native process identifier type (pid_t on POSIX).
    using process_id_type   = pid_t;
#endif

    /// @brief RAII wrapper for a native OS handle / file descriptor.
    ///
    /// On POSIX, wraps an `int` file descriptor and calls `close()`.
    /// On Windows, wraps a `HANDLE` and calls `CloseHandle()`.
    /// Move-only.
    struct FileDescriptor
    {
#ifdef _WIN32
        HANDLE handle{nullptr};

        FileDescriptor() = default;
        explicit FileDescriptor(HANDLE h) : handle{h} {}
#else
        int fd{-1};

        FileDescriptor() = default;
        explicit FileDescriptor(int descriptor) : fd{descriptor} {}
#endif

        ~FileDescriptor()
        {
            reset();
        }

        FileDescriptor(const FileDescriptor&) = delete;
        FileDescriptor& operator=(const FileDescriptor&) = delete;

        FileDescriptor(FileDescriptor&& other) noexcept
#ifdef _WIN32
            : handle{std::exchange(other.handle, nullptr)}
#else
            : fd{std::exchange(other.fd, -1)}
#endif
        {
        }

        FileDescriptor& operator=(FileDescriptor&& other) noexcept
        {
            if (this != &other)
            {
                reset();
#ifdef _WIN32
                handle = std::exchange(other.handle, nullptr);
#else
                fd = std::exchange(other.fd, -1);
#endif
            }
            return *this;
        }

        void reset() noexcept
        {
#ifdef _WIN32
            if (handle != nullptr && handle != INVALID_HANDLE_VALUE)
            {
                CloseHandle(handle);
                handle = nullptr;
            }
#else
            if (fd != -1)
            {
                close(fd);
                fd = -1;
            }
#endif
        }

        [[nodiscard]] native_handle_type get() const noexcept
        {
#ifdef _WIN32
            return handle;
#else
            return fd;
#endif
        }
    };

    // ----------------------------------------------------------------
    //  Windows-only detail helpers
    // ----------------------------------------------------------------
#ifdef _WIN32
    namespace detail
    {
        /// @brief Formats a Windows system error code as a human-readable string.
        [[nodiscard]] inline std::string get_last_error_string(
            DWORD error = GetLastError()
        )
        {
            if (error == 0)
            {
                return "No error";
            }

            LPSTR buffer = nullptr;
            const DWORD size = FormatMessageA(
                FORMAT_MESSAGE_ALLOCATE_BUFFER |
                FORMAT_MESSAGE_FROM_SYSTEM     |
                FORMAT_MESSAGE_IGNORE_INSERTS,
                nullptr,
                error,
                MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
                reinterpret_cast<LPSTR>(&buffer),
                0,
                nullptr
            );

            std::string message;
            if (size > 0 && buffer != nullptr)
            {
                message.assign(buffer, size);
                // Strip trailing CR/LF/spaces that FormatMessage appends.
                while (!message.empty() &&
                       (message.back() == '\r' ||
                        message.back() == '\n' ||
                        message.back() == ' '))
                {
                    message.pop_back();
                }
            }
            else
            {
                message = std::format("Unknown error ({})", error);
            }

            if (buffer != nullptr)
            {
                LocalFree(buffer);
            }

            return message;
        }

        /// @brief Appends a Windows command-line-quoted argument to `out`.
        ///
        /// Wraps the argument in double quotes and escapes embedded
        /// double-quotes with a backslash.  This is sufficient for the
        /// typical case but is NOT a fully CRT-conformant quoting routine
        /// (which would also need to handle backslash sequences before
        /// quotes).  Suitable for controlled argument sets.
        inline void append_quoted(std::string& out, std::string_view arg)
        {
            out.push_back('"');
            for (char c : arg)
            {
                if (c == '"')
                {
                    out.push_back('\\');
                }
                out.push_back(c);
            }
            out.push_back('"');
        }
    }
#endif // _WIN32

    /// @brief Cross-platform process management for C++23.
    ///
    /// Launches a child process with piped stdin/stdout, exposes native
    /// handles for async I/O, and provides wait/terminate/exit_code
    /// lifecycle control.  RAII — the destructor calls terminate() + wait()
    /// if the process is still running.
    ///
    /// On POSIX, stderr is inherited from the parent (not piped).
    /// On Windows, stderr is inherited from the parent's STD_ERROR_HANDLE.
    /// If the parent has no console, the child's stderr may be invalid.
    ///
    /// @note On Windows, `running()` relies on GetExitCodeProcess() which
    ///       returns STILL_ACTIVE (259) for live processes.  If a child
    ///       legitimately exits with code 259, it will be misreported as
    ///       still running — a known Win32 limitation.
    class Process final
    {
    public:
        using Arguments = std::vector<std::string>;

        Process() = default;

        Process(const Process&) = delete;
        Process& operator=(const Process&) = delete;

        Process(Process&& other) noexcept
            : pid_{std::exchange(other.pid_, invalid_pid())}
            , stdin_write_{std::exchange(other.stdin_write_, FileDescriptor{})}
            , stdout_read_{std::exchange(other.stdout_read_, FileDescriptor{})}
            , exit_code_{std::exchange(other.exit_code_, -1)}
#ifdef _WIN32
            , process_handle_{std::exchange(other.process_handle_, FileDescriptor{})}
#endif
        {
        }

        Process& operator=(Process&& other) noexcept
        {
            if (this != &other)
            {
                reset();

                pid_          = std::exchange(other.pid_, invalid_pid());
                stdin_write_  = std::exchange(other.stdin_write_, FileDescriptor{});
                stdout_read_  = std::exchange(other.stdout_read_, FileDescriptor{});
                exit_code_    = std::exchange(other.exit_code_, -1);
#ifdef _WIN32
                process_handle_ = std::exchange(other.process_handle_, FileDescriptor{});
#endif
            }

            return *this;
        }

        ~Process()
        {
            reset();
        }

        // ----------------------------------------------------------------
        //  Lifecycle
        // ----------------------------------------------------------------

        /// @brief Launches a child process with piped stdin/stdout.
        /// @param executable  path or name of the program to run.
        /// @param arguments   command-line arguments (excluding argv[0]).
        /// @throws std::logic_error if a process is already running.
        /// @throws std::runtime_error on OS-level failure.
        void start(
            std::string_view executable,
            std::span<const std::string> arguments = {}
        )
        {
            if (running())
            {
                throw std::logic_error{
                    "Process is already running."
                };
            }

            reset(); // Clean up any previous (already-exited) state.

#ifdef _WIN32
            start_windows(executable, arguments);
#else
            start_posix(executable, arguments);
#endif
        }

        /// @brief Convenience overload — accepts a braced initializer list.
        void start(
            std::string_view executable,
            std::initializer_list<std::string_view> arguments
        )
        {
            std::vector<std::string> args{arguments.begin(), arguments.end()};
            start(executable, std::span<const std::string>{args});
        }

        // ----------------------------------------------------------------
        //  Native handle access (for AsyncPipe / direct I/O)
        // ----------------------------------------------------------------

        /// @brief Returns the native write-end of the child's stdin pipe.
        ///
        /// On POSIX this is an `int` file descriptor suitable for
        /// epoll/io_uring.  On Windows this is a `HANDLE` suitable for
        /// ReadFile/WriteFile with IOCP.
        [[nodiscard]] native_handle_type get_stdin_write() const noexcept
        {
            return stdin_write_.get();
        }

        /// @brief Returns the native read-end of the child's stdout pipe.
        [[nodiscard]] native_handle_type get_stdout_read() const noexcept
        {
            return stdout_read_.get();
        }

        /// @brief Closes the write-end of the child's stdin pipe.
        ///
        /// Signals EOF to the child so that programs like `cat` or
        /// `grep` that read until end-of-input can exit cleanly.
        void close_stdin() noexcept
        {
            stdin_write_.reset();
        }

        // ----------------------------------------------------------------
        //  Cross-Platform Blocking I/O (Ideal for Thread Pool Bridging)
        // ----------------------------------------------------------------

        /// @brief Reads data from the child's stdout pipe (blocking).
        ///
        /// Wraps POSIX `::read` and Windows `ReadFile`. This will block
        /// the calling thread until data is available or the pipe is closed.
        /// Perfect for dispatching via `pool.run_blocking()`.
        std::expected<std::size_t, std::error_code> read_stdout(std::span<std::byte> buffer) noexcept
        {
            std::expected<std::size_t, std::error_code> result{};
#ifdef _WIN32
            DWORD bytes_read = 0;
            BOOL success = ReadFile(
                stdout_read_.get(),
                buffer.data(),
                static_cast<DWORD>(buffer.size()),
                &bytes_read,
                nullptr
            );
            if (success)
            {
                result = static_cast<std::size_t>(bytes_read);
            }
            else
            {
                DWORD err = GetLastError();
                if (err == ERROR_BROKEN_PIPE || err == ERROR_HANDLE_EOF)
                {
                    result = static_cast<std::size_t>(0); // EOF
                }
                else
                {
                    result = std::unexpected(std::error_code{static_cast<int>(err), std::system_category()});
                }
            }
#else
            ssize_t bytes_read = ::read(stdout_read_.get(), buffer.data(), buffer.size());
            if (bytes_read >= 0)
            {
                result = static_cast<std::size_t>(bytes_read);
            }
            else
            {
                result = std::unexpected(std::error_code{errno, std::system_category()});
            }
#endif
            return result;
        }

        /// @brief Writes data to the child's stdin pipe (blocking).
        ///
        /// Wraps POSIX `::write` and Windows `WriteFile`. This will block
        /// the calling thread if the pipe buffer is full.
        std::expected<std::size_t, std::error_code> write_stdin(std::span<const std::byte> buffer) noexcept
        {
            std::expected<std::size_t, std::error_code> result{};
#ifdef _WIN32
            DWORD bytes_written = 0;
            BOOL success = WriteFile(
                stdin_write_.get(),
                buffer.data(),
                static_cast<DWORD>(buffer.size()),
                &bytes_written,
                nullptr
            );
            if (success)
            {
                result = static_cast<std::size_t>(bytes_written);
            }
            else
            {
                result = std::unexpected(std::error_code{static_cast<int>(GetLastError()), std::system_category()});
            }
#else
            ssize_t bytes_written = ::write(stdin_write_.get(), buffer.data(), buffer.size());
            if (bytes_written >= 0)
            {
                result = static_cast<std::size_t>(bytes_written);
            }
            else
            {
                result = std::unexpected(std::error_code{errno, std::system_category()});
            }
#endif
            return result;
        }

        // ----------------------------------------------------------------
        //  Status & lifecycle queries
        // ----------------------------------------------------------------

        /// @brief Checks whether the child process is still running.
        ///
        /// On POSIX, uses waitpid(WNOHANG) and reaps the zombie if the
        /// child has exited.  On Windows, uses GetExitCodeProcess() and
        /// checks for STILL_ACTIVE.
        ///
        /// @return true if the child is still running; false if it exited
        ///         or was never started.
        bool running() noexcept
        {
#ifdef _WIN32
            return running_windows();
#else
            return running_posix();
#endif
        }

        /// @brief Waits indefinitely for the child to exit.
        /// @return the process exit code.
        /// @throws std::logic_error if no process was started.
        /// @throws std::runtime_error on OS-level failure.
        int wait()
        {
#ifdef _WIN32
            return wait_windows();
#else
            return wait_posix();
#endif
        }

        /// @brief Gracefully terminates the child process and waits for it.
        ///
        /// On POSIX, sends SIGTERM then reaps with waitpid.
        /// On Windows, calls TerminateProcess() then WaitForSingleObject().
        /// This method is noexcept-safe — errors are swallowed to allow
        /// safe use in destructors and cleanup paths.
        void terminate() noexcept
        {
#ifdef _WIN32
            terminate_windows();
#else
            terminate_posix();
#endif
        }

        /// @brief Returns the last known exit code.
        /// @return the exit code, or -1 if the process hasn't exited yet.
        [[nodiscard]] int exit_code() const noexcept
        {
            return exit_code_;
        }

        /// @brief Returns the OS-native process identifier (PID).
        [[nodiscard]] process_id_type pid() const noexcept
        {
            return pid_;
        }

    private:
        // Sentinel "no process" value for pid_.
        static constexpr process_id_type invalid_pid() noexcept
        {
#ifdef _WIN32
            return 0;   // PID 0 is the idle process, never a user child.
#else
            return -1;
#endif
        }

        process_id_type  pid_{invalid_pid()};
        FileDescriptor   stdin_write_{};
        FileDescriptor   stdout_read_{};
        int              exit_code_{-1};
#ifdef _WIN32
        /// Open handle to the child process — required for
        /// WaitForSingleObject / GetExitCodeProcess / TerminateProcess.
        FileDescriptor   process_handle_{};
#endif

        /// @brief Terminates the child if still running, then releases all resources.
        void reset() noexcept
        {
#ifdef _WIN32
            if (process_handle_.get() != nullptr)
            {
                // Best-effort terminate + reap.
                TerminateProcess(process_handle_.get(), 1);
                WaitForSingleObject(process_handle_.get(), INFINITE);
            }
            process_handle_.reset();
            pid_ = invalid_pid();
#else
            if (pid_ > 0)
            {
                kill(pid_, SIGTERM);
                int status{};
                waitpid(pid_, &status, 0);
            }
            pid_ = invalid_pid();
#endif
            stdin_write_.reset();
            stdout_read_.reset();
        }

        // ================================================================
        //  Windows implementation
        // ================================================================
#ifdef _WIN32
        void start_windows(
            std::string_view executable,
            std::span<const std::string> arguments
        )
        {
            SECURITY_ATTRIBUTES sa{};
            sa.nLength              = sizeof(sa);
            sa.bInheritHandle       = TRUE;   // child ends must be inheritable
            sa.lpSecurityDescriptor = nullptr;

            HANDLE child_stdin_read  = nullptr;
            HANDLE child_stdin_write = nullptr;
            HANDLE child_stdout_read = nullptr;
            HANDLE child_stdout_write = nullptr;

            if (!CreatePipe(&child_stdin_read, &child_stdin_write, &sa, 0))
            {
                throw std::runtime_error{
                    std::format(
                        "CreatePipe() failed: {}",
                        detail::get_last_error_string()
                    )
                };
            }

            // Wrap ends immediately for RAII cleanup on any error path.
            FileDescriptor stdin_read {child_stdin_read};
            FileDescriptor stdin_write{child_stdin_write};

            // Parent's write-end must NOT be inherited by the child.
            if (!SetHandleInformation(
                    stdin_write.get(),
                    HANDLE_FLAG_INHERIT,
                    0))
            {
                throw std::runtime_error{
                    std::format(
                        "SetHandleInformation() failed: {}",
                        detail::get_last_error_string()
                    )
                };
            }

            if (!CreatePipe(&child_stdout_read, &child_stdout_write, &sa, 0))
            {
                throw std::runtime_error{
                    std::format(
                        "CreatePipe() failed: {}",
                        detail::get_last_error_string()
                    )
                };
            }

            FileDescriptor stdout_read {child_stdout_read};
            FileDescriptor stdout_write{child_stdout_write};

            // Parent's read-end must NOT be inherited by the child.
            if (!SetHandleInformation(
                    stdout_read.get(),
                    HANDLE_FLAG_INHERIT,
                    0))
            {
                throw std::runtime_error{
                    std::format(
                        "SetHandleInformation() failed: {}",
                        detail::get_last_error_string()
                    )
                };
            }

            // Build the Windows command line.  We quote every argument
            // to keep parsing deterministic.  Note: this simple escaper
            // does not handle backslash-quote edge cases per the MSVCRT
            // rules; for arbitrary argument sets consider a stricter
            // implementation.
            std::string command_line;
            command_line.reserve(executable.size() + 1);
            detail::append_quoted(command_line, executable);

            for (const auto& argument : arguments)
            {
                command_line.push_back(' ');
                detail::append_quoted(command_line, argument);
            }

            STARTUPINFOA si{};
            si.cb         = sizeof(si);
            si.hStdInput  = stdin_read.get();
            si.hStdOutput = stdout_write.get();
            si.hStdError  = GetStdHandle(STD_ERROR_HANDLE);  // inherited
            si.dwFlags    = STARTF_USESTDHANDLES;

            PROCESS_INFORMATION pi{};

            // CreateProcessA requires a mutable command-line buffer.
            std::vector<char> mutable_cmd(command_line.begin(),
                                          command_line.end());
            mutable_cmd.push_back('\0');

            const BOOL ok = CreateProcessA(
                nullptr,               // lpApplicationName — use command line
                mutable_cmd.data(),    // lpCommandLine (mutated by CreateProcess)
                nullptr,               // lpProcessAttributes
                nullptr,               // lpThreadAttributes
                TRUE,                  // bInheritHandles — inherit pipe ends
                0,                     // dwCreationFlags (CREATE_NO_WINDOW optional)
                nullptr,               // lpEnvironment (inherit parent's)
                nullptr,               // lpCurrentDirectory (inherit parent's)
                &si,
                &pi
            );

            if (!ok)
            {
                throw std::runtime_error{
                    std::format(
                        "CreateProcessA() failed: {}",
                        detail::get_last_error_string()
                    )
                };
            }

            // Close child-side pipe ends in the parent — they were
            // duplicated into the child by CreateProcess.
            stdin_read.reset();
            stdout_write.reset();

            // Thread handle is not needed; close it immediately to
            // avoid a handle leak.
            CloseHandle(pi.hThread);

            stdin_write_     = std::move(stdin_write);
            stdout_read_     = std::move(stdout_read);
            process_handle_  = FileDescriptor{pi.hProcess};
            pid_             = pi.dwProcessId;
            exit_code_       = -1;
        }

        bool running_windows() noexcept
        {
            bool result = false;

            if (process_handle_.get() != nullptr)
            {
                DWORD exit_code = 0;
                if (GetExitCodeProcess(process_handle_.get(), &exit_code))
                {
                    if (exit_code == STILL_ACTIVE)
                    {
                        result = true;
                    }
                    else
                    {
                        exit_code_ = static_cast<int>(exit_code);
                        process_handle_.reset();
                        stdin_write_.reset();
                        stdout_read_.reset();
                        pid_ = invalid_pid();
                    }
                }
                // On GetExitCodeProcess failure, treat as still running
                // to avoid losing the handle — caller can retry.
            }

            return result;
        }

        int wait_windows()
        {
            if (process_handle_.get() == nullptr)
            {
                if (exit_code_ != -1)
                {
                    return exit_code_;
                }
                throw std::logic_error{"No process is running."};
            }

            const DWORD wait_result =
                WaitForSingleObject(process_handle_.get(), INFINITE);

            if (wait_result != WAIT_OBJECT_0)
            {
                throw std::runtime_error{
                    std::format(
                        "WaitForSingleObject() failed: {}",
                        detail::get_last_error_string()
                    )
                };
            }

            DWORD exit_code = 0;
            if (!GetExitCodeProcess(process_handle_.get(), &exit_code))
            {
                throw std::runtime_error{
                    std::format(
                        "GetExitCodeProcess() failed: {}",
                        detail::get_last_error_string()
                    )
                };
            }

            exit_code_ = static_cast<int>(exit_code);
            process_handle_.reset();
            stdin_write_.reset();
            stdout_read_.reset();
            pid_ = invalid_pid();

            return exit_code_;
        }

        void terminate_windows() noexcept
        {
            if (process_handle_.get() == nullptr)
            {
                return;
            }

            // Best-effort terminate.  TerminateProcess is unconditional
            // (no SIGTERM-equivalent graceful shutdown on Windows).
            TerminateProcess(process_handle_.get(), 1);

            // Best-effort reap — ignore failures.
            if (WaitForSingleObject(process_handle_.get(), INFINITE)
                    == WAIT_OBJECT_0)
            {
                DWORD exit_code = 0;
                if (GetExitCodeProcess(process_handle_.get(), &exit_code))
                {
                    exit_code_ = static_cast<int>(exit_code);
                }
            }

            reset();
        }
#endif // _WIN32

        // ================================================================
        //  POSIX implementation
        // ================================================================
#ifndef _WIN32
        void start_posix(
            std::string_view executable,
            std::span<const std::string> arguments
        )
        {
            int stdin_pipe[2]  = {-1, -1};
            int stdout_pipe[2] = {-1, -1};

            if (pipe(stdin_pipe) != 0)
            {
                throw std::runtime_error{
                    std::format("pipe() failed: {}", std::strerror(errno))
                };
            }

            // Wrap stdin pipe ends immediately for RAII cleanup on error.
            FileDescriptor stdin_read {stdin_pipe[0]};
            FileDescriptor stdin_write{stdin_pipe[1]};

            if (pipe(stdout_pipe) != 0)
            {
                throw std::runtime_error{
                    std::format("pipe() failed: {}", std::strerror(errno))
                };
            }

            // Wrap stdout pipe ends immediately for RAII cleanup on error.
            FileDescriptor stdout_read {stdout_pipe[0]};
            FileDescriptor stdout_write{stdout_pipe[1]};

            pid_t child = fork();

            if (child < 0)
            {
                throw std::runtime_error{
                    std::format("fork() failed: {}", std::strerror(errno))
                };
            }

            if (child == 0)
            {
                // ---- Child process ----

                // Redirect stdin and stdout.  Check return values — if
                // dup2 fails, the child must exit immediately.
                if (dup2(stdin_read.get(), STDIN_FILENO) < 0 ||
                    dup2(stdout_write.get(), STDOUT_FILENO) < 0)
                {
                    _exit(126); // 126 = "cannot execute" (permission/setup error)
                }

                // Close all original pipe descriptors in the child —
                // stdin_read and stdout_write are now duplicated as
                // STDIN_FILENO and STDOUT_FILENO.
                stdin_read.reset();
                stdin_write.reset();
                stdout_read.reset();
                stdout_write.reset();

                // Build argv array for execvp.
                const std::string executable_copy{executable};
                std::vector<char*> argv{};

                argv.reserve(arguments.size() + 2);

                argv.push_back(
                    const_cast<char*>(executable_copy.c_str())
                );

                for (const auto& argument : arguments)
                {
                    argv.push_back(
                        const_cast<char*>(argument.c_str())
                    );
                }

                argv.push_back(nullptr);

                execvp(executable_copy.c_str(), argv.data());

                // execvp only returns on failure — exit with 127
                // (standard "command not found" convention).
                _exit(127);
            }

            // ---- Parent process ----
            // Close child-side pipe ends (already duplicated in child).
            stdin_read.reset();
            stdout_write.reset();

            stdin_write_ = std::move(stdin_write);
            stdout_read_ = std::move(stdout_read);
            pid_         = child;
            exit_code_   = -1;
        }

        bool running_posix() noexcept
        {
            bool result = false;

            if (pid_ > 0)
            {
                int status{};
                const auto wait_result = waitpid(pid_, &status, WNOHANG);

                if (wait_result == 0)
                {
                    // Still running.
                    result = true;
                }
                else if (wait_result > 0)
                {
                    // Child has exited — reap and update state.
                    if (WIFEXITED(status))
                    {
                        exit_code_ = WEXITSTATUS(status);
                    }
                    else if (WIFSIGNALED(status))
                    {
                        exit_code_ = 128 + WTERMSIG(status);
                    }
                    else
                    {
                        exit_code_ = -1;
                    }

                    pid_ = invalid_pid();
                    stdin_write_.reset();
                    stdout_read_.reset();
                }
                // else: wait_result < 0 (error, e.g. ECHILD) — treat as not running.
            }

            return result;
        }

        int wait_posix()
        {
            // If already reaped by running(), return the cached exit code.
            if (pid_ <= 0)
            {
                if (exit_code_ != -1)
                {
                    return exit_code_;
                }
                throw std::logic_error{
                    "No process is running."
                };
            }

            int status{};

            const auto result =
                waitpid(
                    pid_,
                    &status,
                    0
                );

            if (result < 0)
            {
                throw std::runtime_error{
                    std::format(
                        "waitpid() failed: {}",
                        std::strerror(errno)
                    )
                };
            }

            if (WIFEXITED(status))
            {
                exit_code_ = WEXITSTATUS(status);
            }
            else if (WIFSIGNALED(status))
            {
                exit_code_ = 128 + WTERMSIG(status);
            }
            else
            {
                exit_code_ = -1;
            }

            pid_ = invalid_pid();
            stdin_write_.reset();
            stdout_read_.reset();

            return exit_code_;
        }

        void terminate_posix() noexcept
        {
            if (pid_ <= 0)
            {
                return;
            }

            // Best-effort SIGTERM — ignore ESRCH (already exited).
            if (kill(pid_, SIGTERM) != 0 && errno != ESRCH)
            {
                // Fall through to waitpid even on error.
            }

            // Best-effort reap — ignore failures.
            int status{};
            if (waitpid(pid_, &status, 0) > 0)
            {
                if (WIFEXITED(status))
                {
                    exit_code_ = WEXITSTATUS(status);
                }
                else if (WIFSIGNALED(status))
                {
                    exit_code_ = 128 + WTERMSIG(status);
                }
            }

            reset();
        }
#endif // !_WIN32
    };
}