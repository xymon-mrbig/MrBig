#include "mrbig.h"

#define WIDTH 15

#if defined (_M_IA64)
#define CPU "Itanium"
#elif defined (_M_IX86)
#define CPU "x86"
#elif defined (_M_X64)
#define CPU "x64"
#else
#define CPU "unknown"
#endif

/* cpu

green Tue Jul 06 02:43:47 VS 2004 [ntserver] up: 3 days, 1 users, 16 procs, load=1%, PhysicalMem: 64MB(53%)



Memory Statistics
Total Physical memory:            66510848 bytes (64.00MB)
Available Physical memory:        31428608 bytes (30.00MB)
Total PageFile size:             123473920 bytes (118.00MB)
Available PageFile size:          93876224 bytes (90.00MB)
Total Virtual memory size:      2147352576 bytes (2.00GB)
Available Virtual memory size:  2121302016 bytes (1.98GB)

Most active processes
00.27%	csrss (0x1a [26])
00.14%	bbnt (0x31 [49])
00.06%	System (0x2 [2])
00.01%	services (0x29 [41])
*/


static long get_uptime(void)
{
	int object = 2;
	DWORD counters[] = {674, 0};
	LONG result;
	struct perfcounter *pc;
	long long perf_time, perf_freq;

	if (debug > 1) mrlog("get_uptime()");
	pc = read_perfcounters(object, counters, &perf_time, &perf_freq);
	if (pc) {
		result = (perf_time-pc[0].value[0])/perf_freq;
	} else {
		mrlog("Can't read_perfcounters(2, 674)");
		result = 0;
	}
	free_perfcounters(pc);
	if (debug > 1) mrlog("get_uptime returns %ld", result);
	return result;
}


static long pscount(void)
{
	/* NT4 and up, requires psapi.dll */
	DWORD aProcesses[1024], cbNeeded;

	if (debug > 1) mrlog("pscount()");
	if (!EnumProcesses(aProcesses, sizeof(aProcesses), &cbNeeded)) {
		mrlog("Can't EnumProcesses");
		return 0;
	}
	return cbNeeded / sizeof aProcesses[0];
}


static long users(void)
{
	DWORD object = 330;
	DWORD counters[] = {314, 0};
	struct perfcounter *pc;
	long result;

	if (debug > 1) mrlog("users()");

	pc = read_perfcounters(object, counters, NULL, NULL);
	if (pc) {
		result = pc[0].value[0];
	} else {
		mrlog("Can't read_perfcounters(330, 314)");
		result = 0;
	}
	free_perfcounters(pc);
	return result;
}

static int get_load(int version)
{
	/* to measure the sample period */
	/* in units of 1 second */
	static time_t time0 = 0;
	time_t time1;

	/* to measure the processor time */
	/* in units of 100 ns = 1/10000000 s */
	static long long proc0 = 0;
	long long proc1;

	struct perfcounter *perfc;
	int i;
	int load = 0;
	double pct;

	if (debug > 1) mrlog("get_load(%d)", version);

	if (version >= 5) {	/* W2K and up */
		DWORD counters[] = {6, 0};
		perfc = read_perfcounters(238, counters, NULL, NULL);
		if (perfc == NULL) return 0;
		for (i = 0; perfc[0].instance; i++) {
			if (!strcmp(perfc[i].instance, "_Total")) break;
		}
		if (perfc[i].instance == NULL) {
			proc1 = 0;	/* No data found */
		} else {
			proc1 = perfc[i].value[0];
		}
	} else {		/* NT4 */
		DWORD counters[] = {240, 0};
		perfc = read_perfcounters(2, counters, NULL, NULL);
		if (perfc == NULL) return 0;
		proc1 = perfc->value[0];
	}
	time1 = time(NULL);
	pct = proc1-proc0;
	if (proc0 && time1 > time0) {
		/* we need two samples! */
		load = 100-(proc1-proc0)/100000/(time1-time0);
	} else {
		load = 0;
	}
	time0 = time1;
	proc0 = proc1;
	free_perfcounters(perfc);
	// sanity check
	if (load < 0) load = 0;
	return load;
}

static void append_limits(char *a, size_t n)
{
	snprcat(a, n, "\nLimits:\n");
	snprcat(a, n, "Yellow uptime: %d minutes\n", bootyellow);
	snprcat(a, n, "Red uptime: %d minutes\n", bootred);
	snprcat(a, n, "Yellow CPU: %d%%\n", cpuyellow);
	snprcat(a, n, "Red CPU: %d%%\n", cpured);
}

void cpu(void)
{
	char b[5000];
	int n = sizeof b;
	char r[1000];
	char up[100];
	char *color = "green";
	long ut;
	int um;
	int uc;
	int load;
	int pc;
	MEMORYSTATUSEX statex;
	OSVERSIONINFO osvi;
	DWORD memusage;

	if (debug > 1) mrlog("cpu(%p, %d)", b, n);

	if (get_option("no_cpu", 0)) {
		mrsend(mrmachine, "cpu", "clear", "option no_cpu\n");
		return;
	}

	ut = get_uptime();
	um = ut/60;
	uc = users();
	load = 0;
	pc = pscount();

	ZeroMemory(&osvi, sizeof osvi);
	osvi.dwOSVersionInfoSize = sizeof(OSVERSIONINFO);
	if (!GetVersionEx(&osvi) || osvi.dwPlatformId != VER_PLATFORM_WIN32_NT) {
		/* Failed, boo hiss */
		b[0] = '\0';
		snprcat(b, n,
			"No version info\n"
			"%s %s (%s)\n",
			PACKAGE, VERSION, CPU);
		mrsend(mrmachine, "cpu", "red", b);
		return;
	}

	r[0] = '\0';
	statex.dwLength = sizeof statex;
	//GlobalMemoryStatus(&stat);
	GlobalMemoryStatusEx(&statex);
	if (um < bootred) {
		snprcat(r, sizeof r, "&red Machine recently rebooted\n");
		color = "red";
	} else if (um < bootyellow) {
		snprcat(r, sizeof r, "&yellow Machine recently rebooted\n");
		color = "yellow";
	}
	up[0] = '\0';
	if (um < 24*60) {
		snprcat(up, sizeof up, "%02d:%02d", um/60, um%60);
	} else {
		snprcat(up, sizeof up, "%d days", um/(24*60));
	}
	load = get_load(osvi.dwMajorVersion);
	if (load >= cpured) {
		color = "red";
	} else if (load >= cpuyellow && !strcmp(color, "green")) {
		color = "yellow";
	}
#if 0
	memusage = 100-stat.dwAvailPhys/(stat.dwTotalPhys/100);
#else
	memusage = statex.dwMemoryLoad;
#endif
	b[0] = '\0';
	snprcat(b, n,
		"%s up: %s, %d users, %d procs, load=%d%%, PhysicalMem: %ldMB (%d%%)\n%s\n"
		"Memory Statistics\n"
		"Total physical memory:         %*.0f bytes\n"
		"Available physical memory:     %*.0f bytes\n"
		"Total pagefile size:           %*.0f bytes\n"
		"Available pagefile size:       %*.0f bytes\n"
		"Total virtual memory:          %*.0f bytes\n"
		"Available virtual memory size: %*.0f bytes\n\n"
		"Windows version %d.%d\n"
		"%s %s (%s)\n",
		now, up, uc, pc, load,
		(long)(statex.ullTotalPhys / (1024*1024)),
		(int)memusage,
		r,
#if 0
		WIDTH, (double)stat.dwTotalPhys,
		WIDTH, (double)stat.dwAvailPhys,
		WIDTH, (double)stat.dwTotalPageFile,
		WIDTH, (double)stat.dwAvailPageFile,
		WIDTH, (double)stat.dwTotalVirtual,
		WIDTH, (double)stat.dwAvailVirtual,
#else
		WIDTH, (double)statex.ullTotalPhys,
		WIDTH, (double)statex.ullAvailPhys,
		WIDTH, (double)statex.ullTotalPageFile,
		WIDTH, (double)statex.ullAvailPageFile,
		WIDTH, (double)statex.ullTotalVirtual,
		WIDTH, (double)statex.ullAvailVirtual,
#endif
		(int)osvi.dwMajorVersion, (int)osvi.dwMinorVersion,
		PACKAGE, VERSION, CPU);
	append_limits(b, n);
	mrsend(mrmachine, "cpu", color, b);
}
