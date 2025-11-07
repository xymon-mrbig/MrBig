#include "clientlog.h"
#include <wtsapi32.h>

#define MINUTE (60UL * 1000) // Milliseconds
#define HOUR (60UL * MINUTE) // Milliseconds
#define DAY (24UL * HOUR)    // Milliseconds
#define WHO_ROW_SIZE 80

LPTSTR who_PrettyConnectStateClass(WTS_CONNECTSTATE_CLASS c) {
    switch (c) {
    case WTSActive:
        return "Active";
    case WTSConnected:
        return "Up";
    case WTSConnectQuery:
        return "Query";
    case WTSShadow:
        return "Shadow";
    case WTSDisconnected:
        return "DC";
    case WTSIdle:
        return "Idle";
    case WTSListen:
        return "Listen";
    case WTSReset:
        return "Reset";
    case WTSDown:
        return "Down";
    case WTSInit:
        return "Init";
    default:
        return "Unknown";
    }
}

typedef struct _who_Session {
    CHAR UserName[16], SessionName[16];
    DWORD Id, State;
    struct IDLE {
        DWORD Days, Hours, Minutes;
    } Idle;
    SYSTEMTIME LogonDate;
    struct _who_Session *Next;
} who_Session;

LPSTR who_PrettySession(who_Session *session, LPSTR out) {
    CHAR idle[12] = "(Active)";
    if (session->Idle.Days >= 99) {
        sprintf(idle, "99+ days");
    } else if (session->Idle.Minutes >= 5 || session->Idle.Hours || session->Idle.Days) {
        snprintf(idle, 12, "%2lud %2luh %2lum", session->Idle.Days, session->Idle.Hours, session->Idle.Minutes);
    }
    CHAR logonTime[20] = "-";
    if (session->LogonDate.wYear > 0) {
        clog_utils_PrettySystemtime(&session->LogonDate, clog_utils_TIMESTAMP_DATETIME, logonTime, sizeof(logonTime));
    }
    snprintf(out, WHO_ROW_SIZE, "%-15s\t%-15s\t%4lu\t%-7s\t%12s\t%-20s",
             session->UserName,
             session->SessionName, 
             session->Id,
             who_PrettyConnectStateClass(session->State),
             idle,
             logonTime);
    return out;
}

DWORD who_GetSessions(DWORD maxNumSessions, DWORD *numRetrievedSessions, who_Session *output) {
    PWTS_SESSION_INFO sessions = NULL;
    *numRetrievedSessions = 0;

    BOOL didRetrieveSessions = WTSEnumerateSessions(WTS_CURRENT_SERVER_HANDLE, 0, 1, &sessions, numRetrievedSessions);
    if (!didRetrieveSessions) {
        WTSFreeMemory(sessions);
        return 0;
    }

    DWORD numProcessedSessions = 0;
    who_Session *session;
    PWTSINFO wtsSession; 
    DWORD infoBytes;
    for (DWORD i = 0; i < *numRetrievedSessions && numProcessedSessions < maxNumSessions; i++) {
        WTSQuerySessionInformation(WTS_CURRENT_SERVER_HANDLE, sessions[i].SessionId, WTSSessionInfo, (LPSTR *)&wtsSession, &infoBytes);
        if (!wtsSession->SessionId) {
            WTSFreeMemory(wtsSession);
            continue;
        }

        session = &output[numProcessedSessions++];
        session->Id = wtsSession->SessionId;

        DWORD idle_ms = (wtsSession->CurrentTime.QuadPart - wtsSession->LastInputTime.QuadPart) / 10000LL;
        session->Idle.Days = idle_ms / DAY;
        session->Idle.Hours = (idle_ms % DAY) / HOUR;
        session->Idle.Minutes = (idle_ms % HOUR) / MINUTE;

        if (wtsSession->LastInputTime.LowPart || wtsSession->LastInputTime.HighPart) {
            FILETIME connectFiletime;
            connectFiletime.dwLowDateTime = wtsSession->LastInputTime.LowPart;
            connectFiletime.dwHighDateTime = wtsSession->LastInputTime.HighPart;
            FileTimeToSystemTime(&connectFiletime, &session->LogonDate);
        } else {
            session->LogonDate = (SYSTEMTIME){0};
        }

        clog_utils_ClampString(wtsSession->UserName, session->UserName, sizeof(session->UserName));
        clog_utils_ClampString(wtsSession->WinStationName, session->SessionName, sizeof(session->SessionName));
        WTSFreeMemory(wtsSession);
    }

    WTSFreeMemory(sessions);
    return numProcessedSessions;
}

void clog_who(DWORD maxNumSessions, clog_Arena scratch) {
    who_Session sessions[maxNumSessions];
    DWORD numUnfilteredSessions;
    DWORD numSessions = who_GetSessions(maxNumSessions, &numUnfilteredSessions, sessions);

    clog_ArenaAppend(&scratch, "[who]");
    if (numUnfilteredSessions == 0) {
        clog_ArenaAppend(&scratch, "\nN/A");
        return;
    }

    clog_ArenaAppend(&scratch, "\n%-15s\t%-15s\t%4s\t%-7s\t%12s\t%-20s",
                "USERNAME", "SESSIONNAME", "ID", "STATE", "IDLE TIME", "LOGON TIME");

    CHAR sessionOutputBuffer[WHO_ROW_SIZE];
    for (DWORD i = 0; i < numSessions; i++) {
        clog_ArenaAppend(&scratch, "\n%s", who_PrettySession(&sessions[i], sessionOutputBuffer));
    }

    if (numSessions >= maxNumSessions) {
        clog_ArenaAppend(&scratch, "\n(...with up to %lu more sessions truncated for brevity.)", numUnfilteredSessions - numSessions);
    }
}

#ifdef STANDALONE
int main(int argc, CHAR *argv[]) {
    clog_ArenaState *st = clog_ArenaMake(0x1000);
    who(5, st->Memory);
    printf("%s", st->Start);
}
#endif