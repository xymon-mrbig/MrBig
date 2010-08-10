#include "mrbig.h"

static char **get_args(EVENTLOGRECORD *pBuf)
{
	char *cp;
	WORD arg_count;
	char **args = NULL;

	if (pBuf->NumStrings == 0) return NULL;

#if 0
	args = GlobalAlloc(GMEM_FIXED, sizeof(char *) * pBuf->NumStrings);
#else
	args = big_malloc("get_args", sizeof(char *) * pBuf->NumStrings);
#endif
	cp = (char *)pBuf + (pBuf->StringOffset);

	for (arg_count=0; arg_count<pBuf->NumStrings; arg_count++) {
		args[arg_count] = cp;
		cp += strlen(cp) + 1;
	}
	return args;
}

static BOOL get_module_from_source(char *log,
	char *source_name, char *entry_name, 
	char *expanded_name)
{
	DWORD lResult;
	DWORD module_name_size;
	char module_name[1000];
	HKEY hAppKey = NULL;
	HKEY hSourceKey = NULL;
	BOOL bReturn = FALSE;
	char key[1024];

	snprintf(key, sizeof key,
		"SYSTEM\\CurrentControlSet\\Services\\EventLog\\%s", log);

	lResult = RegOpenKeyEx(HKEY_LOCAL_MACHINE, key, 0, KEY_READ, &hAppKey);

	if (lResult != ERROR_SUCCESS) {
		printf("registry can not open.\n");
		goto Exit;
	}

	lResult = RegOpenKeyEx(hAppKey, source_name, 0, KEY_READ, &hSourceKey);

	if (lResult != ERROR_SUCCESS) goto Exit;

	module_name_size = 1000;

	lResult = RegQueryValueEx(hSourceKey, "EventMessageFile",
		NULL, NULL, module_name, &module_name_size);

	if (lResult != ERROR_SUCCESS) goto Exit;

	ExpandEnvironmentStrings(module_name, expanded_name, 1000);

	bReturn = TRUE;

Exit:
	if (hSourceKey != NULL) RegCloseKey(hSourceKey);
	if (hAppKey != NULL) RegCloseKey(hAppKey);

	return bReturn;
}

static BOOL disp_message(char *log, char *source_name, char *entry_name,
	char **args, DWORD MessageId,
	char *msgbuf, size_t msgsize)
{
	BOOL bResult;
	BOOL bReturn = FALSE;
	HANDLE hSourceModule = NULL;
	char source_module_name[1000];
	char *pMessage = NULL;

	bResult = get_module_from_source(log,
			source_name, entry_name, source_module_name);
	if (!bResult) goto Exit;

	/* Sometimes source_module_name will come back as a list of libraries,
	   i.e. this.dll;that.dll;someother.dll. That makes LoadLibraryEx
	   fail and no messages can be formatted. This ugly hack removes all
	   but the first dll so at least that one is loaded.
	*/
	if (1) {
		char *p = strchr(source_module_name, ';');
		if (p) {
			*p = '\0';
		}
	}

	hSourceModule = LoadLibraryEx(source_module_name, NULL,
		DONT_RESOLVE_DLL_REFERENCES | LOAD_LIBRARY_AS_DATAFILE);

	if (hSourceModule == NULL) goto Exit;

	FormatMessage(
		FORMAT_MESSAGE_ALLOCATE_BUFFER |
		FORMAT_MESSAGE_FROM_HMODULE |
		FORMAT_MESSAGE_ARGUMENT_ARRAY,
		hSourceModule,
		MessageId,
		MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
		(LPSTR)&pMessage,
		0,
		(va_list *)args);

	bReturn = TRUE;

Exit:
	if (pMessage != NULL) snprintf(msgbuf, msgsize, "%s", pMessage);
	else snprintf(msgbuf, msgsize, "(%d)\n", (int)MessageId);

	if (hSourceModule != NULL) FreeLibrary(hSourceModule);
	if (pMessage != NULL) LocalFree(pMessage);

	return bReturn;
}

struct event *read_log(char *log, int maxage)
{
	struct event *e = NULL, *p;
	DWORD BufSize;
	DWORD ReadBytes;
	DWORD NextSize;
	BOOL bResult;
	char *cp;
	char *pSourceName;
	char *pComputerName;
	HANDLE hEventLog = NULL;
	EVENTLOGRECORD *pBuf = NULL;
	char **args = NULL;
	char msgbuf[1024];

	hEventLog = OpenEventLog(NULL, log);

	if(hEventLog == NULL) {
		printf("event log can not open.\n");
		goto Exit;
	}

	for(;;) {
		BufSize = 1;
		pBuf = big_malloc("read_log (pBuf)", BufSize);

		bResult = ReadEventLog(
			hEventLog,
			EVENTLOG_BACKWARDS_READ |
			EVENTLOG_SEQUENTIAL_READ,
			0,
			pBuf,
			BufSize,
			&ReadBytes,
			&NextSize);

		if (!bResult && GetLastError() != ERROR_INSUFFICIENT_BUFFER)
			break;

		BufSize = NextSize;
		pBuf = big_realloc("read_log (pBuf)", pBuf, BufSize);

		bResult = ReadEventLog(
			hEventLog,
			EVENTLOG_BACKWARDS_READ |
			EVENTLOG_SEQUENTIAL_READ,
			0,
			pBuf,
			BufSize,
			&ReadBytes,
			&NextSize);

		if(!bResult) break;

		if (pBuf->TimeGenerated < maxage) {
			//printf("Too old\n");
			goto Next;
		}
		p = big_malloc("read_log (node)", sizeof *p);
		p->next = e;
		e = p;
		e->record = pBuf->RecordNumber;
		e->gtime = pBuf->TimeGenerated;
		e->wtime = pBuf->TimeWritten;
		e->id = pBuf->EventID;
		e->type = pBuf->EventType;

		cp = (char *)pBuf;
		cp += sizeof(EVENTLOGRECORD);

		pSourceName = cp;
		cp += strlen(cp)+1;

		pComputerName = cp;
		cp += strlen(cp)+1;

		e->source = big_strdup("read_log (source)", pSourceName);

		args = get_args(pBuf);

		disp_message(log, pSourceName, "EventMessageFile",
			args, pBuf->EventID, msgbuf, sizeof msgbuf);
		e->message = big_strdup("read_log (message)", msgbuf);

		big_free("read_log (args)", args);
		args = NULL;

	Next:
		big_free("read_log (pBuf)", pBuf);
		pBuf = NULL;
	}

Exit:
	big_free("read_log (pBuf)", pBuf);
	big_free("read_log (args)", args);
	if (hEventLog) CloseEventLog(hEventLog);

	return e;
}

void free_log(struct event *e)
{
	struct event *p;

	while (e) {
		p = e;
		e = p->next;
		big_free("free_log(source)", p->source);
		big_free("free_log(message)", p->message);
		big_free("free_log(node)", p);
	}
}

void print_log(struct event *e)
{
	if (e == NULL) {
		printf("No messages\n");
		return;
	}

	while (e) {
		printf("Record Number: %d\n", (int)e->record);
		printf("Time Generated: %s", ctime(&e->gtime));
		printf("Time Written: %s", ctime(&e->wtime));
		printf("Event ID: %d\n", (int)&e->id);
		printf("Event Type: ");
		switch(e->type) {
		case EVENTLOG_SUCCESS:
			printf("Success\n");
			break;
		case EVENTLOG_ERROR_TYPE:
			printf("Error\n");
			break;
		case EVENTLOG_WARNING_TYPE:
			printf("Warning\n");
			break;
		case EVENTLOG_INFORMATION_TYPE:
			printf("Information\n");
			break;
		case EVENTLOG_AUDIT_SUCCESS:
			printf("Audit success\n");
			break;
		case EVENTLOG_AUDIT_FAILURE:
			printf("Audit failure\n");
			break;
		default:
			printf("Unknown\n");
			break;
		}
		printf("Source: %s\n", e->source);
		printf("Message: %s\n", e->message);
		printf("\n");
		e = e->next;
	}
}

#if 0
int main(int argc, char **argv)
{
	int maxage = 360000;	/* 100h is really old... */
	struct event *app, *sys, *sec;

	time_t now = time(NULL);
	app = read_log("Application", now-maxage);
	sys = read_log("System", now-maxage);
	sec = read_log("Security", now-maxage);
	printf("Application\n\n");
	print_log(app);
	printf("\n\nSystem\n\n");
	print_log(sys);
	printf("\n\nSecurity\n\n");
	print_log(sec);
	free_log(app);
	free_log(sys);
	free_log(sec);
	return 0;
}
#endif
