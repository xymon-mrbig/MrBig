#include "clientlog.h"

void clog_bios(clog_Arena scratch) {

    clog_ArenaAppend(&scratch, "[bios]\n");
    HKEY hKey;
    LSTATUS status = RegOpenKeyExA(HKEY_LOCAL_MACHINE, "HARDWARE\\DESCRIPTION\\System\\BIOS", 0, KEY_READ, &hKey);
    clog_Defer(&scratch, hKey, RETURN_LONG, &RegCloseKey);

    if (status != ERROR_SUCCESS) {
        clog_ArenaAppend(&scratch, "(Unable to open registry key 'HKEY_LOCAL_MACHINE\\HARDWARE\\DESCRIPTION\\System\\BIOS')");
        return;
    }
    char regSz[256];
    DWORD type, len;

#define clog_APPEND_BIOS(key)                                                 \
    len = sizeof(regSz);                                                      \
    status = RegGetValue(hKey, NULL, key, RRF_RT_REG_SZ, &type, regSz, &len); \
    clog_ArenaAppend(&scratch, "%21s:\t", key);                               \
    if (status != ERROR_SUCCESS)                                              \
        clog_ArenaAppend(&scratch, "(Unable to read)\n");                     \
    else                                                                      \
        clog_ArenaAppend(&scratch, "%s\n", regSz)

    clog_APPEND_BIOS("SystemProductName");

    clog_APPEND_BIOS("SystemManufacturer");

    clog_APPEND_BIOS("BaseBoardManufacturer");

    clog_APPEND_BIOS("BIOSVendor");

    clog_APPEND_BIOS("BIOSVersion");

    clog_APPEND_BIOS("BIOSReleaseDate");

    clog_PopDeferAll(&scratch);
#undef clog_APPEND_BIOS
}

#ifdef STANDALONE
int main(int argc, CHAR *argv[]) {
    clog_ArenaState *st = clog_ArenaMake(0x20000);
    clog_reboots(5, st->Memory);
    printf("%s", st->Start);
}
#endif