// ============================================================================
//  mcp_system_info_server.cpp — MCP STDIO Server: System Information
//  Developed by: Pooria Yousefi
//  License: Apache 2.0
//
//  A standalone MCP server that provides system environment information.
//  Communicates via newline-delimited JSON-RPC 2.0 over stdin/stdout.
//
//  Build: clang++ -std=c++23 -O3 -I include src/mcp_system_info_server.cpp -o bin/mcp_system_info_server
// ============================================================================
#include "poorimcp.hpp"

#include <chrono>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#  define WIN32_LEAN_AND_MEAN
#  define NOMINMAX
#  include <windows.h>
#else
#  include <unistd.h>
#  include <sys/statvfs.h>
#  if defined(__APPLE__)
#    include <sys/sysctl.h>
#    include <mach/mach.h>
#  elif defined(__linux__)
#    include <sys/sysinfo.h>
#  endif
extern char** environ;
#endif

using namespace pooriayousefi::mcp;
using namespace pooriayousefi::core;
using namespace pooriayousefi::json;

namespace pooriayousefi
{
    // Cross-platform helper to get memory info
    void get_memory_info(uint64_t& total, uint64_t& available)
    {
        total = 0;
        available = 0;

#ifdef _WIN32
        MEMORYSTATUSEX memInfo;
        memInfo.dwLength = sizeof(MEMORYSTATUSEX);
        if (GlobalMemoryStatusEx(&memInfo))
        {
            total = memInfo.ullTotalPhys;
            available = memInfo.ullAvailPhys;
        }
#elif defined(__APPLE__)
        int mib[2];
        mib[0] = CTL_HW;
        mib[1] = HW_MEMSIZE;
        size_t length = sizeof(total);
        sysctl(mib, 2, &total, &length, nullptr, 0);

        vm_statistics64_data_t vm_stat;
        unsigned int count = HOST_VM_INFO64_COUNT;
        if (host_statistics64(mach_host_self(), HOST_VM_INFO64, reinterpret_cast<host_info64_t>(&vm_stat), &count) == KERN_SUCCESS)
        {
            available = (vm_stat.free_count + vm_stat.inactive_count) * vm_page_size;
        }
#elif defined(__linux__)
        struct sysinfo memInfo;
        if (sysinfo(&memInfo) == 0)
        {
            total = memInfo.totalram * memInfo.mem_unit;
            available = (memInfo.freeram + memInfo.bufferram) * memInfo.mem_unit;
        }
#endif
    }

    // Cross-platform helper to get CPU usage (blocks for 100ms to sample)
    double get_cpu_usage()
    {
        double usage = 0.0;

#ifdef _WIN32
        FILETIME idleTime, kernelTime, userTime;
        FILETIME idleTime2, kernelTime2, userTime2;
        GetSystemTimes(&idleTime, &kernelTime, &userTime);
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        GetSystemTimes(&idleTime2, &kernelTime2, &userTime2);

        auto convert = [](FILETIME ft) -> uint64_t
        {
            ULARGE_INTEGER li;
            li.LowPart = ft.dwLowDateTime;
            li.HighPart = ft.dwHighDateTime;
            return li.QuadPart;
        };

        uint64_t sys_idle = convert(idleTime2) - convert(idleTime);
        uint64_t sys_kernel = convert(kernelTime2) - convert(kernelTime);
        uint64_t sys_user = convert(userTime2) - convert(userTime);
        uint64_t sys_total = sys_kernel + sys_user;
        if (sys_total > 0)
        {
            usage = static_cast<double>(sys_total - sys_idle) / sys_total * 100.0;
        }
#elif defined(__APPLE__)
        host_cpu_load_info_data_t cpuinfo;
        mach_msg_type_number_t count = HOST_CPU_LOAD_INFO_COUNT;
        host_statistics(mach_host_self(), HOST_CPU_LOAD_INFO, reinterpret_cast<host_info_t>(&cpuinfo), &count);
        
        uint64_t total1 = cpuinfo.cpu_ticks[CPU_STATE_USER] + cpuinfo.cpu_ticks[CPU_STATE_SYSTEM] + cpuinfo.cpu_ticks[CPU_STATE_NICE] + cpuinfo.cpu_ticks[CPU_STATE_IDLE];
        uint64_t idle1 = cpuinfo.cpu_ticks[CPU_STATE_IDLE];

        std::this_thread::sleep_for(std::chrono::milliseconds(100));

        host_statistics(mach_host_self(), HOST_CPU_LOAD_INFO, reinterpret_cast<host_info_t>(&cpuinfo), &count);
        uint64_t total2 = cpuinfo.cpu_ticks[CPU_STATE_USER] + cpuinfo.cpu_ticks[CPU_STATE_SYSTEM] + cpuinfo.cpu_ticks[CPU_STATE_NICE] + cpuinfo.cpu_ticks[CPU_STATE_IDLE];
        uint64_t idle2 = cpuinfo.cpu_ticks[CPU_STATE_IDLE];

        if (total2 - total1 > 0)
        {
            usage = static_cast<double>((total2 - total1) - (idle2 - idle1)) / (total2 - total1) * 100.0;
        }
#elif defined(__linux__)
        auto get_proc_stat = []() -> std::pair<uint64_t, uint64_t>
        {
            FILE* fp = fopen("/proc/stat", "r");
            if (!fp) return {0, 0};
            char buf[1024];
            fgets(buf, sizeof(buf), fp);
            fclose(fp);
            unsigned long long user, nice, system, idle, iowait, irq, softirq, steal;
            int parsed = sscanf(buf, "cpu %llu %llu %llu %llu %llu %llu %llu %llu",
                                &user, &nice, &system, &idle, &iowait, &irq, &softirq, &steal);
            uint64_t total = user + nice + system + idle + iowait + irq + softirq + steal;
            return {total, idle};
        };
        
        auto [total1, idle1] = get_proc_stat();
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        auto [total2, idle2] = get_proc_stat();
        
        if (total2 - total1 > 0)
        {
            usage = static_cast<double>((total2 - total1) - (idle2 - idle1)) / (total2 - total1) * 100.0;
        }
#endif
        return usage;
    }

    // Cross-platform helper to get disk usage
    bool get_disk_info(const std::string& path, uint64_t& total, uint64_t& free)
    {
        total = 0;
        free = 0;

#ifdef _WIN32
        ULARGE_INTEGER freeBytesAvailable, totalNumberOfBytes, totalNumberOfFreeBytes;
        if (GetDiskFreeSpaceExA(path.c_str(), &freeBytesAvailable, &totalNumberOfBytes, &totalNumberOfFreeBytes))
        {
            total = totalNumberOfBytes.QuadPart;
            free = freeBytesAvailable.QuadPart;
            return true;
        }
        return false;
#else
        struct statvfs stat;
        if (statvfs(path.c_str(), &stat) == 0)
        {
            total = stat.f_blocks * stat.f_frsize;
            free = stat.f_bavail * stat.f_frsize;
            return true;
        }
        return false;
#endif
    }

    // Cross-platform helper to get environment variables
    JSON get_env_vars()
    {
        JSON result = JSONObject{};

#ifdef _WIN32
        char* env_block = GetEnvironmentStringsA();
        if (!env_block) return result;
        
        // Rule #3: Use smart pointers instead of raw pointers.
        std::unique_ptr<char, decltype(&FreeEnvironmentStringsA)> env_ptr(env_block, &FreeEnvironmentStringsA);
        
        char* p = env_block;
        while (*p != '\0')
        {
            std::string entry(p);
            size_t pos = entry.find('=');
            if (pos != std::string::npos)
            {
                std::string key = entry.substr(0, pos);
                std::string val = entry.substr(pos + 1);
                if (!key.empty())
                {
                    result[key] = val;
                }
            }
            p += entry.length() + 1;
        }
#else
        char** p = environ;
        while (*p != nullptr)
        {
            std::string entry(*p);
            size_t pos = entry.find('=');
            if (pos != std::string::npos)
            {
                std::string key = entry.substr(0, pos);
                std::string val = entry.substr(pos + 1);
                if (!key.empty())
                {
                    result[key] = val;
                }
            }
            p++;
        }
#endif
        return result;
    }
}

// ---- System Info Tool Handlers ------------------------------------------------

AsyncTask<JSON> handle_get_system_resources(const JSON& args)
{
    JSON result{};
    
    JSON info;
    info["cpu_cores"] = static_cast<double>(std::thread::hardware_concurrency());

    uint64_t total_ram = 0;
    uint64_t avail_ram = 0;
    pooriayousefi::get_memory_info(total_ram, avail_ram);
    info["total_memory_bytes"] = static_cast<double>(total_ram);
    info["available_memory_bytes"] = static_cast<double>(avail_ram);

    double cpu_usage = pooriayousefi::get_cpu_usage();
    info["cpu_usage_percent"] = cpu_usage;

    result["result"] = std::move(info);
    co_return result;
}

AsyncTask<JSON> handle_get_disk_usage(const JSON& args)
{
    JSON result{};
    std::string path = ".";
    if (args.contains("path") && args["path"].is_string())
    {
        path = args["path"].get_string();
    }

    uint64_t total_space = 0;
    uint64_t free_space = 0;
    if (!pooriayousefi::get_disk_info(path, total_space, free_space))
    {
        result["error"] = "Failed to get disk usage for path: " + path;
    }
    else
    {
        JSON disk_info;
        disk_info["path"] = path;
        disk_info["total_space_bytes"] = static_cast<double>(total_space);
        disk_info["free_space_bytes"] = static_cast<double>(free_space);
        result["result"] = std::move(disk_info);
    }
    co_return result;
}

AsyncTask<JSON> handle_get_environment_variables(const JSON& args)
{
    JSON result{};
    JSON env_vars = pooriayousefi::get_env_vars();
    result["result"] = std::move(env_vars);
    co_return result;
}

// ---- Main entry point -------------------------------------------------------

int main()
{
    int result{EXIT_SUCCESS};

    std::cerr << "system_info_server (MCP STDIO) running...\n";

    MCPServer server;

    // Tool 1: get_system_resources
    MCPTool tool1;
    tool1.name = "get_system_resources";
    tool1.description = "Gets system resources including CPU core count, current CPU usage percentage (may take ~100ms to sample), and total/available RAM in bytes.";
    tool1.parameters_schema = pooriayousefi::json::parse(R"({"type":"object","properties":{}})").value_or(JSON{});
    server.register_tool(std::move(tool1), handle_get_system_resources);

    // Tool 2: get_disk_usage
    MCPTool tool2;
    tool2.name = "get_disk_usage";
    tool2.description = "Gets disk usage for a specified path. Returns total and free space in bytes. Defaults to current directory.";
    tool2.parameters_schema = pooriayousefi::json::parse(R"({"type":"object","properties":{"path":{"type":"string","description":"The directory path to check. Defaults to current directory."}}})").value_or(JSON{});
    server.register_tool(std::move(tool2), handle_get_disk_usage);

    // Tool 3: get_environment_variables
    MCPTool tool3;
    tool3.name = "get_environment_variables";
    tool3.description = "Retrieves all system environment variables as a key-value JSON object. Useful for finding paths like JAVA_HOME or PYTHONPATH.";
    tool3.parameters_schema = pooriayousefi::json::parse(R"({"type":"object","properties":{}})").value_or(JSON{});
    server.register_tool(std::move(tool3), handle_get_environment_variables);

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