#include "clientlog.h"

#define RUN(f, args...)                                            \
    do {                                                           \
        if (clog_mrlog) clog_mrlog("\nClientlog running " #f "(" #args ")"); \
        f(args);                                                   \
    } while (0)

void (*clog_mrlog)(char *fmt, ...) = NULL;
void clientlog(char *mrmachine, void (*mrsend)(char *machine, char *message), void (*mrlog)(char *fmt, ...)) {
    clog_mrlog = mrlog;
    LOG_DEBUG("Clientlog start");
    clog_ArenaState *arenaState = clog_ArenaMake(0x80000); // 512 KB
    clog_Arena arena = arenaState->Memory;

    LOG_DEBUG("Clientlog setup");
    clog_DeferError(&arena, errorcode) {
        LOG_DEBUG("Clientlog error, code %d", errorcode);
        LPSTR errormessage = "\n(Clientlog ran into a problem, error code %d)";
        sprintf((char *)arenaState->CurrentStart - 1 - strlen(errormessage), errormessage, errorcode);

        LOG_DEBUG("Clientlog error mrsend");
        mrsend(mrmachine, (char *)arenaState->Start);

        LOG_DEBUG("Clientlog error teardown");
        clog_PopDeferAll(&arena);
        clog_ArenaFreeAll(arenaState);

        LOG_DEBUG("Clientlog error end");
        return;
    }

    LOG_DEBUG("Clientlog start processes query");
    processes_Handle hProcesses = clog_processes_StartQuery(&arena);

    LOG_DEBUG("Clientlog start message");
    clog_ArenaAppend(&arena, "client %s.windows windows\n", mrmachine);

    RUN(clog_date, arena);
    clog_ArenaAppend(&arena, "\n");

    RUN(clog_osversion, arena);
    clog_ArenaAppend(&arena, "\n");

    // RUN(clog_kbs, arena);
    // clog_ArenaAppend(&arena, "\n");
    RUN(clog_winuptime, arena);
    clog_ArenaAppend(&arena, "\n");

    RUN(clog_bios, arena);
    // No newline

    RUN(clog_who, 10, arena);
    clog_ArenaAppend(&arena, "\n");

    RUN(clog_diskinfo, arena);
    clog_ArenaAppend(&arena, "\n");

    RUN(clog_winmemory, arena);
    clog_ArenaAppend(&arena, "\n");

    RUN(clog_ipconfig, arena);
    clog_ArenaAppend(&arena, "\n");

    RUN(clog_winroute, arena);
    clog_ArenaAppend(&arena, "\n");

    RUN(clog_winports, arena);
    clog_ArenaAppend(&arena, "\n");

    RUN(clog_processes_EndAppendQuery, hProcesses, &arena);
    // No newline

    RUN(clog_runningservices, arena);
    // No newline

    RUN(clog_eventlog, 5, arena);
    clog_ArenaAppend(&arena, "\n");

    RUN(clog_applications, arena);
    clog_ArenaAppend(&arena, "\n");

    RUN(clog_certificates, arena);
    // No newline

    RUN(clog_reboots, 5, arena);
    clog_ArenaAppend(&arena, "\n");

    RUN(clog_clientversion, arena);
    clog_ArenaAppend(&arena, "\n");

    RUN(clog_clock, arena);
    // No newline

    LOG_DEBUG("Clientlog mrsend\n");
    mrsend(mrmachine, (char *)arenaState->Start);

    LOG_DEBUG("Clientlog teardown");
    clog_PopDeferAll(&arena);
    clog_ArenaFreeAll(arenaState);

    LOG_DEBUG("Clientlog end");
}

#ifdef CLIENTLOGEXE
void sendfn(char *machine, char *message) {
    printf("%s", message);
}

void logfn(char *fmt, ...) {
    va_list vargs;
    va_start(vargs, fmt);
    vprintf(fmt, vargs);
    va_end(vargs);
}

int main(int argc, char *argv[]) {
    clientlog("test", &sendfn, &logfn);
    return 0;
}
#endif