#include "clientlog.h"
#include <time.h>

#define TIME_BUF_SIZE 64

void clog_clock(clog_Arena scratch) {
    time_t unixtime = time(NULL);
    struct tm *tm_time; // careful, pointers returned by localtime and gmtime point to the same memory

    tm_time = localtime(&unixtime);
    CHAR localBuf[TIME_BUF_SIZE];
    strftime(localBuf, TIME_BUF_SIZE, "%Y-%m-%d %H:%M:%S %Z", tm_time);

    tm_time = gmtime(&unixtime);
    CHAR systemBuf[TIME_BUF_SIZE];
    strftime(systemBuf, TIME_BUF_SIZE, "%Y-%m-%d %H:%M:%S", tm_time);

    clog_ArenaAppend(&scratch, "[clock]");
    clog_ArenaAppend(&scratch, "\nlocal:\t%s", localBuf);
    clog_ArenaAppend(&scratch, "\nUTC:\t%s UTC", systemBuf);
}

#ifdef STANDALONE
int main(int argc, char *argv[]) {
    clog_ArenaState *st = clog_ArenaMake(0x10000);
    clog_clock(st->Memory);
    printf("%s", st->Start);
    return 0;
}
#endif