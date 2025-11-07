#include "clientlog.h"
#include <math.h>

/** Ensures that a string at most 'outSize' bytes worth of characters. Characters above
 * the limit (and 3 character before) are replaced with two period characters, i.e. "..".
 * If the input 'str' is an empty string, output buffer will contain a single dash, i.e. "-".
 * @param str string to
 * @param out a string buffer which will contain the pretty printed value.
 * @param outSize the number of bytes that can be written to out, must be at least 2
 * @return The input parameter "out" for convenience.
 */
LPSTR clog_utils_ClampString(LPSTR str, LPSTR out, size_t outSize) {
    int written = snprintf(out, outSize, "%s", str);
    if (written >= outSize && outSize > 7) {
        strcpy(&out[outSize - 3], "..");
    } else if (written == 0) {
        strcpy(out, "-");
    }

    return out;
}

/**
 * Pretty print a byte number to two decimal places.
 * @example clog_utils_PrettyBytes(2048, 0, ...) -> "2.00 KB"
 * @example clog_utils_PrettyBytes(8192, 2 ...) -> "0.01 MB" (rounded from 0,007813 MB)
 * @param bytes the byte number to pretty print.
 * @param target base 1024 magnitude to print, e.g. 1 MB = 1024^2 B, so for
 *               target = 2, bytes = 1 048 576 = 1.00 * 1024^2, we get "1.00 MB".
 *               A target of 0 (FALSE) means "try to find a best match".
 * @param out output buffer to store the result.
 * @return The input parameter "out" for convenience.
 */
LPSTR clog_utils_PrettyBytes(ULONGLONG bytes, DWORD target, LPSTR out) {
    static DOUBLE LOG1024E = 0.144269504088896340736;
    static LPCTSTR prefixes[] = {"", "K", "M", "G", "T"};

    // Given bytes = 1024^magnitude, then
    // magnitude = log_1024(bytes) = ln(bytes) * log_1024(e),
    DWORD magnitude = target ? target : (DWORD)(log(bytes) * LOG1024E);
    LPCTSTR prefix = prefixes[min(4, magnitude)];
    DOUBLE result = bytes * pow(1024.0, -(double)magnitude);
    snprintf(out, 16, "%.2lf %sB", result, prefix);
    return out;
}

/** Pretty print a SYSTEMTIME value.
 * @param t pointer to the SYSTEMTIME to pretty print.
 * @param flags must be one of clog_utils_TIMESTAMP_DATE, clog_utils_TIMESTAMP_CLOCK, clog_utils_TIMESTAMP_DATETIME, controls whether to print date (year-month-day) only, clock (hours:minutes:seconds) only, or both.
 * @param out a string buffer which will contain the pretty printed value.
 * @param outSize the number of bytes that can be written to out
 * @return The input parameter "out" for convenience.
 */
LPSTR clog_utils_PrettySystemtime(SYSTEMTIME *t, UINT8 flags, LPSTR out, size_t outSize) {
    DWORD written = 0;
    if (flags & clog_utils_TIMESTAMP_DATE) written += snprintf(&out[written], outSize - written, "%u-%02u-%02u", t->wYear, t->wMonth, t->wDay);
    if ((flags & clog_utils_TIMESTAMP_DATE) && (flags & clog_utils_TIMESTAMP_CLOCK)) written += snprintf(&out[written], outSize - written, " ");
    if (flags & clog_utils_TIMESTAMP_CLOCK) written += snprintf(&out[written], outSize - written, "%02u:%02u:%02u", t->wHour, t->wMinute, t->wSecond);
    return out;
}

#define PROCESS_TIMOUT_LIMIT_MS 1000
#define BUFREAD 513
/** Run a shell command. Callers should call clog_PopDeferAll(&scratch) after this function has been used.
 * @param cmdline the command to run, including flags
 * @param scratch a clientlog arena to which the output will be appended
 * @return If the command finished sucessfully, TRUE, otherwise FALSE. A command with an errored status code is not considered a failure.
 */
DWORD clog_utils_RunCmdSynchronously(CHAR *cmdline, clog_Arena scratch) {
    HANDLE hPipeOutputRead = NULL;
    HANDLE hPipeOutputWrite = NULL;
    DWORD status = 0;

    SECURITY_ATTRIBUTES securityAttributes;
    // Set the bInheritHandle flag so pipe handles are inherited.
    securityAttributes.nLength = sizeof(SECURITY_ATTRIBUTES);
    securityAttributes.bInheritHandle = TRUE;
    securityAttributes.lpSecurityDescriptor = NULL;

    // Create a pipe for the child process's STDOUT.
    if (!CreatePipe(&hPipeOutputRead, &hPipeOutputWrite, &securityAttributes, 0)) {
        status = GetLastError();
        clog_ArenaAppend(&scratch, "(Failed to run command, unknown error. Error code 1.%#010x.)", status);
        CloseHandle(hPipeOutputRead);
        CloseHandle(hPipeOutputWrite);
        return status;
    }

    // Ensure the read handle to the pipe for STDOUT is not inherited.
    if (!SetHandleInformation(hPipeOutputRead, HANDLE_FLAG_INHERIT, 0)) {
        status = GetLastError();
        clog_ArenaAppend(&scratch, "(Failed to run command, unknown error. Error code 2.%#010x.)", status);
        CloseHandle(hPipeOutputRead);
        CloseHandle(hPipeOutputWrite);
        return status;
    }

    PROCESS_INFORMATION procInfo;
    ZeroMemory(&procInfo, sizeof(procInfo));
    STARTUPINFO startInfo;
    ZeroMemory(&startInfo, sizeof(startInfo));
    startInfo.cb = sizeof(STARTUPINFO);
    startInfo.hStdError = hPipeOutputWrite;
    startInfo.hStdOutput = hPipeOutputWrite;
    startInfo.dwFlags |= STARTF_USESTDHANDLES;

    status = CreateProcess(NULL,
                           cmdline,
                           NULL,
                           NULL,
                           TRUE, // important, must inherit handles due to STARTF_USESTDHANDLES
                           0,
                           NULL,
                           NULL,
                           &startInfo,
                           &procInfo);

    CloseHandle(hPipeOutputWrite);
    if (!status) {
        status = GetLastError();
        clog_ArenaAppend(&scratch, "(Failed to run command, could not create process from '%s'. Error code 3.%#010x.)", cmdline, status);
        CloseHandle(hPipeOutputRead);
        CloseHandle(procInfo.hProcess);
        CloseHandle(procInfo.hThread);
        return status;
    }

    status = WaitForSingleObject(procInfo.hProcess, PROCESS_TIMOUT_LIMIT_MS);
    if (status != WAIT_OBJECT_0) {
        if (status == WAIT_FAILED) {
            status = GetLastError();
            clog_ArenaAppend(&scratch, "(Failed to run command, unknown error. Error code 4.%#010x.)", PROCESS_TIMOUT_LIMIT_MS, status);
        } else if (status == WAIT_TIMEOUT) {
            clog_ArenaAppend(&scratch, "(Failed to run command, process took more than %dms to run. Error code 5.%lu.)", PROCESS_TIMOUT_LIMIT_MS, status);
        } else {
            DWORD lastError = GetLastError();
            clog_ArenaAppend(&scratch, "(Failed to run command, unknown error. Error code 6.%lu.%#010x.)", PROCESS_TIMOUT_LIMIT_MS, status, lastError);
        }
        CloseHandle(procInfo.hProcess);
        CloseHandle(procInfo.hThread);
        CloseHandle(hPipeOutputRead);
        return status;
    }

    DWORD readBufLen = 0, written = 0;
    CHAR readBuf[BUFREAD];
    do {
        status = ReadFile(hPipeOutputRead, readBuf, BUFREAD-1, &readBufLen, NULL);
        if (readBufLen > 0) {
            readBuf[readBufLen] = '\0';
            clog_ArenaAppend(&scratch, "%s", readBuf);
            written += readBufLen;
        } else {
            break;
        }
    } while (TRUE);

    CloseHandle(procInfo.hProcess);
    CloseHandle(procInfo.hThread);
    CloseHandle(hPipeOutputRead);
    if (written <= 0) {
        clog_ArenaAppend(&scratch, "(No output)");
    }

    return status;
}
#undef BUFREAD
#undef PROCESS_TIMOUT_LIMIT_MS