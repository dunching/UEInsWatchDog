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

#include "UEInsWatchDog.h"

// ======================= 主函数 =======================

int main()
{
    Config cfg = LoadConfig();

    std::vector<RunningInstance> running;

    // 启动所有端口
    for (auto& inst : cfg.Instances)
    {
        RunningInstance run;
        run.Config = inst;

        if (LaunchInstance(cfg, inst, run.ProcessInfo, run))
        {
            running.push_back(run);
        }
    }

    // 监控循环
    while (true)
    {
        ULONGLONG now = GetTickCount64();

        for (auto& run : running)
        {
            DWORD wait =
                WaitForSingleObject(run.ProcessInfo.hProcess, 0);

            const auto Offset = now - run.StartTime;
            if (Offset < cfg.StartTime)
            {
                continue;
            }

            const auto TempValue = run.shared->LastHeartbeat;
            if (now < TempValue)
            {
                continue;    
            }
            
            // 心跳超时
            auto HeartbeatOffset = now - TempValue;
            if (
                run.shared &&
                HeartbeatOffset > (ULONGLONG)(cfg.HeartbeatTimeoutSeconds * 1000)
            )
            {
                std::cout << "Heartbeat timeout, kill: "
                    << run.Config.Port << "\n";

                TerminateProcess(run.ProcessInfo.hProcess, 1);
            }

            std::cout << "Port"
                << run.Config.Port << "OK"<<"\n";

            // 崩溃或退出
            if (wait == WAIT_OBJECT_0)
            {
                DWORD exitCode;
                GetExitCodeProcess(
                                   run.ProcessInfo.hProcess,
                                   &exitCode
                                  );

                CloseHandle(run.ProcessInfo.hProcess);
                CloseHandle(run.ProcessInfo.hThread);

                std::cout << "Port "
                    << run.Config.Port
                    << " exited: "
                    << exitCode << "\n";

                if (exitCode != 0)
                {
                    Sleep(cfg.RestartDelaySeconds * 1000);
                    LaunchInstance(
                                   cfg,
                                   run.Config,
                                   run.ProcessInfo,
                                   run
                                  );
                }
            }
        }

        Sleep(cfg.HeartbeatCheckIntervalSeconds * 1000);

        for (auto& run : running)
        {
            DWORD wait =
                WaitForSingleObject(run.ProcessInfo.hProcess, 0);

            // ===== 每5分钟重设亲和度 =====
            if (run.Config.CpuAffinityMask != 0)
            {
                SetProcessAffinityMask(
                                       run.ProcessInfo.hProcess,
                                       run.Config.CpuAffinityMask
                                      );
            }

            // 下面保持你原有逻辑
        }

        // 下面保持你原有逻辑
        auto Ary = GetAllUEProcesses(cfg.RealName);
        for (const auto Iter : Ary)
        {
            bool bOK = false;
            for (const auto& run : running)
            {
                if (Iter == run.ProcessInfo.dwProcessId)
                {
                    bOK = true;
                    break;
                }
            }
            if (!bOK)
            {
                HANDLE h = OpenProcess(PROCESS_TERMINATE, FALSE, Iter);
                if (h)
                {
                    TerminateProcess(h, 0);
                    CloseHandle(h);
                }
            }
        }
    }

    return 0;
}
