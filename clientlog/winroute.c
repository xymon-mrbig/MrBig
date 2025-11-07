#include "clientlog.h"

void clog_winroute(clog_Arena scratch) {
    clog_ArenaAppend(&scratch, "[winroute]\n");
    clog_utils_RunCmdSynchronously("C:\\Windows\\System32\\route print", scratch);
}