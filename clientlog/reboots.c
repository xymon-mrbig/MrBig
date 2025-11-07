#include "clientlog.h"
#include <time.h>

#define MINUTE (60)            // Unit seconds
#define DAY (MINUTE * 60 * 24) // Unit seconds
#define MAX_RECORD_SIZE (0x10000)
#define MAX_EVENT_AGE (30 * DAY)
#define MAX_EVENT_TIMEGAP (15 * MINUTE)
#define REBOOTS_ROW_SIZE 128
#define EVENTLOG_STOPPED_EVENT_ID 6006
#define SYSTEM_RESTART_INITATED_EVENT_ID 1074

typedef struct _reboots_Event {
    SYSTEMTIME Timestamp;
    char Reason[48], User[32];
    struct _reboots_Event *Next;
} reboots_Event;

LPSTR reboots_PrettyEvent(reboots_Event *e, LPSTR out) {
    CHAR rebootTime[20], user[32], reason[64];

    snprintf(out, REBOOTS_ROW_SIZE, "%-23s\t%-31s\t%s",
             clog_utils_PrettySystemtime(&e->Timestamp, clog_utils_TIMESTAMP_DATETIME, rebootTime, sizeof(rebootTime)),
             clog_utils_ClampString(e->User, user, sizeof(user)),
             clog_utils_ClampString(e->Reason, reason, sizeof(reason)));
    return out;
}

/**
 * Wrapper for ReadEventLog. Handles reallocation when lpBuffer is too small.
 * @param hEventLog log handle received from OpenEventLog.
 * @param lpBuffer buffer to store events, reallocated in case it is too small.
 * @param pnBytesRead number of bytes received from ReadEventLogA.
 * @return TRUE if events were loaded, FALSE otherwise
 */
EVENTLOGRECORD *reboots_LoadEvents(HANDLE hEventLog, DWORD *pnBytesRead, clog_Arena *a) {
    DWORD pnMinNumberOfBytesNeeded, nNumberOfBytesToRead = MAX_RECORD_SIZE;
    void *lpBuffer = clog_ArenaAlloc(a, void, MAX_RECORD_SIZE);
    if (!ReadEventLog(hEventLog,
                      EVENTLOG_SEQUENTIAL_READ | EVENTLOG_BACKWARDS_READ,
                      0,
                      lpBuffer,
                      nNumberOfBytesToRead,
                      pnBytesRead,
                      &pnMinNumberOfBytesNeeded)) {

        DWORD status = GetLastError();
        if (status == ERROR_INSUFFICIENT_BUFFER) { // Should only happen if ReadEventLog changes behavior in the future
            status = ERROR_SUCCESS;

            // works since we allocate backwards and immediately after (in front of) previous allocation
            lpBuffer = (EVENTLOGRECORD *)clog__ArenaAllocate(a,
                                                        pnMinNumberOfBytesNeeded - nNumberOfBytesToRead,
                                                        alignof(EVENTLOGRECORD), // Mind the gap!
                                                        1);

            nNumberOfBytesToRead = pnMinNumberOfBytesNeeded;
            if (!ReadEventLog(hEventLog,
                              EVENTLOG_SEQUENTIAL_READ | EVENTLOG_BACKWARDS_READ,
                              0,
                              lpBuffer,
                              nNumberOfBytesToRead,
                              pnBytesRead,
                              &pnMinNumberOfBytesNeeded)) {
                // status = GetLastError();
                // clog_ThrowError(a, status);
                return NULL;
            } else {
                return lpBuffer;
            }
        } else {
            return NULL;
        }
    }

    return lpBuffer;
}

/**
 * Read reboot events from System event log. The number of reboots is limited to numReboots.
 * @param numReboots [input, output] pointer to the maximum number of reboots to allow,
 * @return A pointer to the first element of a linked list of reboot events. Parameter numReboots is overwritten with the actual number of events, which is always smaller than the original value
 */
DWORD reboots_RecentReboots(DWORD maxNumReboots, reboots_Event *output, clog_Arena scratch) {
    HANDLE systemLog = OpenEventLog(NULL, "System");
    if (systemLog) clog_Defer(&scratch, systemLog, RETURN_INT, &CloseEventLog);

    time_t currentTime = time(NULL);

    reboots_Event *reboot;
    DWORD numRebootsFound = 0;

    EVENTLOGRECORD *logEventRecords, *currEventLogStoppedEvent = NULL, *currEventRecord;
    DWORD readBytes;
    clog_Arena rewind = scratch; // copy
    while ((logEventRecords = reboots_LoadEvents(systemLog, &readBytes, &rewind))) {
        currEventRecord = logEventRecords;
        while (readBytes > 0 && numRebootsFound < maxNumReboots && (currentTime - currEventRecord->TimeGenerated) < MAX_EVENT_AGE) {
            // If a "Stopped" event was logged within MAX_EVENT_TIMEGAP minutes, assume it as the cause of the next reboot event
            WORD eventID = currEventRecord->EventID & 0xFFFF;
            if (eventID == EVENTLOG_STOPPED_EVENT_ID) {
                currEventLogStoppedEvent = currEventRecord;
            } else if (eventID == SYSTEM_RESTART_INITATED_EVENT_ID) {
                DWORD rebootTime;
                if (currEventLogStoppedEvent != NULL &&
                    currEventLogStoppedEvent->TimeGenerated - currEventRecord->TimeGenerated > MAX_EVENT_TIMEGAP) {
                    rebootTime = currEventRecord->TimeGenerated;
                } else {
                    rebootTime = currEventRecord->TimeGenerated;
                }

                reboot = &output[numRebootsFound++];

                // Unixtime to Systemtime
                LONGLONG fileTimeGeneratedSeconds = Int32x32To64(rebootTime, 10000000) + 116444736000000000LL;
                FILETIME fileTimeGenerated;
                fileTimeGenerated.dwLowDateTime = (DWORD)fileTimeGeneratedSeconds;
                fileTimeGenerated.dwHighDateTime = fileTimeGeneratedSeconds >> 32;
                FileTimeToSystemTime(&fileTimeGenerated, &reboot->Timestamp);

                char *params[currEventRecord->NumStrings];
                char *param = ((char *)currEventRecord + currEventRecord->StringOffset);
                for (int i = 0; i < currEventRecord->NumStrings; i++) {
                    params[i] = param;
                    param += strlen(param) + 1;
                }
                if (currEventRecord->NumStrings > 2) { // Number of substitution strings SHOULD always be more than 7, but who knows
                    strncpy(reboot->Reason, params[2], sizeof(reboot->Reason));
                    reboot->Reason[sizeof(reboot->Reason) - 1] = '\0';
                } else {
                    strcpy(reboot->Reason, "Unknown");
                }
                if (currEventRecord->NumStrings > 6) {
                    strncpy(reboot->User, params[6], sizeof(reboot->User));
                    reboot->User[sizeof(reboot->User) - 1] = '\0';
                } else {
                    strcpy(reboot->User, "Unknown");
                }
            }
            readBytes -= currEventRecord->Length;
            currEventRecord = (EVENTLOGRECORD *)((LPBYTE)currEventRecord + currEventRecord->Length);
        }
        rewind = scratch; // deallocates transient (right-side) allocations from rewind, in this case, logEventRecords
    }

    return numRebootsFound;
}

void clog_reboots(DWORD maxNumReboots, clog_Arena scratch) {
    reboots_Event rebootevents[maxNumReboots];
    DWORD numRebootsFound = reboots_RecentReboots(maxNumReboots, rebootevents, scratch);
    clog_PopDeferAll(&scratch);
    clog_ArenaAppend(&scratch, "[reboots]");
    if (numRebootsFound == 0) {
        clog_ArenaAppend(&scratch, "\n(No reboots found within the last %lu days)", MAX_EVENT_AGE / DAY);
        return;
    };

    clog_ArenaAppend(&scratch, "\n%-23s\t%-31s\t%-47s", "Date", "User", "Reason");
    CHAR rebootOutputBuffer[REBOOTS_ROW_SIZE];
    for (DWORD i = 0; i < numRebootsFound; i++) {
        clog_ArenaAppend(&scratch, "\n%s", reboots_PrettyEvent(&rebootevents[i], rebootOutputBuffer));
    }
}

#ifdef STANDALONE
int main(int argc, CHAR *argv[]) {
    clog_ArenaState *st = clog_ArenaMake(0x20000);
    clog_reboots(5, st->Memory);
    printf("%s", st->Start);
}
#endif