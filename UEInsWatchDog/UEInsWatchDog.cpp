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
#include <tlhelp32.h>

#include "UEInsWatchDog.h"

#pragma comment(lib, "advapi32.lib")

Config LoadConfig()
{
    Config cfg;

    std::ifstream file("D:\\WorkSpace\\OutDir\\SmartCity\\watchdog.json");
    if (!file.is_open())
    {
        std::cout << "Open watchdog.json failed\n";
        return cfg;
    }

    std::wstring content(
                         (std::istreambuf_iterator<char>(file)),
                         std::istreambuf_iterator<char>()
                        );

    auto GetValue = [&](
        const std::wstring& key
        )
    {
        size_t pos = content.find(L"\"" + key + L"\"");
        if (pos == std::wstring::npos)
            return std::wstring(L"");

        size_t start = content.find(L":", pos) + 1;
        size_t end = content.find_first_of(L",}", start);

        std::wstring v = content.substr(start, end - start);
        v.erase(remove(v.begin(), v.end(), '\"'), v.end());
        // v.erase(remove(v.begin(), v.end(), ' '), v.end());
        return v;
    };

    cfg.ExePath = GetValue(L"ExePath");
    cfg.ExePath.erase(remove(cfg.ExePath.begin(), cfg.ExePath.end(), ' '), cfg.ExePath.end());

    cfg.StartProName = GetValue(L"StartProName");
    cfg.StartProName.erase(remove(cfg.StartProName.begin(), cfg.StartProName.end(), ' '), cfg.StartProName.end());

    cfg.RealName = GetValue(L"RealName");
    cfg.RealName.erase(remove(cfg.RealName.begin(), cfg.RealName.end(), ' '), cfg.RealName.end());
    
    cfg.Arguments = GetValue(L"Arguments");

    cfg.RestartDelaySeconds =
        std::stoi(GetValue(L"RestartDelaySeconds"));

    cfg.MaxRestartCount =
        std::stoi(GetValue(L"MaxRestartCount"));

    cfg.RestartTimeWindowSeconds =
        std::stoi(GetValue(L"RestartTimeWindowSeconds"));

    cfg.HeartbeatTimeoutSeconds =
        std::stoi(GetValue(L"HeartbeatTimeoutSeconds"));

    cfg.HeartbeatCheckIntervalSeconds =
        std::stoi(GetValue(L"HeartbeatCheckIntervalSeconds"));

    // 解析端口
    size_t pos = 0;
    while (true)
    {
        size_t keyStart = content.find(L"\"", pos);
        if (keyStart == std::wstring::npos)
            break;

        size_t keyEnd = content.find(L"\"", keyStart + 1);
        std::wstring key =
            content.substr(keyStart + 1, keyEnd - keyStart - 1);

        if (std::all_of(key.begin(), key.end(), ::isdigit))
        {
            InstanceConfig inst;
            inst.Port = std::stoi(key);

            size_t blockStart = content.find(L"{", keyEnd);
            size_t blockEnd = content.find(L"}", blockStart);

            std::wstring block =
                content.substr(blockStart, blockEnd - blockStart);

            size_t maskPos = block.find(L"CpuAffinityMask");
            if (maskPos != std::wstring::npos)
            {
                size_t s = block.find(L":", maskPos) + 1;
                size_t e = block.find_first_of(L",}", s);
                std::wstring mask = block.substr(s, e - s);
                mask.erase(remove(mask.begin(), mask.end(), ' '), mask.end());
                inst.CpuAffinityMask = std::stoull(mask);
            }

            cfg.Instances.push_back(inst);
            pos = blockEnd;
        }
        else
        {
            pos = keyEnd;
        }
    }

    return cfg;
}

HANDLE CreateSharedMemory(
    SharedData** data,
    const InstanceConfig& RunningInstance
    )
{
    std::wstring Str(L"Local\\UE_WATCHDOG_HEARTBEAT_");

    Str.append(std::to_wstring(RunningInstance.Port));

    HANDLE hMap = CreateFileMappingW(
                                     INVALID_HANDLE_VALUE,
                                     nullptr,
                                     PAGE_READWRITE,
                                     0,
                                     sizeof(SharedData),
                                     Str.data()
                                    );

    if (!hMap)
        return nullptr;

    *data = (SharedData*)MapViewOfFile(
                                       hMap,
                                       FILE_MAP_ALL_ACCESS,
                                       0,
                                       0,
                                       sizeof(SharedData)
                                      );

    return hMap;
}

bool LaunchInstance(
    const Config& cfg,
    const InstanceConfig& inst,
    PROCESS_INFORMATION& pi,
    RunningInstance& run
    )
{
    std::wstring exeFull =
        std::wstring(cfg.ExePath.begin(), cfg.ExePath.end()) +
        std::wstring(cfg.StartProName.begin(), cfg.StartProName.end());

    std::wstring cmd =
        L" -PixelStreamingPort=" +
        std::to_wstring(inst.Port) +
        L" " +
        std::wstring(
                     cfg.Arguments.begin(),
                     cfg.Arguments.end()
                    );

    std::vector<wchar_t> buffer(cmd.begin(), cmd.end());
    buffer.push_back(L'\0');

    STARTUPINFOW si = {sizeof(si)};

    BOOL result = CreateProcessW(
                                 exeFull.data(),
                                 buffer.data(),
                                 NULL,
                                 NULL,
                                 FALSE,
                                 CREATE_NEW_CONSOLE,
                                 NULL,
                                 NULL,
                                 &si,
                                 &pi
                                );

    if (!result)
    {
        std::cout << "Launch failed: " << inst.Port << "\n";
        return false;
    }

    std::cout << "Launched port: " << inst.Port << "\n";

    if (run.hMap == nullptr)
    {
        run.hMap = CreateSharedMemory(&run.shared, inst);
    }

    run.StartTime = GetTickCount64();

    return true;
}

std::vector<DWORD> GetAllUEProcesses(
    const std::wstring& exeName
    )
{
    std::vector<DWORD> result;

    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE)
        return result;

    PROCESSENTRY32 pe;
    pe.dwSize = sizeof(pe);

    if (Process32First(snapshot, &pe))
    {
        do
        {
            if (_wcsicmp(pe.szExeFile, exeName.c_str()) == 0)
            {
                result.push_back(pe.th32ProcessID);
            }
        }
        while (Process32Next(snapshot, &pe));
    }

    CloseHandle(snapshot);
    return result;
}
