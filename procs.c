#include "mrbig.h"

/* procs

green Tue Jul 06 02:43:47 VS 2004 [ntserver] All processes are OK

 WinVNC OK
 bbnt OK
*/


//  Forward declarations:
static BOOL GetProcessList(void);
//static void printError(TCHAR * msg);

struct proc {
	char *name;
	int count;
	struct proc *next;
} *plist;

struct cfg {
	char *name;
	int min, max;
	char *machine;
	struct cfg *next;
} *pcfg;

struct report {
	char str[5000];
	char *machine;
	char *color;
	struct report *next;
} *preports_procs;

static void read_proccfg(/*char *p*/)
{
	struct cfg *pc;
	char b[100], name[100];
	char machine[100];
	int i, min, max, n;

	pcfg = NULL;
	for (i = 0; get_cfg("procs", b, sizeof b, i); i++) {
		machine[0] = 0;
		name[0] = 0;
		if (b[0] == '#') continue;
		if (b[0] == '"') {
			n = sscanf(b+1, "%[^\"]\" %d %d %s", name, &min, &max, machine);
		} else {
			n = sscanf(b, "%s %d %d %s", name, &min, &max, machine);
		}
		if (n < 1) continue;
		if (n < 2) min = 1;
		if (n < 3) max = min;
		pc = big_malloc("read_proccfg (node)", sizeof *pc);
		pc->name = big_strdup("read_proccfg (name)", name);
		pc->min = min;
		pc->max = max;
		if (strlen(machine) > 0) {
			char *p;
			for (p = machine; *p; p++) {
				if (*p == '.') *p = ',';
			}
			pc->machine = big_strdup("procs (machine)", machine);
		}
		else
			pc->machine = big_strdup("procs (machine)", mrmachine);
		pc->next = pcfg;
		pcfg = pc;
	}
}

static struct proc *lookup_procname(char *p)
{
	struct proc *pl;

	for (pl = plist; pl; pl = pl->next) {
		if (!strcasecmp(pl->name, p)) {
			return pl;
		}
	}
	pl = big_malloc("lookup_procname (node)", sizeof *pl);
	pl->name = big_strdup("lookup_procname (name)", p);
	pl->next = plist;
	pl->count = 0;
	plist = pl;
	return pl;
}

static void store_procname(char *p)
{
	struct proc *pl = lookup_procname(p);

	pl->count++;
}

void procs(void)
{
	char b[5000];
	int n = sizeof b;
	char cfgfile[1024];
	struct proc *pl;
	struct cfg *pc;
	int m, running, unique;
	plist = NULL;
	struct report *rep;
	char *mycolor;

	preports_procs = big_malloc("procs (report)", sizeof(struct report));
	preports_procs->machine = big_strdup("procs (report->machine)", mrmachine);
	preports_procs->next = NULL;
	preports_procs->str[0] = 0;
	preports_procs->color = "green";

	if (debug > 1) mrlog("procs(%p, %d)", b, n);

	if (get_option("no_procs", 0)) {
		mrsend(mrmachine, "procs", "clear", "option no_procs\n");
		return;
	}

	cfgfile[0] = '\0';
	snprcat(cfgfile, sizeof cfgfile, "%s%c%s", cfgdir, dirsep, "procs.cfg");
	read_cfg("procs", cfgfile);
	read_proccfg(/*cfgfile*/);
	GetProcessList();

	while (pcfg) {
		pc = pcfg;
		pcfg = pc->next;
		pl = lookup_procname(pc->name);
		m = pl->count;
		if (m < pc->min || m > pc->max) {
			mycolor = "red";
		} else {
			mycolor = "green";
		}
//		p[0] = '\0';

		for(rep = preports_procs; rep; rep = rep->next) {
			if (!strcmp(pc->machine, rep->machine)) {
				break;
			}
		}

		if (rep == NULL) {
			rep = big_malloc("procs (report)", sizeof(struct report));
			rep->next = preports_procs;
			rep->machine = big_strdup("procs (report->machine)", pc->machine);
			rep->str[0] = 0;
			rep->color = "green";
			preports_procs = rep;
		}
		if (strcmp(mycolor, "green")) {
			// if any process is non-green, report goes red
			rep->color = "red";
		}
		snprcat(rep->str, sizeof rep->str, "&%s %s - %d running (min %d, max %d)\n",
			mycolor, pc->name, m, pc->min, pc->max);
//		strlcat(q, p, sizeof q);
		big_free("procs (pc->name)", pc->name);
		big_free("procs (pc->machine)", pc->machine);
		big_free("procs (pc)", pc);
	}
	running = 0;
	unique = 0;
	while (plist) {
		pl = plist;
		plist = pl->next;
		if (debug > 1) {
			mrlog("Found %d instances of process '%s'", pl->count, pl->name);
		}
		running += pl->count;
		unique++;
		big_free("procs (pl->name)", pl->name);
		big_free("procs (pl)", pl);
	}

	rep = preports_procs;
	while (rep) {
		struct report* prev;
		b[0] = '\0';
		snprcat(b, n, "%s\n\n%s\nTotal %d processes running (%d unique)\n",
			now, rep->str, running, unique);
		mrsend(rep->machine, "procs", rep->color, b);
		prev = rep;
		rep = rep->next;
		big_free("procs (report->machine)", prev->machine);
		big_free("procs (report)", prev);
	}

}


/* Works for NT 4 and up; requires psapi.dll */
void PrintProcessNameAndID( DWORD processID )
{
    TCHAR szProcessName[MAX_PATH] = TEXT("<unknown>");

    // Get a handle to the process.

    HANDLE hProcess = OpenProcess( PROCESS_QUERY_INFORMATION |
                                   PROCESS_VM_READ,
                                   FALSE, processID );

    // Get the process name.

    if (NULL != hProcess )
    {
        HMODULE hMod;
        DWORD cbNeeded;

        if ( EnumProcessModules( hProcess, &hMod, sizeof(hMod),
             &cbNeeded) )
        {
            GetModuleBaseName( hProcess, hMod, szProcessName,
                               sizeof(szProcessName)/sizeof(TCHAR) );
        }
    }

    store_procname(szProcessName);
    CloseHandle( hProcess );
}

static BOOL GetProcessList(void)
{
    // Get the list of process identifiers.

    DWORD aProcesses[1024], cbNeeded, cProcesses;
    unsigned int i;

    if ( !EnumProcesses( aProcesses, sizeof(aProcesses), &cbNeeded ) )
        return FALSE;

    // Calculate how many process identifiers were returned.

    cProcesses = cbNeeded / sizeof(DWORD);

    // Print the name and process identifier for each process.

    for ( i = 0; i < cProcesses; i++ )
        PrintProcessNameAndID( aProcesses[i] );
    return TRUE;
}

#if 0	/* unused */
static void printError(TCHAR * msg)
{
	DWORD eNum;
	TCHAR sysMsg[256];
	TCHAR *p;

	eNum = GetLastError();
	FormatMessage(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, NULL, eNum, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),	// Default language
		      sysMsg, 256, NULL);

	// Trim the end of the line and terminate it with a null
	p = sysMsg;
	while ((*p > 31) || (*p == 9))
		++p;
	do {
		*p-- = 0;
	} while ((p >= sysMsg) && ((*p == '.') || (*p < 33)));

	// Display the message
	printf("\n  WARNING: %s failed with error %d (%s)", msg,
	       (int) eNum, sysMsg);
}
#endif
