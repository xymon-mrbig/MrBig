#include "mrbig.h"

//#define DEBUGGING "jajamensan"

#ifdef DEBUGGING
char cfgdir[256];
int dirsep = '\\';

void mrlog(char *p)
{
	printf("mrlog: %s\n", p);
}
#endif

static void pickup_file(char *fn)
{
	char full_fn[256], machname[256], testname[256];
	char *b, *s, *p;
	FILE *fp;
	size_t n;

	if (debug) mrlog("pickup_file(%s)", fn);

	if (get_option("no_ext", 0)) {
		/* no mrsend with clear status because we don't
		   know the names of the tests
		*/
		return;
	}

	snprintf(full_fn, sizeof(full_fn), "%s%c%s", pickupdir, dirsep, fn);

	p = strchr(fn, '.');
	if (p) {
		*p++ = 0;
		strncpy(machname, fn, sizeof(machname));
		strncpy(testname, p, sizeof(testname));
	} else {
		strncpy(machname, mrmachine, sizeof(machname));
		strncpy(testname, fn, sizeof(machname));
	}
	machname[sizeof(machname)-1] = 0;
	testname[sizeof(testname)-1] = 0;
	if (debug) {
		mrlog("pickup_file(%s)", fn);
		mrlog("Full path '%s'", full_fn);
		mrlog("machname = '%s'", machname);
		mrlog("testname = '%s'", testname);
	}
	fp = big_fopen("pickup_file", full_fn, "r");
	if (!fp) {
		mrlog("Can't pick up file");
		goto Exit;
	}

	b = big_malloc("pickup_file", report_size);
	s = big_malloc("pickup_file", report_size);
	n = fread(b, 1, report_size-1, fp);
	big_fclose("pickup_file", fp);
	remove(full_fn);
	b[n] = 0;
	no_return(b);

	p = strchr(b, '\n');

	if (p == NULL) {
		mrlog("No color in pickup file");
		goto Exit;
	}

	*p++ = 0;

	snprintf(s, report_size, "%s\n\n%s\n", now, p);

	mrsend(machname, testname, b, s);

Exit:
	big_free("pickup_file", b);
	big_free("pickup_file", s);
}

static void pickup(void)
{
	char pattern[1024];
	WIN32_FIND_DATA FindFileData;
	HANDLE hFind;

	pattern[0] = '\0';
	snprcat(pattern, sizeof pattern, "%s%c*", pickupdir, dirsep);

	if (debug) {
		mrlog("pickup()");
		mrlog("picking up from '%s'", pattern);
	}

	hFind = FindFirstFile(pattern, &FindFileData);

	if (hFind == INVALID_HANDLE_VALUE) {
		mrlog("Invalid pickup directory");
		return;
	}

	do {
		if (!(FindFileData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
			pickup_file(FindFileData.cFileName);
	} while (FindNextFile(hFind, &FindFileData));

	FindClose(hFind);
}

void ext_tests(void)
{
	char cfgfile[1024], cmd[1024], *p;
	STARTUPINFO si;
	PROCESS_INFORMATION pi;
	DWORD n;
	int i;

	if (debug > 1) mrlog("ext_tests()");

	cfgfile[0] = '\0';
	snprcat(cfgfile, sizeof cfgfile, "%s%c%s", cfgdir, dirsep, "ext.cfg");
	read_cfg("ext", cfgfile);

	for (i = 0; get_cfg("ext", cmd, sizeof cmd, i); i++) {
		p = strchr(cmd, '\n');
		if (p) *p = '\0';
		if (cmd[0] == '#' || cmd[0] == '\0') continue;
		if (debug) mrlog("Ext test: %s", cmd);
		ZeroMemory(&si, sizeof si);
		ZeroMemory(&pi, sizeof pi);
		if (CreateProcess(NULL, cmd, NULL, NULL, FALSE,
				CREATE_NO_WINDOW|DETACHED_PROCESS,
				NULL, NULL, &si, &pi)) {
			/* Wait no more than two minutes */
			n = WaitForSingleObject(pi.hProcess, 120*1000);
			if (debug) mrlog("WaitForSingleObject returns %d", n);
			if (n == WAIT_TIMEOUT) {
				TerminateProcess(pi.hProcess, EXIT_FAILURE);
				mrlog("Terminating process");
				mrlog(cmd);
			}
			CloseHandle(pi.hProcess);
			CloseHandle(pi.hThread);
		} else {
			mrlog("CreateProcess failed");
		}
	}
	pickup();
}

