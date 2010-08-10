#include "mrbig.h"

/* procs

green Tue Jul 06 02:43:47 VS 2004 [ntserver] All processes are OK

 WinVNC OK
 bbnt OK
*/


//  Forward declarations:
static BOOL GetProcessList(void);
static void printError(TCHAR * msg);

struct proc {
	char *name;
	int count;
	struct proc *next;
} *plist;

struct cfg {
	char *name;
	int min, max;
	struct cfg *next;
} *pcfg;

static void read_proccfg(/*char *p*/)
{
	struct cfg *pc;
	char b[100], name[100];
	int i, min, max, n;

	pcfg = NULL;
	for (i = 0; get_cfg("procs", b, sizeof b, i); i++) {
		if (b[0] == '#') continue;
		n = sscanf(b, "%s %d %d", name, &min, &max);
		if (n < 1) continue;
		if (n < 2) min = 1;
		if (n < 3) max = min;
		pc = big_malloc("procs.c/read_proccfg (node)", sizeof *pc);
		pc->name = big_strdup("procs.c/read_proccfg (name)", name);
		pc->min = min;
		pc->max = max;
		pc->next = pcfg;
		pcfg = pc;
	}
}

#if 0	/* Not necessary, because these lists are freed in procs() */
static void free_cfg(struct cfg *pcfg)
{
	struct cfg *p;

	while (pcfg) {
		p = pcfg;
		pcfg = p->next;
		big_free("procs.c/free_cfg (name)", p->name);
		big_free("procs.c/free_cfg (node)", p);
	}
}

static void free_procnames(struct proc *plist)
{
	struct proc *p;

	while (plist) {
		p = plist;
		plist = p->next;
		big_free("free_procnames (name)", p->name);
		big_free("free_procnames (node)", p);
	}
}
#endif

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
	char p[100], q[5000], *color = "green", *mycolor;
	char cfgfile[1024];
	struct proc *pl;
	struct cfg *pc;
	int m, running;
	plist = NULL;

	if (debug > 1) mrlog("procs(%p, %d)", b, n);
	snprintf(cfgfile, sizeof cfgfile, "%s%c%s", cfgdir, dirsep, "procs.cfg");
	read_cfg("procs", cfgfile);
	read_proccfg(/*cfgfile*/);
	GetProcessList();
	q[0] = '\0';
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
		if (strcmp(mycolor, "green"))
			color = mycolor;
		snprintf(p, sizeof p, "&%s %s - %d running (min %d, max %d)\n",
			mycolor, pc->name, m, pc->min, pc->max);
		strlcat(q, p, sizeof q);
		big_free("procs (pc->name)", pc->name);
		big_free("procs (pc)", pc);
	}
	running = 0;
	while (plist) {
		pl = plist;
		plist = pl->next;
		big_free("procs (pl->name)", pl->name);
		big_free("procs (pl)", pl);
		running++;
	}
	snprintf(b, n, "%s\n\n%s\nTotal %d processes running\n",
		now, q, running);
	mrsend(mrmachine, "procs", color, b);
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
