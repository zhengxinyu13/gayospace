// 控制台中断处理器实现（契约见头文件）。
#include "platform/Interrupt.h"

#ifdef _WIN32
#include <windows.h>
#else
#include <csignal>
#endif

namespace gayospace::platform {

namespace {
InterruptCallback g_callback = nullptr;

#ifdef _WIN32
BOOL WINAPI consoleCtrlHandler(DWORD ctrlType)
{
    // 仅捕获"请求退出"类事件；CTRL_LOGOFF/SHUTDOWN 交给系统默认（进程即将被杀，无意义）
    switch (ctrlType) {
    case CTRL_C_EVENT:
    case CTRL_BREAK_EVENT:
    case CTRL_CLOSE_EVENT:
        if (g_callback != nullptr) {
            g_callback();
        }
        return TRUE; // 已处理，阻止进程被直接终止（给落盘 tick 机会）
    default:
        return FALSE;
    }
}
#else
extern "C" void posixSignalHandler(int)
{
    if (g_callback != nullptr) {
        g_callback();
    }
}
#endif
} // namespace

bool installInterruptHandler(InterruptCallback callback)
{
    g_callback = callback;
#ifdef _WIN32
    return SetConsoleCtrlHandler(consoleCtrlHandler, TRUE) != 0;
#else
    // SIGPIPE 不装：CLI 不写管道；SIGINT/SIGTERM 均视为用户请求退出（130 同语义）
    if (std::signal(SIGINT, &posixSignalHandler) == SIG_ERR) {
        return false;
    }
    if (std::signal(SIGTERM, &posixSignalHandler) == SIG_ERR) {
        return false;
    }
    return true;
#endif
}

} // namespace gayospace::platform
