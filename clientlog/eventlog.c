#include "clientlog.h"
#include <time.h>
#include <winevt.h>

#define MAX_PROVIDER_NAME_LENGTH (255)
#define MAX_EVENT_MESSAGE_SIZE (0x1000) // 4 KB
#define MAX_EVENTLOG_ROW_SIZE (512)

// Due to string literal concatenation in EvtQuery below, you must treat this as a string (not a number) when modifying
// For example, a writing 3600 * 1000 instead of 3600000 will not work
#define MAX_EVENT_AGE_MS 3600000

static WCHAR *CHANNELS[] = {
    L"Application",
    L"Setup",
    L"System",
};
static size_t NUM_CHANNELS = sizeof(CHANNELS) / sizeof(*CHANNELS);

typedef struct {
    ULONGLONG Timestamp;
    CHAR Provider[MAX_PROVIDER_NAME_LENGTH];
    CHAR Message[MAX_EVENT_MESSAGE_SIZE];
    UINT16 EventID;
    UINT8 Level;
} eventlog_Event;

LPCSTR eventlog_PrettyEventLevel(UINT8 level) {
    switch (level) {
    case 0:
        return "Always";
    case 1:
        return "Critical";
    case 2:
        return "Error";
    case 3:
        return "Warning";
    case 4:
        return "Info";
    case 5:
        return "Verbose";
    default:
        return "Unknown";
    }
}

LPSTR eventlog_PrettyEvent(eventlog_Event *e, CHAR *out) {
    CHAR eventTimestamp[24], providerBuffer[30], eventMessageClamped[440];
    SYSTEMTIME t;
    FileTimeToSystemTime((FILETIME *)&e->Timestamp, &t);

    snprintf(out, MAX_EVENTLOG_ROW_SIZE, "%-23s\t%6d \t%-7s\t %-30s\t%s",
             clog_utils_PrettySystemtime(&t, clog_utils_TIMESTAMP_DATETIME, eventTimestamp, sizeof(eventTimestamp)),
             e->EventID,
             eventlog_PrettyEventLevel(e->Level),
             clog_utils_ClampString(e->Provider, providerBuffer, sizeof(providerBuffer)),
             clog_utils_ClampString(e->Message, eventMessageClamped, sizeof(eventMessageClamped)));
    return out;
}

eventlog_Event eventlog_GetEventData(EVT_HANDLE eventHandle) {
    eventlog_Event result = {0};

    // Follows general steps of .Net (C#) class EventLogRecord.cs, from System.Diagnostics.Reader
    EVT_HANDLE contextHandle = EvtCreateRenderContext(0, NULL, EvtRenderContextSystem);

    DWORD bufferNeeded, propCount;
    BOOL status = EvtRender(contextHandle, eventHandle, EvtRenderEventValues, 0, NULL, &bufferNeeded, &propCount);
    DWORD error = GetLastError();
    if (error != ERROR_INSUFFICIENT_BUFFER) {
        EvtClose(contextHandle);
        return result;
    }

    EVT_VARIANT eventSystemProperties[bufferNeeded / sizeof(EVT_VARIANT)];
    status = EvtRender(contextHandle, eventHandle, EvtRenderEventValues, bufferNeeded, eventSystemProperties, &bufferNeeded, &propCount);
    if (!status) {
        EvtClose(contextHandle);
        return result;
    }

    EVT_VARIANT timestampPending = eventSystemProperties[EvtSystemTimeCreated];
    if (timestampPending.Type != EvtVarTypeNull) {
        result.Timestamp = timestampPending.FileTimeVal;
    }

    EVT_VARIANT providerNamePending = eventSystemProperties[EvtSystemProviderName];
    LPCWSTR providerName = NULL;
    if (providerNamePending.Type != EvtVarTypeNull) {
        providerName = providerNamePending.StringVal;
        size_t providerNameWritten = wcstombs(result.Provider, providerName, MAX_PROVIDER_NAME_LENGTH);
        if (providerNameWritten >= MAX_PROVIDER_NAME_LENGTH) result.Provider[MAX_PROVIDER_NAME_LENGTH - 1] = '\0';
    }

    EVT_VARIANT eventIdPending = eventSystemProperties[EvtSystemEventID];
    if (eventIdPending.Type != EvtVarTypeNull) {
        result.EventID = eventIdPending.UInt16Val;
    }

    EVT_VARIANT levelPending = eventSystemProperties[EvtSystemLevel];
    if (levelPending.Type != EvtVarTypeNull) {
        result.Level = levelPending.ByteVal;
    }

    if (providerName != NULL) {
        // TODO: cache publisher handles like in .NET EventLogRecord.
        EVT_HANDLE pmHandle = EvtOpenPublisherMetadata(NULL, providerName, NULL, 0, 0); // RE providerName. We cannot use result.Provider, since we need wchar_t*

        WCHAR emptyBuffer[1] = {L'\0'}; // Due to: https://github.com/dotnet/runtime/issues/100198"
        status = EvtFormatMessage(pmHandle, eventHandle, 0, 0, NULL, EvtFormatMessageEvent, 0, emptyBuffer, &bufferNeeded);
        error = GetLastError();
        if (!status && // EventLogRecord says: Unresolved inserts are indications that strings COULD be missing, and are not real errors
            error != ERROR_EVT_UNRESOLVED_VALUE_INSERT &&
            error != ERROR_EVT_UNRESOLVED_PARAMETER_INSERT &&
            error != ERROR_INSUFFICIENT_BUFFER) {
            EvtClose(pmHandle);
            EvtClose(contextHandle);
            return result;
        }

        WCHAR messageBuffer[bufferNeeded];
        status = EvtFormatMessage(pmHandle, eventHandle, 0, 0, NULL, EvtFormatMessageEvent, bufferNeeded, messageBuffer, &bufferNeeded);
        error = GetLastError();
        EvtClose(pmHandle);
        if (!status &&
            error != ERROR_EVT_UNRESOLVED_VALUE_INSERT &&
            error != ERROR_EVT_UNRESOLVED_PARAMETER_INSERT) {
            EvtClose(contextHandle);
            return result;
        }
        size_t messageWritten = wcstombs(result.Message, messageBuffer, MAX_EVENT_MESSAGE_SIZE);
        if (messageWritten >= MAX_EVENT_MESSAGE_SIZE) result.Message[MAX_EVENT_MESSAGE_SIZE - 1] = '\0';
    }

    EvtClose(contextHandle);
    return result;
}

void clog_eventlog(DWORD maxNumEvents, clog_Arena scratch) {
    CHAR eventBuffer[MAX_EVENTLOG_ROW_SIZE];
    for (DWORD channelIx = 0; channelIx < NUM_CHANNELS; channelIx++) {
        CHAR channelName[32];
        wcstombs(channelName, CHANNELS[channelIx], 32);
        clog_ArenaAppend(&scratch, "\n[eventlog_%s]", CharLowerA(channelName));

        EVT_HANDLE hLog = EvtQuery(NULL, CHANNELS[channelIx], L"Event/System[Level<4 and TimeCreated[timediff(@SystemTime) <= " STR(MAX_EVENT_AGE_MS) "]]", EvtQueryChannelPath | EvtQueryReverseDirection);
        clog_Defer(&scratch, hLog, RETURN_INT, &EvtClose);

        EVT_HANDLE event[maxNumEvents];
        DWORD nEvents = 0;
        EvtNext(hLog, maxNumEvents, event, INFINITE, 0, &nEvents);
        if (nEvents > 0) {
            clog_ArenaAppend(&scratch, "\n%-23s\t%6s \t%-7s\t %-30s\t%s", "Timestamp", "Id", "Level", "Source", "Message");

            for (DWORD i = 0; i < nEvents; i++) {
                eventlog_Event e = eventlog_GetEventData(event[i]);
                clog_Defer(&scratch, event[i], RETURN_INT, &EvtClose);
                clog_ArenaAppend(&scratch, "\n%s", eventlog_PrettyEvent(&e, eventBuffer));
                clog_PopDefer(&scratch);
            }
        } else {
            clog_ArenaAppend(&scratch, "\n(No warnings or errors found within the last %lfh.)", MAX_EVENT_AGE_MS / 3600000.0);
        }
        clog_PopDefer(&scratch);
    }
}

#ifdef STANDALONE
int main(int argc, char *argv[]) {
    clog_ArenaState *st = clog_ArenaMake(0x100000);
    clog_eventlog(5, st->Memory);
    clog_PopDeferAll(&st->Memory);
    printf((char *)st->Start);
    return 0;
}
#endif