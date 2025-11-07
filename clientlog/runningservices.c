#include "clientlog.h"
#include <winsvc.h>

LPTSTR runningservices_PrettyStartType(DWORD state) {
    switch (state) {
    case SERVICE_AUTO_START:
        return "Auto";
    case SERVICE_BOOT_START:
        return "Boot";
    case SERVICE_DEMAND_START:
        return "Manual";
    case SERVICE_DISABLED:
        return "None";
    case SERVICE_SYSTEM_START:
        return "System";
    default:
        return "Unknown";
    }
}

LPTSTR runningservices_PrettyServiceStatus(DWORD state) {
    switch (state) {
    case SERVICE_CONTINUE_PENDING:
        return "Unpausing";
    case SERVICE_PAUSE_PENDING:
        return "Pausing";
    case SERVICE_START_PENDING:
        return "Starting";
    case SERVICE_STOP_PENDING:
        return "Stopping";
    case SERVICE_PAUSED:
        return "Paused";
    case SERVICE_RUNNING:
        return "Running";
    case SERVICE_STOPPED:
        return "Stopped";
    default:
        return "Unknown";
    }
}

void clog_runningservices(clog_Arena scratch) {
    SC_HANDLE hServiceManager = OpenSCManager(NULL, SERVICES_ACTIVE_DATABASE, SC_MANAGER_ENUMERATE_SERVICE);
    clog_Defer(&scratch, hServiceManager, RETURN_INT, &CloseServiceHandle);

    BYTE *servicesBuffer = NULL;
    DWORD servicesBufferSize = 0;
    DWORD servicesBytesNeeded = 0;
    DWORD servicesNumReturned = 0;
    DWORD servicesResumeHandle = 0;

    EnumServicesStatusEx(hServiceManager, SC_ENUM_PROCESS_INFO, /*SERVICE_DRIVER |*/ SERVICE_WIN32, SERVICE_STATE_ALL, NULL, 0, &servicesBytesNeeded, &servicesNumReturned, &servicesResumeHandle, NULL);
    servicesBufferSize = servicesBytesNeeded;
    servicesBuffer = clog_ArenaAlloc(&scratch, BYTE, servicesBufferSize);
    BOOL servicesSuccessful = EnumServicesStatusEx(hServiceManager, SC_ENUM_PROCESS_INFO, /*SERVICE_DRIVER |*/ SERVICE_WIN32, SERVICE_STATE_ALL, servicesBuffer, servicesBufferSize, &servicesBytesNeeded, &servicesNumReturned, &servicesResumeHandle, NULL);

    clog_ArenaAppend(&scratch, "[runningservices]");
    clog_ArenaAppend(&scratch, "\n%6s \t%-23s\t%-63s\t%-7s\t%s", "PID", "SERVICE", "DISPLAY NAME", "STARTUP", "STATUS");
    if (!servicesSuccessful) {
        clog_ArenaAppend(&scratch, "(Unable to get services, error code %#010lx)", GetLastError());
    } else {
        CHAR serviceNameBuf[24], displayNameBuf[64];
        DWORD numActive = 0, numInactive = 0;
        LPENUM_SERVICE_STATUS_PROCESS active[servicesNumReturned];
        LPENUM_SERVICE_STATUS_PROCESS inactive[servicesNumReturned];
        LPENUM_SERVICE_STATUS_PROCESS services = (LPENUM_SERVICE_STATUS_PROCESS)servicesBuffer;
        for (DWORD i = 0; i < servicesNumReturned; i++) {
            ENUM_SERVICE_STATUS_PROCESS *service = &services[i];
            if (service->ServiceStatusProcess.dwCurrentState != SERVICE_STOPPED) {
                active[numActive++] = service;
            } else {
                inactive[numInactive++] = service;
            }
        }

        for (DWORD i = 0; i < numActive; i++) {
            ENUM_SERVICE_STATUS_PROCESS service = *active[i];
            SC_HANDLE hService = OpenService(hServiceManager, service.lpServiceName, SERVICE_QUERY_CONFIG);
            clog_Defer(&scratch, hService, RETURN_INT, CloseServiceHandle);
            QueryServiceConfig(hService, NULL, 0, &servicesBytesNeeded);
            BYTE confBytes[servicesBytesNeeded];
            QUERY_SERVICE_CONFIG *conf = (QUERY_SERVICE_CONFIG *)confBytes;
            BOOL queryConfOK = QueryServiceConfig(hService, conf, servicesBytesNeeded, &servicesBytesNeeded);
            if (!queryConfOK) {
                clog_ArenaAppend(&scratch, "\n%6lu \t%-23s\t%-63s\t%-7s\t%s",
                                 service.ServiceStatusProcess.dwProcessId,
                                 clog_utils_ClampString(service.lpServiceName, serviceNameBuf, sizeof serviceNameBuf),
                                 clog_utils_ClampString(service.lpDisplayName, displayNameBuf, sizeof displayNameBuf),
                                 runningservices_PrettyStartType(-1),
                                 runningservices_PrettyServiceStatus(service.ServiceStatusProcess.dwCurrentState));
            } else {
                clog_ArenaAppend(&scratch, "\n%6lu \t%-23s\t%-63s\t%-7s\t%s",
                                 service.ServiceStatusProcess.dwProcessId,
                                 clog_utils_ClampString(service.lpServiceName, serviceNameBuf, sizeof serviceNameBuf),
                                 clog_utils_ClampString(service.lpDisplayName, displayNameBuf, sizeof displayNameBuf),
                                 runningservices_PrettyStartType(conf->dwStartType),
                                 runningservices_PrettyServiceStatus(service.ServiceStatusProcess.dwCurrentState));
            }
            clog_PopDefer(&scratch);
        }

        for (DWORD i = 0; i < numInactive; i++) {
            ENUM_SERVICE_STATUS_PROCESS service = *inactive[i];
            SC_HANDLE hService = OpenService(hServiceManager, service.lpServiceName, SERVICE_QUERY_CONFIG);
            clog_Defer(&scratch, hService, RETURN_INT, CloseServiceHandle);
            QueryServiceConfig(hService, NULL, 0, &servicesBytesNeeded);
            BYTE confBytes[servicesBytesNeeded];
            QUERY_SERVICE_CONFIG *conf = (QUERY_SERVICE_CONFIG *)confBytes;
            BOOL queryConfOK = QueryServiceConfig(hService, conf, servicesBytesNeeded, &servicesBytesNeeded);
            if (!queryConfOK) {
                clog_ArenaAppend(&scratch, "\n%6s \t%-23s\t%-63s\t%-7s\t%s",
                                 "-",
                                 clog_utils_ClampString(service.lpServiceName, serviceNameBuf, sizeof serviceNameBuf),
                                 clog_utils_ClampString(service.lpDisplayName, displayNameBuf, sizeof displayNameBuf),
                                 runningservices_PrettyStartType(-1),
                                 runningservices_PrettyServiceStatus(service.ServiceStatusProcess.dwCurrentState));
            } else if (conf->dwStartType == SERVICE_AUTO_START) {
                clog_ArenaAppend(&scratch, "\n%6s \t%-23s\t%-63s\t%-7s\t%s",
                                 "-",
                                 clog_utils_ClampString(service.lpServiceName, serviceNameBuf, sizeof serviceNameBuf),
                                 clog_utils_ClampString(service.lpDisplayName, displayNameBuf, sizeof displayNameBuf),
                                 runningservices_PrettyStartType(conf->dwStartType),
                                 runningservices_PrettyServiceStatus(service.ServiceStatusProcess.dwCurrentState));
            }
            clog_PopDefer(&scratch);
        }
    }
}

#ifdef STANDALONE
int main(int argc, TCHAR *argv[]) {
    clog_ArenaState *st = clog_ArenaMake(0x10000);
    clog_runningservices(st->Memory);
    clog_PopDeferAll(&st->Memory);
    printf("%s", st->Start);
}
#endif