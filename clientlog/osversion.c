#include "clientlog.h"

typedef struct {
    CHAR name[64];
    CHAR version[16];
    WORD buildNumber;
} osversion_WindowsVersion;

const osversion_WindowsVersion WindowsPersonalVersions[] = {
    // Must be sorted by buildNumber
    {"Windows 2000 Professional", "NT 5.0", 2195},
    {"Windows XP", "NT 5.1", 2600},
    {"Windows XP Professional x64 Edition", "NT 5.2", 3790},
    {"Windows Vista", "NT 6.0", 6002},
    {"Windows 7", "NT 6.1", 7601},
    {"Windows 8", "NT 6.2", 9200},
    {"Windows 8.1", "NT 6.3", 9600},
    {"Windows 10", "1507", 10240},
    {"Windows 10", "1511", 10586},
    {"Windows 10", "1607", 14393},
    {"Windows 10", "1703", 15063},
    {"Windows 10", "1709", 16299},
    {"Windows 10", "1803", 17134},
    {"Windows 10", "1809", 17763},
    {"Windows 10", "1903", 18362},
    {"Windows 10", "1909", 18363},
    {"Windows 10", "2004", 19041},
    {"Windows 10", "20H2", 19042},
    {"Windows 10", "21H1", 19043},
    {"Windows 10", "21H2", 19044},
    {"Windows 10", "22H2", 19045},
    {"Windows 10", "unknown version", 19046},
    {"Windows 11", "21H2", 22000},
    {"Windows 11", "22H2", 22621},
    {"Windows 11", "23H2", 22631},
    {"Windows 11", "24H2", 26100},
    {"Windows 11+", "unknown version", 26101},
};

const osversion_WindowsVersion WindowsServerVersions[] = {
    // Must be sorted by buildNumber
    {"Windows 2000 Server", "NT 5.0", 2195},
    {"Windows Server 2003", "NT 5.2", 3790},
    {"Windows Server 2008", "NT 6.0", 6003},
    {"Windows Server 2008 R2", "NT 6.1", 7601},
    {"Windows Server 2012", "NT 6.2", 9200},
    {"Windows Server 2012 R2", "NT 6.3", 9600},
    {"Windows Server 2016", "1607", 14393},
    {"Windows Server", "1709", 16299},
    {"Windows Server", "1803", 17134},
    {"Windows Server", "1809", 17763},
    {"Windows Server", "1903", 18362},
    {"Windows Server", "1909", 18363},
    {"Windows Server", "2004", 19041},
    {"Windows Server", "20H2", 19042},
    {"Windows Server 2022", "21H2", 20348},
    {"Windows Server", "23H2", 25398},
    {"Windows Server 2025", "24H2", 26100},
    {"Windows Server", "unknown version", 26101},
};

void clog_osversion(clog_Arena scratch) {
    HKEY hKey;
    LONG productStatus = RegOpenKeyEx(HKEY_LOCAL_MACHINE, "Software\\Microsoft\\Windows NT\\CurrentVersion", 0, KEY_READ, &hKey);
    CHAR productName[64];
    DWORD typeProduct;
    DWORD lenProduct = 64;
    if (productStatus == ERROR_SUCCESS) {
        productStatus = RegGetValue(hKey, NULL, "productName", RRF_RT_REG_SZ, &typeProduct, productName, &lenProduct);
    }
    RegCloseKey(hKey);

    OSVERSIONINFOEXW osVersionInfo;
    BOOL versionSuccess = FALSE;

    clog_ArenaAppend(&scratch, "[osversion]");
    OSVERSIONINFOEXW ovi = {0};
    ovi.dwOSVersionInfoSize = sizeof(OSVERSIONINFOEXW);
    if (GetVersionExW((LPOSVERSIONINFOW)&ovi)) {
        osVersionInfo = ovi;
        versionSuccess = TRUE;
    }

    CHAR osName[64];
    if (!versionSuccess) {
        CHAR *result;
        if (productStatus != ERROR_SUCCESS) {
            result = "\nNot supported";
        } else {
            result = productName;
        }

        clog_ArenaAppend(&scratch, "\n%s", result);
        return;
    }

    // START Remove this if it becomes out of date. It is only used to get the version member of osversion_WindowsVersion
    BOOL isPersonalComputer = osVersionInfo.wProductType == VER_NT_WORKSTATION;
    const osversion_WindowsVersion *versionList = isPersonalComputer
                                                   ? WindowsPersonalVersions
                                                   : WindowsServerVersions;
    size_t versionListSize = isPersonalComputer
                                 ? sizeof WindowsPersonalVersions
                                 : sizeof WindowsServerVersions;

    size_t nVersions = versionListSize / sizeof(osversion_WindowsVersion);

    WORD build = osVersionInfo.dwBuildNumber;
    size_t nCurr = nVersions / 2;
    size_t nLow = 0;
    size_t nHigh = nVersions;
    while (nLow < nHigh - 1) {
        if (build < versionList[nCurr].buildNumber) {
            nHigh = nCurr;
            nCurr = (nLow + nHigh) / 2;
        } else if (build > versionList[nCurr].buildNumber) {
            nLow = nCurr + 1;
            nCurr = (nLow + nHigh) / 2;
        } else {
            break;
        }
    }
    osversion_WindowsVersion win = versionList[nCurr];
    // END Remove

    int osNameWritten = 0;
    if (productStatus == ERROR_SUCCESS) {
        osNameWritten = snprintf(osName, 64, "%s", productName);
    } else {
        osNameWritten = snprintf(osName, 64, "%s", win.name);
    }
    if (osVersionInfo.szCSDVersion[0] != '\0') {
        CHAR servicePack[32];
        size_t servicePackLen = wcstombs(servicePack, osVersionInfo.szCSDVersion, 32);
        if (servicePackLen > 31) servicePack[31] = '\0';
        snprintf(&osName[osNameWritten], 64 - osNameWritten, " %s", servicePack);
    }

    CHAR osVersion[64];
    snprintf(osVersion, 64, "version %lu.%lu.%lu (%s)", osVersionInfo.dwMajorVersion, osVersionInfo.dwMinorVersion, osVersionInfo.dwBuildNumber, win.version);

    clog_ArenaAppend(&scratch, "\n%s, %s", osName, osVersion);

    /*
    const size_t suitesBufSize = 300;
    CHAR installedSuites[suitesBufSize];
    int suitesWritten = 0;
    if (VER_SUITE_BACKOFFICE & osVersionInfo.wSuiteMask)
        suitesWritten += snprintf(&installedSuites[suitesWritten], max(0, suitesBufSize - suitesWritten), "Microsoft BackOffice\n");
    if (VER_SUITE_BLADE & osVersionInfo.wSuiteMask)
        suitesWritten += snprintf(&installedSuites[suitesWritten], max(0, suitesBufSize - suitesWritten), "Windows Server 2003, Web Edition\n");
    if (VER_SUITE_COMPUTE_SERVER & osVersionInfo.wSuiteMask)
        suitesWritten += snprintf(&installedSuites[suitesWritten], max(0, suitesBufSize - suitesWritten), "Windows Server 2003, Compute Cluster Edition\n");
    if (VER_SUITE_DATACENTER & osVersionInfo.wSuiteMask)
        suitesWritten += snprintf(&installedSuites[suitesWritten], max(0, suitesBufSize - suitesWritten), "Windows Server Datacenter\n");
    if (VER_SUITE_ENTERPRISE & osVersionInfo.wSuiteMask)
        suitesWritten += snprintf(&installedSuites[suitesWritten], max(0, suitesBufSize - suitesWritten), "Windows Server Enterprise\n");
    if (VER_SUITE_EMBEDDEDNT & osVersionInfo.wSuiteMask)
        suitesWritten += snprintf(&installedSuites[suitesWritten], max(0, suitesBufSize - suitesWritten), "Windows XP Embedded\n");
    if (VER_SUITE_PERSONAL & osVersionInfo.wSuiteMask)
        suitesWritten += snprintf(&installedSuites[suitesWritten], max(0, suitesBufSize - suitesWritten), "Windows Vista/XP Home\n");
    if (VER_SUITE_SINGLEUSERTS & osVersionInfo.wSuiteMask)
        suitesWritten += snprintf(&installedSuites[suitesWritten], max(0, suitesBufSize - suitesWritten), "Remote desktop\n");
    if (VER_SUITE_STORAGE_SERVER & osVersionInfo.wSuiteMask)
        suitesWritten += snprintf(&installedSuites[suitesWritten], max(0, suitesBufSize - suitesWritten), "Windows Storage Server 2003\n");
    if (VER_SUITE_WH_SERVER & osVersionInfo.wSuiteMask)
        suitesWritten += snprintf(&installedSuites[suitesWritten], max(0, suitesBufSize - suitesWritten), "Windows Home Server\n");
    if ((VER_SUITE_SMALLBUSINESS & osVersionInfo.wSuiteMask) && (VER_SUITE_SMALLBUSINESS_RESTRICTED & osVersionInfo.wSuiteMask))
        suitesWritten += snprintf(&installedSuites[suitesWritten], max(0, suitesBufSize - suitesWritten), "Microsoft Small Business Server\n");

    if (suitesWritten > 0) {
        clog_ArenaAppend(&scratch, "\nInstalled suites:\n%s", installedSuites);
    }
    */
}

#ifdef STANDALONE
int main(int argc, char *argv[]) {
    clog_ArenaState *st = clog_ArenaMake(0x10000);
    clog_osversion(st->Memory);
    printf("%s", st->Start);
}
#endif