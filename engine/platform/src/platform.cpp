#include "next/platform/platform.h"
#include <windows.h>
#include <chrono>
#include <dbghelp.h>
#include <eh.h>
#include <cstdio>
#include <cstdarg>
#include <cstring>

namespace Next {

// Minimal local logging helpers to avoid higher-layer dependencies
// This is the lowest-level logging before the logger system is initialized
static void PlatformLog(const char* level, const char* format, ...) {
    va_list args;
    va_start(args, format);
    
    // Format the complete message
    char buffer[1024];
    int prefixLen = snprintf(buffer, sizeof(buffer), "%s ", level);
    if (prefixLen > 0 && prefixLen < static_cast<int>(sizeof(buffer))) {
        vsnprintf(buffer + prefixLen, sizeof(buffer) - prefixLen, format, args);
    }
    
    // Add newline
    size_t len = strlen(buffer);
    if (len < sizeof(buffer) - 1) {
        buffer[len] = '\n';
        buffer[len + 1] = '\0';
    }
    
    // Output to debugger (Visual Studio Output window)
    OutputDebugStringA(buffer);
    
    // Also output to stderr as fallback
    fprintf(stderr, "%s", buffer);
    fflush(stderr);
    
    va_end(args);
}

// Windows exception filter for crash handling
LONG WINAPI CrashHandler(EXCEPTION_POINTERS* exceptionInfo) {
    PlatformLog("[FATAL]", "=== CRASH DETECTED ===");

    // Get exception code
    DWORD exceptionCode = exceptionInfo->ExceptionRecord->ExceptionCode;
    const char* exceptionName = "Unknown";

    switch (exceptionCode) {
        case EXCEPTION_ACCESS_VIOLATION:
            exceptionName = "Access Violation";
            break;
        case EXCEPTION_ARRAY_BOUNDS_EXCEEDED:
            exceptionName = "Array Bounds Exceeded";
            break;
        case EXCEPTION_BREAKPOINT:
            exceptionName = "Breakpoint";
            break;
        case EXCEPTION_DATATYPE_MISALIGNMENT:
            exceptionName = "Data Type Misalignment";
            break;
        case EXCEPTION_FLT_DENORMAL_OPERAND:
            exceptionName = "Float Denormal Operand";
            break;
        case EXCEPTION_FLT_DIVIDE_BY_ZERO:
            exceptionName = "Float Divide by Zero";
            break;
        case EXCEPTION_FLT_INEXACT_RESULT:
            exceptionName = "Float Inexact Result";
            break;
        case EXCEPTION_FLT_INVALID_OPERATION:
            exceptionName = "Float Invalid Operation";
            break;
        case EXCEPTION_FLT_OVERFLOW:
            exceptionName = "Float Overflow";
            break;
        case EXCEPTION_FLT_STACK_CHECK:
            exceptionName = "Float Stack Check";
            break;
        case EXCEPTION_FLT_UNDERFLOW:
            exceptionName = "Float Underflow";
            break;
        case EXCEPTION_ILLEGAL_INSTRUCTION:
            exceptionName = "Illegal Instruction";
            break;
        case EXCEPTION_IN_PAGE_ERROR:
            exceptionName = "In Page Error";
            break;
        case EXCEPTION_INT_DIVIDE_BY_ZERO:
            exceptionName = "Integer Divide by Zero";
            break;
        case EXCEPTION_INT_OVERFLOW:
            exceptionName = "Integer Overflow";
            break;
        case EXCEPTION_INVALID_DISPOSITION:
            exceptionName = "Invalid Disposition";
            break;
        case EXCEPTION_NONCONTINUABLE_EXCEPTION:
            exceptionName = "Noncontinuable Exception";
            break;
        case EXCEPTION_PRIV_INSTRUCTION:
            exceptionName = "Privileged Instruction";
            break;
        case EXCEPTION_SINGLE_STEP:
            exceptionName = "Single Step";
            break;
        case EXCEPTION_STACK_OVERFLOW:
            exceptionName = "Stack Overflow";
            break;
    }

    PlatformLog("[FATAL]", "Exception Code: 0x%08X (%s)", exceptionCode, exceptionName);
    PlatformLog("[FATAL]", "Exception Address: 0x%p", exceptionInfo->ExceptionRecord->ExceptionAddress);

    // Write mini dump if possible
    HANDLE hFile = CreateFileA("crash_dump.dmp",
                               GENERIC_WRITE,
                               0,
                               nullptr,
                               CREATE_ALWAYS,
                               FILE_ATTRIBUTE_NORMAL,
                               nullptr);

    if (hFile != INVALID_HANDLE_VALUE) {
        MINIDUMP_EXCEPTION_INFORMATION mei;
        mei.ThreadId = GetCurrentThreadId();
        mei.ExceptionPointers = exceptionInfo;
        mei.ClientPointers = FALSE;

        MINIDUMP_TYPE dumpType = MiniDumpNormal;

        if (MiniDumpWriteDump(GetCurrentProcess(),
                             GetCurrentProcessId(),
                             hFile,
                             dumpType,
                             &mei,
                             nullptr,
                             nullptr)) {
            PlatformLog("[FATAL]", "Mini dump written to: crash_dump.dmp");
        } else {
            PlatformLog("[ERROR]", "Failed to write mini dump");
        }

        CloseHandle(hFile);
    }

    // Show crash dialog
    char message[512];
    snprintf(message, sizeof(message),
             "NEXT Engine has crashed!\n\nException: %s (0x%08X)\nA crash dump has been saved.",
             exceptionName, exceptionCode);
    MessageBoxA(nullptr, message, "NEXT Engine - Fatal Error", MB_ICONERROR | MB_OK);

    return EXCEPTION_EXECUTE_HANDLER;
}

bool PlatformInitialize() {
    // Set up crash handler
    SetUnhandledExceptionFilter(CrashHandler);

    // Set up pure virtual function call handler
    _set_purecall_handler([]() {
        PlatformLog("[FATAL]", "Pure virtual function call!");
        std::abort();
    });

    // Set up invalid parameter handler
    _set_invalid_parameter_handler([](const wchar_t* expression,
                                       const wchar_t* function,
                                       const wchar_t* file,
                                       unsigned int line,
                                       uintptr_t reserved) {
        PlatformLog("[FATAL]", "Invalid parameter detected");
        std::abort();
    });

    PlatformLog("[INFO]", "Platform crash handlers initialized");
    return true;
}

void PlatformShutdown() {
    PlatformLog("[INFO]", "Platform shutting down");
}

const char* GetPlatformName() {
    return "Windows";
}

double GetTimeInSeconds() {
    static auto startTime = std::chrono::high_resolution_clock::now();
    auto now = std::chrono::high_resolution_clock::now();
    return std::chrono::duration<double>(now - startTime).count();
}

void SleepMs(uint32_t milliseconds) {
    Sleep(milliseconds);
}

} // namespace Next
