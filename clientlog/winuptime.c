#include "clientlog.h"

#define MINUTE (60UL * 1000) // Milliseconds
#define HOUR (60UL * MINUTE) // Milliseconds
#define DAY (24UL * HOUR)    // Milliseconds

void clog_winuptime(clog_Arena scratch) {
    SYSTEMTIME systime;
    SYSTEMTIME boottime;
    FILETIME systime_filetime;
    FILETIME boottime_filetime;
    ULARGE_INTEGER systime_lu;
    ULARGE_INTEGER boottime_lu;

    GetSystemTime(&systime);
    SystemTimeToFileTime(&systime, &systime_filetime);
    systime_lu.LowPart = systime_filetime.dwLowDateTime;
    systime_lu.HighPart = systime_filetime.dwHighDateTime;

    ULONGLONG ms_since_boot = GetTickCount();
    ULONGLONG boottime_10ns = systime_lu.QuadPart - ms_since_boot * 10000;
    boottime_lu.QuadPart = boottime_10ns;
    boottime_filetime.dwLowDateTime = boottime_lu.LowPart;
    boottime_filetime.dwHighDateTime = boottime_lu.HighPart;
    FileTimeToSystemTime(&boottime_filetime, &boottime);

    ULONGLONG uptime_days = ms_since_boot / DAY;
    ULONGLONG uptime_hours = (ms_since_boot % DAY) / HOUR;
    ULONGLONG uptime_minutes = (ms_since_boot % HOUR) / MINUTE;

    clog_ArenaAppend(&scratch, "[winuptime]\n");
    clog_ArenaAppend(&scratch, "up %llu days, %02llu:%02llu, since %u-%02u-%02u %02u:%02u:%02u",
                   uptime_days, uptime_hours, uptime_minutes,
                   boottime.wYear, boottime.wMonth, boottime.wDay, boottime.wHour, boottime.wMinute, boottime.wSecond);
}

#ifdef STANDALONE
int main(int argc, TCHAR *argv[]) {
    clog_ArenaState *st = clog_ArenaMake(100);
    clog_winuptime(st->Memory);
    printf("%s", st->Start);
}
#endif