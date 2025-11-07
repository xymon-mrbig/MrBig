#include "clientlog.h"

void clog_ipconfig(clog_Arena scratch) {
    clog_ArenaAppend(&scratch, "[ipconfig]\n");
    clog_utils_RunCmdSynchronously("C:\\Windows\\System32\\ipconfig.exe /all", scratch);
}