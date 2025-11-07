#include "mrbig.h"
#include <winevt.h>

#define EVENT_READ_TIMEOUT (1000)
#define MAX_PROVIDER_NAME_LENGTH (255)
#define MAX_EVENT_MESSAGE_SIZE (0x2000) // 8 KB
#define EVENT_BATCH_SIZE (255)          // Number of events to fetch for each iteration of each log. When "fast" is enabled, do not fetch more events for that log

typedef struct Channel {
    WCHAR Name[256];
} Channel;

struct event *get_event_data(EVT_HANDLE eventHandle) {

    struct event *result = NULL;

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

    result = big_malloc("get_event_data (result)", sizeof(struct event));

    EVT_VARIANT timestampPending = eventSystemProperties[EvtSystemTimeCreated];
    if (timestampPending.Type != EvtVarTypeNull) {
        time_t created = timestampPending.FileTimeVal / 10000000LL - 11644473600LL;
        result->gtime = created;
        result->wtime = created;
    }

    EVT_VARIANT providerNamePending = eventSystemProperties[EvtSystemProviderName];
    LPCWSTR providerName = NULL;
    if (providerNamePending.Type != EvtVarTypeNull) {
        providerName = providerNamePending.StringVal;
        char provider_buf[MAX_PROVIDER_NAME_LENGTH];
        size_t provider_len = wcstombs(provider_buf, providerName, MAX_PROVIDER_NAME_LENGTH);
        if (provider_len >= MAX_PROVIDER_NAME_LENGTH) provider_buf[MAX_PROVIDER_NAME_LENGTH - 1] = '\0';
        result->source = big_strdup("get_event_data (source)", provider_buf);
    }

    EVT_VARIANT eventIdPending = eventSystemProperties[EvtSystemEventID];
    if (eventIdPending.Type != EvtVarTypeNull) {
        result->id = eventIdPending.UInt16Val;
    }

    EVT_VARIANT recordIdPending = eventSystemProperties[EvtSystemEventRecordId];
    if (recordIdPending.Type != EvtVarTypeNull) {
        result->record = recordIdPending.UInt64Val;
    }

    EVT_VARIANT levelPending = eventSystemProperties[EvtSystemLevel];
    if (levelPending.Type != EvtVarTypeNull) {
        result->type = levelPending.ByteVal; // TODO convert
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
            goto CLEANUP;
        }

        WCHAR messageBuffer[bufferNeeded];
        status = EvtFormatMessage(pmHandle, eventHandle, 0, 0, NULL, EvtFormatMessageEvent, bufferNeeded, messageBuffer, &bufferNeeded);
        EvtClose(pmHandle);
        error = GetLastError();
        if (!status &&
            error != ERROR_EVT_UNRESOLVED_VALUE_INSERT &&
            error != ERROR_EVT_UNRESOLVED_PARAMETER_INSERT) {

            goto CLEANUP;
        }

        char message_buf[MAX_EVENT_MESSAGE_SIZE];
        size_t message_len = wcstombs(message_buf, messageBuffer, MAX_EVENT_MESSAGE_SIZE);
        if (message_len >= MAX_EVENT_MESSAGE_SIZE) message_buf[MAX_EVENT_MESSAGE_SIZE - 1] = '\0';
        result->message = big_strdup("get_event_data (message)", message_buf);
    }

CLEANUP:
    EvtClose(contextHandle);
    if (result->source != NULL && result->message == NULL) result->message = "(No message)";
    return result;
}

struct event *read_log(char *log, int maxage, int fast) {
    WCHAR wlog[128];
    mbstowcs(wlog, log, 254);
    wlog[127] = L'\0';

    WCHAR query[128];
    snwprintf(query, 128, L"Event/System[TimeCreated[timediff(@SystemTime) <= %d]]", maxage);
    EVT_HANDLE hLog = EvtQuery(NULL, wlog, query, EvtQueryChannelPath | EvtQueryReverseDirection);
    if (hLog == NULL) {
        if (GetLastError() == 5) {
            mrlog("read_log: Unable to read %s, Access Denied", log);
        } else {
            mrlog("read_log: Unable to read %s, error code %lu", log, GetLastError());
        }
        return NULL;
    }

    EVT_HANDLE hEvents[EVENT_BATCH_SIZE];
    DWORD numHandles = 0;
    BOOL moreEvents;
    struct event *events = NULL;
    do {
        moreEvents = EvtNext(hLog, EVENT_BATCH_SIZE, hEvents, EVENT_READ_TIMEOUT, 0, &numHandles);
        if (moreEvents) {
            for (int i = 0; i < numHandles; i++) {
                struct event *e = get_event_data(hEvents[i]);
                EvtClose(hEvents[i]);
                e->next = events;
                events = e;
            }
        } else {
            if (GetLastError() != ERROR_NO_MORE_ITEMS)
                mrlog("read_log: Unable to iterate events in log %s, error code %lu\n", log, GetLastError());
        }
    } while (!fast && moreEvents);
    EvtClose(hLog);

    return events;
}

void free_log(struct event *e) {
    struct event *p;

    while (e) {
        p = e;
        e = p->next;
        big_free("free_log(source)", p->source);
        big_free("free_log(message)", p->message);
        big_free("free_log(node)", p);
    }
}

#if defined(STANDALONE)
#include <stdio.h>
#define MAX_KEY_LENGTH 255
#define MAX_VALUE_NAME 16383

void prettyEvent(struct event e) {
    printf("Event:\n");
    printf("\tType: %d\n\tSource %s\n\tMessage: %s\n\tgtime: %lu\n\twtime: %lu\n\tRecord: %lu\n\tID: %lu\n", e.type, e.source, e.message, e.gtime, e.wtime, e.record, e.id);
}

int main(int argv, char **argc) {
    time_t t0 = 1000 * 60 * 60 * 24 * 7;
    time_t msgage = 0;
    char *fastmsgs_mode = "123123123123123312312312";
    int fastfile = FALSE;
    HKEY hTestKey;
    TCHAR achKey[MAX_KEY_LENGTH + 1];    // buffer for subkey name
    DWORD cbName;                        // size of name string
    TCHAR achClass[MAX_PATH] = TEXT(""); // buffer for class name
    DWORD cchClassName = MAX_PATH;       // size of class string
    DWORD cSubKeys = 0;                  // number of subkeys
    DWORD cbMaxSubKey;                   // longest subkey size
    DWORD cchMaxClass;                   // longest class string
    DWORD cValues;                       // number of values for key
    DWORD cchMaxValue;                   // longest value name
    DWORD cbMaxValueData;                // longest value data
    DWORD cbSecurityDescriptor;          // size of security descriptor
    FILETIME ftLastWriteTime;            // last write time
    DWORD retCode;
    if (RegOpenKeyEx(HKEY_LOCAL_MACHINE,
                     TEXT("System\\CurrentControlSet\\Services\\EventLog"),
                     0,
                     KEY_READ,
                     &hTestKey) == ERROR_SUCCESS) {
        retCode = RegQueryInfoKey(
            hTestKey,              // key handle
            achClass,              // buffer for class name
            &cchClassName,         // size of class string
            NULL,                  // reserved
            &cSubKeys,             // number of subkeys
            &cbMaxSubKey,          // longest subkey size
            &cchMaxClass,          // longest class string
            &cValues,              // number of values for this key
            &cchMaxValue,          // longest value name
            &cbMaxValueData,       // longest value data
            &cbSecurityDescriptor, // security descriptor
            &ftLastWriteTime);     // last write time
        for (int i = 0; i < cSubKeys; i++) {
            cbName = MAX_KEY_LENGTH;
            retCode = RegEnumKeyEx(hTestKey, i,
                                   achKey, &cbName, NULL,
                                   NULL, NULL, &ftLastWriteTime);
            if (retCode == ERROR_SUCCESS) {
                printf("%s\n", achKey);
                struct event *e = read_log(achKey, t0 - msgage,
                                           !strcmp(fastmsgs_mode + 9, "on") || fastfile);
                while (e != NULL) {
                    prettyEvent(*e);
                    e = e->next;
                }
            }
        }
    }
    return 0;
}
#endif // STANDALONE