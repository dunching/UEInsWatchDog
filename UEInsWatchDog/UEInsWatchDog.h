// ============================================================
// UE Multi-Port WatchDog
// ============================================================

#include <windows.h>
#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <algorithm>
#include <chrono>

// ======================= 配置结构 =======================

struct InstanceConfig
{
    int Port = 0;
    DWORD_PTR CpuAffinityMask = 0;
};

struct Config
{
    std::wstring ExePath;
    std::wstring StartProName;
    std::wstring RealName;
    std::wstring Arguments;

    int RestartDelaySeconds = 3;
    int MaxRestartCount = 5;
    int RestartTimeWindowSeconds = 60;
    int HeartbeatTimeoutSeconds = 10;
    int HeartbeatCheckIntervalSeconds = 1;
    ULONGLONG StartTime = 1000 * 60 * 2;

    std::vector<InstanceConfig> Instances;
};

// ======================= JSON加载 =======================

Config LoadConfig();

// ======================= 共享心跳 =======================

struct SharedData
{
    uint32_t Port;
    ULONGLONG LastHeartbeat = 0.f;
};

struct RunningInstance
{
    InstanceConfig Config;
    
    PROCESS_INFORMATION ProcessInfo;
    
    ULONGLONG StartTime = 0;
    SharedData* shared = nullptr;
    HANDLE hMap = nullptr;
};

HANDLE CreateSharedMemory(
    SharedData** data,
    const InstanceConfig& RunningInstance
    );

// ======================= 启动进程 =======================

bool LaunchInstance(
    const Config& cfg,
    const InstanceConfig& inst,
    PROCESS_INFORMATION& pi,
    RunningInstance& run
    );

std::vector<DWORD> GetAllUEProcesses(const std::wstring& exeName);
