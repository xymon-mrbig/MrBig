#include "mrbig.h"

#define SERVICE_NAME "MrBig"
#define DISPLAY_NAME "Mr Big Monitoring Agent"

SERVICE_STATUS          ServiceStatus; 
SERVICE_STATUS_HANDLE   ServiceStatusHandle; 

VOID  WINAPI ServiceCtrlHandler (DWORD opcode); 
VOID  WINAPI ServiceStart (DWORD argc, LPTSTR *argv); 
DWORD ServiceInitialization (DWORD argc, LPTSTR *argv, 
        DWORD *specificError); 
//VOID SvcDebugOut(LPSTR String, DWORD Status);

void DeleteSampleService(SC_HANDLE schSCManager)
{
    SC_HANDLE schService;

    schService = OpenService(schSCManager, SERVICE_NAME, SERVICE_ALL_ACCESS);
    if (schService == NULL) {
        printf("Can't open service (%d)\n", (int)GetLastError());
        return;
    }
    if (!DeleteService(schService)) {
        printf("Can't delete service (%d)\n", (int)GetLastError());
    }
    CloseServiceHandle(schService);
}

int delete_service(void)
{
        SC_HANDLE schSCManager;

	startup_log("delete_service()", 0);

        // Open a handle to the SC Manager database.

        schSCManager = OpenSCManager(
            NULL,                    // local machine
            NULL,                    // ServicesActive database
            SC_MANAGER_ALL_ACCESS);  // full access rights

        if (NULL == schSCManager) {
            printf("OpenSCManager failed (%d)\n", (int)GetLastError());
	    return 0;
	}

        DeleteSampleService(schSCManager);
	return 1;
}

BOOL CreateSampleService(SC_HANDLE schSCManager) 
{ 
    TCHAR szPath[MAX_PATH]; 
    SC_HANDLE schService;
    
    if( !GetModuleFileName( NULL, szPath, MAX_PATH ) )
    {
        printf("GetModuleFileName failed (%d)\n", (int)GetLastError()); 
        return FALSE;
    }

    schService = CreateService( 
        schSCManager,              // SCManager database 
        SERVICE_NAME,              // name of service 
        DISPLAY_NAME,              // service name to display 
        SERVICE_ALL_ACCESS,        // desired access 
        SERVICE_WIN32_OWN_PROCESS, // service type 
        SERVICE_AUTO_START,      // start type 
        SERVICE_ERROR_NORMAL,      // error control type 
        szPath,                    // path to service's binary 
        NULL,                      // no load ordering group 
        NULL,                      // no tag identifier 
        NULL,                      // no dependencies 
        NULL,                      // LocalSystem account 
        NULL);                     // no password 
 
    if (schService == NULL) {
        printf("CreateService failed (%d)\n", (int)GetLastError()); 
        return FALSE;
    } else {
        CloseServiceHandle(schService); 
        return TRUE;
    }
}

int install_service(void)
{
	SC_HANDLE schSCManager;

	// Open a handle to the SC Manager database. 
 
	startup_log("install_service()", 0);

	schSCManager = OpenSCManager( 
	    NULL,                    // local machine 
	    NULL,                    // ServicesActive database 
	    SC_MANAGER_ALL_ACCESS);  // full access rights 
 
	if (NULL == schSCManager) 
	    printf("OpenSCManager failed (%d)\n", (int)GetLastError());

	if (CreateSampleService(schSCManager)) {
		printf("Success\n");
	} else {
		printf("Failure\n");
	}
	return 0;
}

int service_main(int argc, char **argv) 
{ 
   SERVICE_TABLE_ENTRY   DispatchTable[] = 
   { 
      { SERVICE_NAME, ServiceStart      }, 
      { NULL,              NULL          } 
   }; 

   startup_log("service_main(%d, %p)", argc, argv);

   if (!StartServiceCtrlDispatcher( DispatchTable)) 
   { 
      	startup_log(" [MY_SERVICE] StartServiceCtrlDispatcher (%d)\n", 
         GetLastError()); 
   } 
   return 0;
} 
 
//VOID SvcDebugOut(LPSTR String, DWORD Status) 
//{ 
//   char fn[256];
//   strlcpy(fn, "D:\\service.out", sizeof fn);
//   FILE *fp = fopen(fn, "a");
//   if (!fp) return;
//   fprintf(fp, "%s (%ld)\n", String, (long)Status);
//   fclose(fp);
//}


void WINAPI ServiceStart (DWORD argc, LPTSTR *argv) 
{ 
    DWORD status; 
    DWORD specificError; 
 
    startup_log("ServiceStart()");
    ServiceStatus.dwServiceType        = SERVICE_WIN32; 
    ServiceStatus.dwCurrentState       = SERVICE_START_PENDING; 
    ServiceStatus.dwControlsAccepted   =
		SERVICE_ACCEPT_STOP | SERVICE_ACCEPT_PAUSE_CONTINUE; 
    ServiceStatus.dwWin32ExitCode      = 0; 
    ServiceStatus.dwServiceSpecificExitCode = 0; 
    ServiceStatus.dwCheckPoint         = 0; 
    ServiceStatus.dwWaitHint           = 0; 
 
    ServiceStatusHandle = RegisterServiceCtrlHandler( 
        SERVICE_NAME, 
        ServiceCtrlHandler); 
 
    if (ServiceStatusHandle == (SERVICE_STATUS_HANDLE)0) 
    { 
        startup_log(" [MY_SERVICE] RegisterServiceCtrlHandler failed %d\n", GetLastError()); 
        return; 
    } 
 
    // Initialization code goes here. 
    status = ServiceInitialization(argc,argv, &specificError); 
 
    // Handle error condition 
    if (status != NO_ERROR) 
    { 
        ServiceStatus.dwCurrentState       = SERVICE_STOPPED; 
        ServiceStatus.dwCheckPoint         = 0; 
        ServiceStatus.dwWaitHint           = 0; 
        ServiceStatus.dwWin32ExitCode      = status; 
        ServiceStatus.dwServiceSpecificExitCode = specificError; 
 
        SetServiceStatus (ServiceStatusHandle, &ServiceStatus); 
        return; 
    } 
 
    // Initialization complete - report running status. 
    ServiceStatus.dwCurrentState       = SERVICE_RUNNING; 
    ServiceStatus.dwCheckPoint         = 0; 
    ServiceStatus.dwWaitHint           = 0; 
 
    if (!SetServiceStatus (ServiceStatusHandle, &ServiceStatus)) 
    { 
        status = GetLastError(); 
        startup_log(" [MY_SERVICE] SetServiceStatus error %ld\n",status); 
    } 
 
    // This is where the service does its work. 
    startup_log(" [MY_SERVICE] Returning the Main Thread \n",0); 
    mrbig();
 
    return; 
} 
 
// Stub initialization function. 
DWORD ServiceInitialization(DWORD   argc, LPTSTR  *argv, 
    DWORD *specificError) 
{ 
#if 0
    argv; 
    argc; 
    specificError; 
#endif
    return(0); 
}


VOID WINAPI ServiceCtrlHandler (DWORD Opcode) 
{ 
   DWORD status; 
 
   switch(Opcode) 
   { 
      case SERVICE_CONTROL_PAUSE: 
      // Do whatever it takes to pause here. 
         ServiceStatus.dwCurrentState = SERVICE_PAUSED; 
         break; 
 
      case SERVICE_CONTROL_CONTINUE: 
      // Do whatever it takes to continue here. 
         ServiceStatus.dwCurrentState = SERVICE_RUNNING; 
         break; 
 
      case SERVICE_CONTROL_STOP: 
      // Do whatever it takes to stop here. 
	 stop_winsock();
         ServiceStatus.dwWin32ExitCode = 0; 
         ServiceStatus.dwCurrentState  = SERVICE_STOPPED; 
         ServiceStatus.dwCheckPoint    = 0; 
         ServiceStatus.dwWaitHint      = 0; 

         if (!SetServiceStatus (ServiceStatusHandle, 
           &ServiceStatus))
         { 
            status = GetLastError(); 
            startup_log(" [MY_SERVICE] SetServiceStatus error %ld\n", 
               status); 
         } 
 
         startup_log(" [MY_SERVICE] Leaving Service \n",0); 
         return; 
 
      case SERVICE_CONTROL_INTERROGATE: 
      // Fall through to send current status. 
         break; 
 
      default: 
         startup_log(" [MY_SERVICE] Unrecognized opcode %ld\n", 
             Opcode); 
   } 
 
   // Send current status. 
   if (!SetServiceStatus (ServiceStatusHandle,  &ServiceStatus)) 
   { 
      status = GetLastError(); 
      startup_log(" [MY_SERVICE] SetServiceStatus error %ld\n", 
         status); 
   } 
   return; 
}

