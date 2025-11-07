#pragma once

#include "arena.h"
#include <stdio.h>
#include <wtypesbase.h>

#define STR__IMPL(x) #x
#define STR(x) STR__IMPL(x) // indirection to expand macros in x
#define lengthof(x) (sizeof(x) / sizeof(*(x)))

#define LOG_DEBUG(...) \
    if (clog_mrlog) clog_mrlog("\n" __VA_ARGS__);
extern void (*clog_mrlog)(char *fmt, ...);

void clientlog(char *mrmachine, void (*mrsend)(char *machine, char *message), void (*mrlog)(char *fmt, ...));

/* utils */
LPSTR clog_utils_ClampString(LPSTR str, LPSTR out, size_t outSize);
LPSTR clog_utils_PrettyBytes(ULONGLONG bytes, DWORD target, LPSTR out);
enum clog_utils_PrettyTimestampFlags {
    clog_utils_TIMESTAMP_DATE = 1,
    clog_utils_TIMESTAMP_CLOCK,
    clog_utils_TIMESTAMP_DATETIME,
};
LPSTR clog_utils_PrettySystemtime(SYSTEMTIME *t, UINT8 flags, LPSTR out, size_t outSize);
DWORD clog_utils_RunCmdSynchronously(CHAR *cmdline, clog_Arena scratch);

/* applications */
void clog_applications(clog_Arena scratch);

/* bios */
void clog_bios(clog_Arena scratch);

/* certificates */
void clog_certificates(clog_Arena scratch);

/* clientversion */
void clog_clientversion(clog_Arena scratch);

/* clock */
void clog_clock(clog_Arena scratch);

/* date */
void clog_date(clog_Arena scratch);

/* diskinfo */
void clog_diskinfo(clog_Arena scratch);

/* eventlog */
void clog_eventlog(DWORD maxNumEvents, clog_Arena scratch);

/* ipconfig */
void clog_ipconfig(clog_Arena scratch);

/* kbs */
void clog_kbs(clog_Arena scratch);

/* osversion */
void clog_osversion(clog_Arena scratch);

/* processes + topprocesses */
typedef void *processes_Handle;
processes_Handle clog_processes_StartQuery(clog_Arena *a);
void clog_processes_EndAppendQuery(processes_Handle h, clog_Arena *a);

/* reboots */
void clog_reboots(DWORD maxNumReboots, clog_Arena scratch);

/* runningservices */
void clog_runningservices(clog_Arena scratch);

/* who */
void clog_who(DWORD maxNumSessions, clog_Arena scratch);

/* winmemory */
void clog_winmemory(clog_Arena scratch);

/* winroute */
void clog_winroute(clog_Arena scratch);

/* winports + winportsused */
void clog_winports(clog_Arena scratch);

/* winuptime */
void clog_winuptime(clog_Arena scratch);
