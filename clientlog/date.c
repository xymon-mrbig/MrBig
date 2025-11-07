#include "clientlog.h"

#define TIME_BUF_SIZE 12

void clog_date(clog_Arena scratch) {
    WCHAR w_date[TIME_BUF_SIZE];
    CHAR s_date[TIME_BUF_SIZE];
    GetDateFormatEx(L"sv-SE", DATE_SHORTDATE, NULL, NULL, w_date, TIME_BUF_SIZE, NULL);
    wcstombs(s_date, w_date, TIME_BUF_SIZE);

    WCHAR w_time[TIME_BUF_SIZE];
    CHAR s_time[TIME_BUF_SIZE];
    GetTimeFormatEx(L"sv-SE", TIME_FORCE24HOURFORMAT, NULL, NULL, w_time, TIME_BUF_SIZE);
    wcstombs(s_time, w_time, TIME_BUF_SIZE);

    clog_ArenaAppend(&scratch, "[date]\n");
    clog_ArenaAppend(&scratch, "%s %s", s_date, s_time);
}

#ifdef STANDALONE
int main(int argc, TCHAR *argv[]) {
    clog_ArenaState *st = clog_ArenaMake(32);
    clog_date(st->Memory);
    printf("%s", st->Start);
}
#endif