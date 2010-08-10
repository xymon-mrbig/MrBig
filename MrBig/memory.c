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

/* memory

green Thu Nov 13 13:35:25 CET 2008 - Memory OK
   Memory              Used       Total  Percentage
&green Physical           1730M       2011M         86%
&green Actual              762M       2011M         37%
&green Swap                  0M       2047M          0%
*/


static void append_limits(char *a, size_t n)
{
	snprcat(a, n, "\nLimits:\n");
	snprcat(a, n, "Yellow RAM: %d\n", memyellow);
	snprcat(a, n, "Red RAM: %d\n", memred);
}

void memory(void)
{
	char b[5000];
	int n = sizeof b;
	char r[1000];
	char *color = "green";
	MEMORYSTATUSEX statex;
	DWORD memusage;
	double pused, ptotal, sused, stotal;
	int ppct, spct;

	if (debug > 1) mrlog("memory(%p, %d)", b, n);

	r[0] = '\0';
	statex.dwLength = sizeof statex;
	//GlobalMemoryStatus(&stat);
	GlobalMemoryStatusEx(&statex);
#if 0
	memusage = 100-stat.dwAvailPhys/(stat.dwTotalPhys/100);
#else
	memusage = statex.dwMemoryLoad;
#endif
	if (memusage > memred) {
		color = "red";
	} else if (memusage > memyellow && !strcmp(color, "green")) {
		color = "yellow";
	}

	ptotal = statex.ullTotalPhys;
	pused = ptotal-statex.ullAvailPhys;
	stotal = statex.ullTotalPageFile;
	sused = stotal-statex.ullAvailPageFile;

	ppct = 100*pused/ptotal;
	spct = 100*sused/stotal;

	b[0] = '\0';
	snprcat(b, n,
		"%s\n\n"
		"   Memory              Used       Total  Percentage\n"
		"&%s Physical           %.0fM       %.0fM         %d%%\n"
		"&%s Swap               %.0fM       %.0fM         %d%%\n",
		now,
		color, pused/1024/1024, ptotal/1024/1024, ppct,
		"green", sused/1024/1024, stotal/1024/1024, spct);
#if 0
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
#endif
	append_limits(b, n);
	mrsend(mrmachine, "memory", color, b);
}

