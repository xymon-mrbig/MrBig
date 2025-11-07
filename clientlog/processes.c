#include "clientlog.h"
#include <tlhelp32.h>
#include <pdhmsg.h>
#include <pdh.h>

#define PROCESSES_BENCHMARK_SECONDS (1)
#define NUM_TOPPROCESSES (20)

typedef struct {
    PDH_HQUERY Query;
    PDH_HCOUNTER IDProcess, WorkingSet, ProcessorTime;
    HANDLE Event;
    DWORD ErrorCode;
} processes_State;

typedef struct {
    CHAR *Process;
    LONG PID;
    CHAR *User;
    DOUBLE CPU;
    LONGLONG Memory;
} processes_TableRow;

typedef struct {
    DWORD NumRows;
    processes_TableRow *Rows;
    DWORD ErrorCode;
} processes_Table;

processes_Handle clog_processes_StartQuery(clog_Arena *a) {
    HANDLE eventRes = NULL;
    PDH_HQUERY queryRes = NULL;
    PDH_HCOUNTER idProcess, workingSet, processorTime;
    processes_State *state = clog_ArenaAlloc(a, processes_State, 1);

    state->ErrorCode = PdhOpenQuery(NULL, 0, &queryRes);
    if (state->ErrorCode != ERROR_SUCCESS) goto Cleanup;

    state->ErrorCode = PdhAddEnglishCounter(queryRes, "\\Process(*)\\ID Process", 0, &idProcess);
    if (state->ErrorCode != ERROR_SUCCESS) goto Cleanup;

    state->ErrorCode = PdhAddEnglishCounter(queryRes, "\\Process(*)\\Working Set - Private", 0, &workingSet);
    if (state->ErrorCode != ERROR_SUCCESS) goto Cleanup;

    state->ErrorCode = PdhAddEnglishCounter(queryRes, "\\Process(*)\\% Processor Time", 0, &processorTime);
    if (state->ErrorCode != ERROR_SUCCESS) goto Cleanup;

    eventRes = CreateEvent(NULL, FALSE, FALSE, NULL);
    if (eventRes == NULL) goto Cleanup;

    PdhCollectQueryData(queryRes); // PdhCollectQueryDataEx does in fact not perform an initial query
    state->ErrorCode = PdhCollectQueryDataEx(queryRes, PROCESSES_BENCHMARK_SECONDS, eventRes);
    if (state->ErrorCode != ERROR_SUCCESS) goto Cleanup;

    state->Query = queryRes;
    state->IDProcess = idProcess;
    state->WorkingSet = workingSet;
    state->ProcessorTime = processorTime;
    state->Event = eventRes;
    state->ErrorCode = ERROR_SUCCESS;
    return state;

Cleanup:
    if (queryRes != NULL) PdhCloseQuery(queryRes);
    if (eventRes != NULL) CloseHandle(eventRes);
    return state;
}

CHAR *processes_GetProcessUser(DWORD processID, clog_Arena *a) { // TODO: Cache lookups
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, processID);
    clog_Defer(a, process, RETURN_INT, &CloseHandle);
    HANDLE processToken = NULL;
    if (!OpenProcessToken(process, TOKEN_QUERY, &processToken)) {
        return "-";
    }
    clog_Defer(a, processToken, RETURN_INT, &CloseHandle);

    DWORD userSIDbufsize = 0;
    GetTokenInformation(processToken, TokenUser, NULL, 0, &userSIDbufsize);
    BYTE userSID[userSIDbufsize];
    if (!GetTokenInformation(processToken, TokenUser, (TOKEN_USER *)userSID, userSIDbufsize, &userSIDbufsize)) {
        return "-";
    }

    DWORD userBufsize = 256, domainBufsize = 256;
    CHAR username[userBufsize];
    CHAR domain[domainBufsize];
    SID_NAME_USE type;
    
    LookupAccountSid(NULL, ((TOKEN_USER *)userSID)->User.Sid, username, &userBufsize, domain, &domainBufsize, &type);
    CHAR *result = clog_ArenaAlloc(a, char, userBufsize + domainBufsize + 2);
    sprintf(result, "%s\\%s", domain, username);
    return result;
}

processes_Table processes_AwaitSummarizeQuery(processes_State *state, clog_Arena *a) {
    processes_Table result = {0};

    result.ErrorCode = WaitForSingleObject(state->Event, PROCESSES_BENCHMARK_SECONDS * 1500);
    CloseHandle(state->Event);
    if (result.ErrorCode != WAIT_OBJECT_0) {
        PdhCloseQuery(state->Query);
        return result;
    }

    DWORD bufsize = 0, numItems;
    result.ErrorCode = PdhGetFormattedCounterArray(state->ProcessorTime, PDH_FMT_DOUBLE, &bufsize, &numItems, NULL);
    if (result.ErrorCode != PDH_MORE_DATA) {
        PdhCloseQuery(state->Query);
        return result;
    }

    BYTE idProcesses[bufsize];
    BYTE workingSets[bufsize];
    BYTE processorTimes[bufsize];

    PdhGetFormattedCounterArray(state->IDProcess, PDH_FMT_LONG, &bufsize, &numItems, (PDH_FMT_COUNTERVALUE_ITEM *)idProcesses);
    PdhGetFormattedCounterArray(state->WorkingSet, PDH_FMT_LARGE, &bufsize, &numItems, (PDH_FMT_COUNTERVALUE_ITEM *)workingSets);
    result.ErrorCode = PdhGetFormattedCounterArray(state->ProcessorTime, PDH_FMT_DOUBLE, &bufsize, &numItems, (PDH_FMT_COUNTERVALUE_ITEM *)processorTimes);
    if (result.ErrorCode == ERROR_SUCCESS) {
        result.NumRows = numItems;
        processes_TableRow *rows = clog_ArenaAlloc(a, processes_TableRow, numItems);
        for (DWORD i = 0; i < numItems; i++) {
            CHAR *processName = ((PDH_FMT_COUNTERVALUE_ITEM *)processorTimes)[i].szName;
            size_t processNameLen = strlen(processName);
            CHAR *heapProcessName = clog_ArenaAlloc(a, CHAR, processNameLen + 10);
            memcpy(heapProcessName, processName, processNameLen + 1);
            LONG processID = ((PDH_FMT_COUNTERVALUE_ITEM *)idProcesses)[i].FmtValue.longValue;
            CHAR *user = processes_GetProcessUser(processID, a);
            clog_PopDeferAll(a);

            rows[i] = (processes_TableRow){
                .Process = heapProcessName,
                .PID = processID,
                .User = user,
                .CPU = ((PDH_FMT_COUNTERVALUE_ITEM *)processorTimes)[i].FmtValue.doubleValue,
                .Memory = ((PDH_FMT_COUNTERVALUE_ITEM *)workingSets)[i].FmtValue.largeValue};
        }
        result.Rows = rows;
    }

    PdhCloseQuery(state->Query);
    return result;
}

void processes_AppendTable(processes_Table t, DWORD maxNumRows, clog_Arena *a) {
    clog_ArenaAppend(a, "\n%-31s\t%6s\t%-31s\t%7s\t%12s\n", "PROCESS", "PID", "USER", "CPU", "MEMORY");
    if (t.ErrorCode != ERROR_SUCCESS) {
        clog_ArenaAppend(a, "(Unable to query processes, error code %#0x.)", t.ErrorCode);
        return;
    }

    CHAR memoryBuf[24];
    for (DWORD i = 0; i < t.NumRows && i < maxNumRows; i++) {
        if (t.Rows[i].PID == 0) {
            maxNumRows++;
            continue;
        }
        if (t.Rows[i].Memory == 0)
            memcpy(memoryBuf, "-", 2);
        else
            clog_utils_PrettyBytes(t.Rows[i].Memory, 0, memoryBuf);

        clog_ArenaAppend(a, "%-31s\t%6ld\t%-31s\t%5.1lf %%\t%12s\n", t.Rows[i].Process, t.Rows[i].PID, t.Rows[i].User, t.Rows[i].CPU, memoryBuf);
    }
}

INT8 processes__CompareName(processes_TableRow *a, processes_TableRow *b) {
    INT8 res = -_stricmp(a->Process, b->Process);
    if (res == 0) {
        return a->PID > b->PID ? 1 : -1;
    } else {
        return res;
    }
}

INT8 processes__CompareCPU(processes_TableRow *a, processes_TableRow *b) {
    if (a->CPU > b->CPU) {
        return 1;
    } else if (a->CPU < b->CPU) {
        return -1;
    } else {
        return processes__CompareName(a, b);
    }
}

INT8 processes__CompareMemory(processes_TableRow *a, processes_TableRow *b) {
    if (a->Memory > b->Memory) {
        return 1;
    } else if (a->Memory < b->Memory) {
        return -1;
    } else {
        return processes__CompareName(a, b);
    }
}

void processes__HelperSortBy(processes_TableRow **p, INT8 (*compare)(processes_TableRow *, processes_TableRow *), DWORD start, DWORD end) {
    if (end - start > 1) {
        processes_TableRow *res[end - start];
        DWORD mid = (end + start) / 2;
        processes__HelperSortBy(p, compare, start, mid);
        processes__HelperSortBy(p, compare, mid, end);
        DWORD i = 0, j = 0;
        while (i < mid - start && j < end - mid) {
            INT8 comp = compare(p[start + i],  p[mid + j]);
            if (comp >= 0) {
                res[i + j] = p[start + i];
                i++;
            } else {
                res[i + j] = p[mid + j];
                j++;
            }
        }

        for (; i < mid - start; i++)
            res[i + j] = p[start + i];
        for (; j < end - mid; j++)
            res[i + j] = p[mid + j];

        for (DWORD k = 0; k < end - start; k++) {
            p[k + start] = res[k];
        }
    }
}

void processes_SortBy(processes_Table *p, INT8 (*compare)(processes_TableRow *, processes_TableRow *), clog_Arena *a) {
    processes_TableRow *res[p->NumRows];
    for (DWORD i = 0; i < p->NumRows; i++)
        res[i] = &p->Rows[i];

    processes__HelperSortBy(res, compare, 0, p->NumRows);
    processes_TableRow *sorted = clog_ArenaAlloc(a, processes_TableRow, p->NumRows);

    for (DWORD i = 0; i < p->NumRows; i++)
        sorted[i] = *res[i];

    p->Rows = sorted;
}

void clog_processes_EndAppendQuery(processes_Handle h, clog_Arena *a) {
    processes_State *startedQuery = (processes_State *)h;

    processes_Table endedQuery;
    if (startedQuery->ErrorCode == ERROR_SUCCESS) {
        endedQuery = processes_AwaitSummarizeQuery(startedQuery, a);
    }

    clog_ArenaAppend(a, "[processes]");
    if (startedQuery->ErrorCode != ERROR_SUCCESS || endedQuery.ErrorCode != ERROR_SUCCESS) {
        clog_ArenaAppend(a, "\n(Unable to query processes, error code %#010x)\n", startedQuery->ErrorCode != ERROR_SUCCESS ? startedQuery->ErrorCode : endedQuery.ErrorCode);
    } else {
        processes_SortBy(&endedQuery, &processes__CompareName, a);
        processes_AppendTable(endedQuery, endedQuery.NumRows, a);
    }

    clog_ArenaAppend(a, "[topprocessescpu]");
    if (startedQuery->ErrorCode != ERROR_SUCCESS || endedQuery.ErrorCode != ERROR_SUCCESS) {
        clog_ArenaAppend(a, "\n(Unable to query processes, error code %#010x)\n", startedQuery->ErrorCode != ERROR_SUCCESS ? startedQuery->ErrorCode : endedQuery.ErrorCode);
    } else {
        processes_SortBy(&endedQuery, &processes__CompareCPU, a);
        processes_AppendTable(endedQuery, NUM_TOPPROCESSES, a);
    }

    clog_ArenaAppend(a, "[topprocessesmemory]");
    if (startedQuery->ErrorCode != ERROR_SUCCESS || endedQuery.ErrorCode != ERROR_SUCCESS) {
        clog_ArenaAppend(a, "\n(Unable to query processes, error code %#010x)\n", startedQuery->ErrorCode != ERROR_SUCCESS ? startedQuery->ErrorCode : endedQuery.ErrorCode);
    } else {
        processes_SortBy(&endedQuery, &processes__CompareMemory, a);
        processes_AppendTable(endedQuery, NUM_TOPPROCESSES, a);
    }
}

void _processes(clog_Arena scratch) {
    // This function should ideally not be used. It is better to separate the calls to processes_StartQuery and processes_EndAppendQuery,
    // since there is a 1 second timer on processes_EndAppendQuery. So we can process other parts of clientlog in the meantime.
    processes_Handle h = clog_processes_StartQuery(&scratch); // BIIGHANDLE
    clog_processes_EndAppendQuery(h, &scratch);
}

#ifdef STANDALONE
int main(int argc, TCHAR *argv[]) {
    clog_ArenaState *st = clog_ArenaMake(0x10000);
    _processes(st->Memory);
    printf("%s", st->Start);
}
#endif