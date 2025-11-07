#include "clientlog.h"
#include <math.h>
#include <pdh.h>

double winmemory_pagefilepercent() {
    double res = -1.0;
    PDH_HQUERY queryRes = INVALID_HANDLE_VALUE;
    PDH_HCOUNTER pagefileTotalPercentCounter;
    DWORD errorCode;

    errorCode = PdhOpenQuery(NULL, 0, &queryRes);
    if (errorCode != ERROR_SUCCESS) goto Cleanup;

    errorCode = PdhAddEnglishCounter(queryRes, "\\Paging File(_Total)\\% Usage", 0, &pagefileTotalPercentCounter);
    if (errorCode != ERROR_SUCCESS) goto Cleanup;

    errorCode = PdhCollectQueryData(queryRes);
    if (errorCode != ERROR_SUCCESS) goto Cleanup;

    PDH_FMT_COUNTERVALUE pagefileTotalPercentCounterValue;
    PdhGetFormattedCounterValue(pagefileTotalPercentCounter, PDH_FMT_DOUBLE, NULL, &pagefileTotalPercentCounterValue);
    res = pagefileTotalPercentCounterValue.doubleValue;
    
Cleanup:
    if (queryRes != INVALID_HANDLE_VALUE) PdhCloseQuery(queryRes);
    return res;
}

void clog_winmemory(clog_Arena scratch) {
    MEMORYSTATUSEX memoryStatus;
    memoryStatus.dwLength = sizeof memoryStatus;
    GlobalMemoryStatusEx(&memoryStatus);

    CHAR totalPhys[16], freePhys[16], usedPhys[16];
    CHAR totalPage[16], freePage[16], usedPage[16];

    ULONGLONG totalPhysicalBytes = memoryStatus.ullTotalPhys;
    ULONGLONG freePhysicalBytes = memoryStatus.ullAvailPhys;
    ULONGLONG usedPhysicalBytes = totalPhysicalBytes - freePhysicalBytes;
    DOUBLE percentPhysicalUsed = 100.0 * usedPhysicalBytes / totalPhysicalBytes;

    ULONGLONG totalVirtualBytes = memoryStatus.ullTotalPageFile;
    ULONGLONG freeVirtualBytes = memoryStatus.ullAvailPageFile;
    ULONGLONG usedVirtualBytes = totalVirtualBytes - freeVirtualBytes;
    DOUBLE percentVirtualUsed = 100.0 * usedVirtualBytes / totalVirtualBytes;

    ULONGLONG totalPagefileBytes = memoryStatus.ullTotalPageFile - memoryStatus.ullTotalPhys; // Apparently slightly too large, a "small overhead" exists in pagefiles that cannot be used
    ULONGLONG freePagefileBytes = 0;
    ULONGLONG usedPagefileBytes = 0;
    DOUBLE percentPagefileUsed = winmemory_pagefilepercent();
    if (percentPagefileUsed >= 0) {
        usedPagefileBytes = (ULONGLONG)floor(percentPagefileUsed / 100.0 * (double)totalPagefileBytes);
        freePagefileBytes = totalPagefileBytes - usedPagefileBytes;
    }

    clog_ArenaAppend(&scratch, "[winmemory]");
    clog_ArenaAppend(&scratch, "\n%8s  %-13s\t%-15s\t%-15s\t%-15s", "", "TOTAL", "USED", "FREE", "MEMORY USAGE");
    
    clog_ArenaAppend(&scratch, "\n%8s  %-13s\t%-15s\t%-15s\t%-.2lf%%",
                    "Physical",
                     clog_utils_PrettyBytes(totalPhysicalBytes, 0, totalPhys),
                     clog_utils_PrettyBytes(usedPhysicalBytes, 2, usedPhys),
                     clog_utils_PrettyBytes(freePhysicalBytes, 2, freePhys),
                     percentPhysicalUsed);

    if (percentPagefileUsed < 0) {
        clog_ArenaAppend(&scratch, "\n%8s  %-13s\t (Pagefile usage not available)",
                        "Pagefile",
                         clog_utils_PrettyBytes(totalPagefileBytes, 0, totalPage));
    } else {
        clog_ArenaAppend(&scratch, "\n%8s  %-13s\t%-15s\t%-15s\t%-.2lf%%", "Pagefile",
                         clog_utils_PrettyBytes(totalPagefileBytes, 0, totalPage),
                         clog_utils_PrettyBytes(usedPagefileBytes, 2, usedPage),
                         clog_utils_PrettyBytes(freePagefileBytes, 2, freePage),
                         percentPagefileUsed);
    }

    clog_ArenaAppend(&scratch, "\n%8s  %-13s\t%-15s\t%-15s\t%-.2lf%%",
                     "Virtual",
                     clog_utils_PrettyBytes(totalVirtualBytes, 0, totalPhys),
                     clog_utils_PrettyBytes(usedVirtualBytes, 2, usedPhys),
                     clog_utils_PrettyBytes(freeVirtualBytes, 2, freePhys),
                     percentVirtualUsed);
}

#ifdef STANDALONE
int main(int argc, TCHAR *argv[]) {
    clog_ArenaState *st = clog_ArenaMake(0x10000);
    clog_winmemory(st->Memory);
    printf("%s", st->Start);
}
#endif