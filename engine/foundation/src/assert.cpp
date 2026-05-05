#include "next/foundation/assert.h"
#include <cstdio>

#ifdef _WIN32
#include <windows.h>
#endif

namespace Next {

void AssertHandler(const char* condition, const char* file, int line, const char* message) {
    const char* messageStr = message ? message : "Assertion failed";
    
    // Format assertion message
    char assertMsg[1024];
    snprintf(assertMsg, sizeof(assertMsg),
        "\n=== ASSERTION FAILED ===\n"
        "Condition: %s\n"
        "Message:   %s\n"
        "File:      %s:%d\n"
        "=========================\n",
        condition, messageStr, file, line);
    
#ifdef _WIN32
    // Output to debugger (Visual Studio Output window)
    OutputDebugStringA(assertMsg);
#endif
    
    // Output to stderr for console
    // Note: This is a low-level assert handler, fprintf is acceptable here
    // as the logger system might not be initialized yet
    fprintf(stderr, "%s", assertMsg);
    fflush(stderr);

#ifdef _WIN32
    // In Windows, show message box
    char buffer[1024];
    snprintf(buffer, sizeof(buffer),
        "Assertion failed!\n\nCondition: %s\nMessage: %s\nFile: %s:%d",
        condition, messageStr, file, line);
    MessageBoxA(nullptr, buffer, "NEXT Engine - Assertion Failed", MB_ICONERROR | MB_OK);
#endif
}

} // namespace Next
