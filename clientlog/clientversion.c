#include "clientlog.h"

#ifndef PACKAGE
#define PACKAGE ""
#endif
#ifndef VERSION
#define VERSION ""
#endif

void clog_clientversion(clog_Arena scratch) {
    clog_ArenaAppend(&scratch, "[clientversion]");
    if (PACKAGE[0] == '\0' || VERSION[0] == '\0') {
        clog_ArenaAppend(&scratch, "\nMrBig version unknown");
    } else {
        clog_ArenaAppend(&scratch, "\n" PACKAGE " version " VERSION);
    }
}

#ifdef STANDALONE
int main(int argc, TCHAR *argv[]) {
    clog_ArenaState *st = clog_ArenaMake(0x100);
    clog_clientversion(st->Memory);
    printf("%s", st->Start);
}
#endif