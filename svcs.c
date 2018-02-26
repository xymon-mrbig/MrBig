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
	char *machine;
	int status;
	struct svc *next;
} *slist, *scfg;

struct report {
	char str[5000];
	char *machine;
	char *color;
	struct report *next;
} *preports;

static void read_svccfg(void)
{
	struct svc *pc;
	char b[256], name[256];
	int i, status, n;
	char machine[256];

	scfg = NULL;
	cfg_mode = 0;
	for (i = 0; get_cfg("svcs", b, sizeof b, i); i++) {
		machine[0] = 0;
		name[0] = 0;
		status = 0;
		if (b[0] == '#') continue;
		if (b[0] == '"') {	/* display name */
			char *p = b;
			p++;
			strlcpy(name, strtok(p,"\""), sizeof(name));
			p = strtok(NULL, "");
			if (p)
				n = sscanf(p, "%d %s", &status, machine);
			if (n < 1) status = SERVICE_RUNNING;
			cfg_mode |= CFG_DISPLAY_NAME;
		} else {
			n = sscanf(b, "%s %d %s", name, &status, machine);
			if (n < 1) continue;
			if (n < 2) status = SERVICE_RUNNING;
			cfg_mode |= CFG_SERVICE_NAME;
		}
		pc = big_malloc("svcs.c/read_svccfg (node)", sizeof *pc);
		pc->name = big_strdup("svcs/read_svccfg (name)", name);
		pc->status = status;
		if (strlen(machine) > 0) {
			char *p;
			for (p = machine; *p; p++) {
				if (*p == '.') *p = ',';
			}
			pc->machine = big_strdup("svcs.c/read_svccfg (machine)", machine);
		}
		else
			pc->machine = big_strdup("svcs.c/read_svccfg (machine)", mrmachine);
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


void svcs(void)
{
	char b[5000];
	int n = sizeof b;
	struct svc *pc, *pl;

	char cfgfile[1024];
	int i, running = 0;
	char *mycolor;
	BYTE services[65536];
	ENUM_SERVICE_STATUS_PROCESS *svc;
	DWORD needed = 0, nsvcs = 0, resume = 0;
	SC_HANDLE sc;
	struct report *rep;

	preports = big_malloc("svcs (report)", sizeof(struct report));
	preports->machine = big_strdup("svcs (report->machine)", mrmachine);
	preports->next = NULL;
	preports->str[0] = 0;
	preports->color = "green";

	if (debug > 1) mrlog("svcs(%p, %d)", b, n);

	if (get_option("no_svcs", 0)) {
		mrsend(mrmachine, "svcs", "clear", "option no_svcs\n");
		return;
	}

	cfgfile[0] = '\0';
	snprcat(cfgfile, sizeof cfgfile, "%s%c%s", cfgdir, dirsep, "services.cfg");
	read_cfg("svcs", cfgfile);
	read_svccfg();

	if ((cfg_mode & CFG_DISPLAY_NAME) && (cfg_mode & CFG_SERVICE_NAME)) {
		preports->color = "red";
		snprcat(preports->str, sizeof(preports->str), "%s\n",
			"Config using mix of service and display names\n");
	}

	sc = OpenSCManager(NULL, NULL, SC_MANAGER_ENUMERATE_SERVICE);
	if (sc == NULL) {
		preports->color = "red";
		snprcat(preports->str, sizeof preports->str, "%s\n",
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

			for(rep = preports; rep; rep = rep->next) {
				if (!strcmp(pc->machine, rep->machine)) {
					break;
				}
			}
			if (rep == NULL) {
				rep = big_malloc("svcs (report)", sizeof(struct report));
				rep->next = preports;
				rep->machine = big_strdup("svcs (pc->machine)", pc->machine);
				rep->str[0] = 0;
				rep->color = "green";
				preports = rep;
			}

			snprcat(rep->str, sizeof rep->str,
				"&%s %s - status %d (expected %d)\n",
				mycolor, pl->name, pl->status, pc->status);
			if (strcmp(mycolor, "green"))
				rep->color = "red";

			big_free("svcs (pc->name)", pc->name);
			big_free("svcs (pc->machine)", pc->machine);
			big_free("svcs (pc)", pc);
		}
		while (slist) {
			pl = slist;
			slist = pl->next;
			big_free("svcs (pl->name)", pl->name);
			big_free("svcs (pl)", pl);
		}
	} else {
		preports->color = "red";
		snprcat(preports->str, sizeof(preports->str),
			"Can't get service list");
	}
	CloseServiceHandle(sc);

	rep = preports;
	while (rep) {
		struct report* prev;
		b[0] = '\0';
		snprcat(b, n, "%s\n\n%s\n"
			"Total %d registered services, %d running\n\n"
			"%d = Not installed\n"
			"%d = Stopped\n"
			"%d = Start pending\n"
			"%d = Stop pending\n"
			"%d = Running\n"
			"%d = Continue pending\n"
			"%d = Pause pending\n"
			"%d = Paused\n",
			now, rep->str, (int)nsvcs, running,
			0, SERVICE_STOPPED, SERVICE_START_PENDING,
			SERVICE_STOP_PENDING, SERVICE_RUNNING,
			SERVICE_CONTINUE_PENDING, SERVICE_PAUSE_PENDING,
			SERVICE_PAUSED);
		mrsend(rep->machine, "svcs", rep->color, b);
		prev = rep;
		rep = rep->next;
		big_free("svcs", prev->machine);
		big_free("svcs", prev);
	}
}

