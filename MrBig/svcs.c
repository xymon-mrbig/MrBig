#include "mrbig.h"

#define CFG_DISPLAY_NAME 1
#define CFG_SERVICE_NAME 2

/* svcs

green Tue Jul 06 02:43:47 VS 2004 Services OK

 VNC Server - Running
 Server - Running
 Big Brother SNM Client 1.08d - Running
*/

static int cfg_mode;

struct svc {
	char *name;
	int status;
	struct svc *next;
} *slist, *scfg;

static void read_svccfg(void)
{
	struct svc *pc;
	char b[256], name[256], *p;
	int i, status, n;

	scfg = NULL;
	cfg_mode = 0;
	for (i = 0; get_cfg("svcs", b, sizeof b, i); i++) {
		if (b[0] == '#') continue;
		if (b[0] == '"') {	/* display name */
			p = strrchr(b+1, '"');
			if (p) {
				*p++ = '\0';
				n = sscanf(p, "%d", &status);
				if (n < 1) status = SERVICE_RUNNING;
			} else {
				status = SERVICE_RUNNING;
			}
			strlcpy(name, b+1, sizeof name);
			cfg_mode |= CFG_DISPLAY_NAME;
		} else {
			n = sscanf(b, "%s %d", name, &status);
			if (n < 1) continue;
			if (n < 2) status = SERVICE_RUNNING;
			cfg_mode |= CFG_SERVICE_NAME;
		}
		pc = big_malloc("svcs.c/read_svccfg (node)", sizeof *pc);
		pc->name = big_strdup("svcs/read_svccfg (name)", name);
		pc->status = status;
		pc->next = scfg;
		scfg = pc;
	}
}

static struct svc *lookup_svcname(char *p)
{
	struct svc *sl;

	for (sl = slist; sl; sl = sl->next) {
		if (!strcasecmp(sl->name, p)) {
			return sl;
		}
	}
	sl = big_malloc("lookup_svcname (node)", sizeof *sl);
	sl->name = big_strdup("lookup_svcname (name)", p);
	sl->next = slist;
	sl->status = 0;
	slist = sl;
	return sl;
}


void svcs(char *b, int n)
{
	struct svc *pc, *pl;
	char q[5000];
	char cfgfile[1024];
	int i, running = 0;
	char *color = "green", *mycolor;
	BYTE services[65536];
	ENUM_SERVICE_STATUS_PROCESS *svc;
	DWORD needed = 0, nsvcs = 0, resume = 0;
	SC_HANDLE sc;

	if (debug > 1) mrlog("svcs(%p, %d)", b, n);

	snprintf(cfgfile, sizeof cfgfile, "%s%c%s", cfgdir, dirsep, "services.cfg");
	read_cfg("svcs", cfgfile);
	read_svccfg();
	q[0] = '\0';
	if ((cfg_mode & CFG_DISPLAY_NAME) && (cfg_mode & CFG_SERVICE_NAME)) {
		color = "red";
		snprcat(q, sizeof q, "%s\n",
			"Config using mix of service and display names\n");
	}
	sc = OpenSCManager(NULL, NULL, SC_MANAGER_ENUMERATE_SERVICE);
	if (sc == NULL) {
		color = "red";
		snprcat(q, sizeof q, "%s\n",
			"null sc handle");
	} else if (EnumServicesStatusEx(sc, SC_ENUM_PROCESS_INFO,
				    SERVICE_WIN32, SERVICE_STATE_ALL,
				    services, 65536, &needed,
				    &nsvcs, &resume, NULL)) {
		svc = (ENUM_SERVICE_STATUS_PROCESS *)services;
		for (i = 0; i < nsvcs; i++) {
			if (cfg_mode & CFG_SERVICE_NAME) {
				pc = lookup_svcname(svc[i].lpServiceName);
			} else {
				pc = lookup_svcname(svc[i].lpDisplayName);
			}
			pc->status = svc[i].ServiceStatusProcess.dwCurrentState;
			if (pc->status == SERVICE_RUNNING) running++;
		}
		while (scfg) {
			pc = scfg;
			scfg = pc->next;
			pl = lookup_svcname(pc->name);
			if (pl->status != pc->status) {
				mycolor = "red";
			} else {
				mycolor = "green";
			}
			if (strcmp(mycolor, "green"))
				color = mycolor;
			snprcat(q, sizeof q,
				"&%s %s - status %d (expected %d)\n",
				mycolor, pl->name, pl->status, pc->status);
			big_free("svcs (pc->name)", pc->name);
			big_free("svcs (pc)", pc);
		}
		while (slist) {
			pl = slist;
			slist = pl->next;
			big_free("svcs (pl->name)", pl->name);
			big_free("svcs (pl)", pl);
		}
	} else {
		color = "red";
		snprcat(q, sizeof q,
			"Can't get service list");
	}
	CloseServiceHandle(sc);
	snprintf(b, n, "status %s.svcs %s %s\n\n%s\n"
		"Total %d registered services, %d running\n\n"
		"%d = Not installed\n"
		"%d = Stopped\n"
		"%d = Start pending\n"
		"%d = Stop pending\n"
		"%d = Running\n"
		"%d = Continue pending\n"
		"%d = Pause pending\n"
		"%d = Paused\n",
		mrmachine, color, now, q, (int)nsvcs, running,
		0, SERVICE_STOPPED, SERVICE_START_PENDING,
		SERVICE_STOP_PENDING, SERVICE_RUNNING,
		SERVICE_CONTINUE_PENDING, SERVICE_PAUSE_PENDING,
		SERVICE_PAUSED);
}

