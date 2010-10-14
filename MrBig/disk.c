#include "mrbig.h"

/* disk

green Tue Jul 06 02:43:47 VS 2004 [ntserver]


Filesystem   1K-blocks     Used    Avail Capacity  Mounted
C              1249696   813056   436640    65%    /FIXED/C ()
E              1249888     4458  1245430     0%    /FIXED/E ()
*/

struct cfg {
	char *name;
	double yellow, red;
	struct cfg *next;
} *pcfg;

static void read_diskcfg(/*char *p*/)
{
	struct cfg *pc;
	char b[100], name[100];
	int n, i;
	double y, r;

	pcfg = NULL;
	for (i = 0; get_cfg("disk", b, sizeof b, i); i++) {
		if (b[0] == '#') continue;
		n = sscanf(b, "%s %lf %lf", name, &y, &r);
		if (n != 3) continue;
		pc = big_malloc("read_diskcfg (node)", sizeof *pc);
		pc->name = big_strdup("read_diskcfg (name)", name);
		pc->yellow = y;
		pc->red = r;
		pc->next = pcfg;
		pcfg = pc;
	}
}

static void free_cfg(void)
{
	struct cfg *dl;

	while (pcfg) {
		dl = pcfg;
		pcfg = dl->next;
		big_free("free_cfg (name)", dl->name);
		big_free("free_cfg (node)", dl);
	}
}

static void lookup_limits(char *drive, double *yellow, double *red)
{
	struct cfg *pc;

	for (pc = pcfg; pc; pc = pc->next) {
		if (!strcasecmp(pc->name, drive)) {
			*yellow = pc->yellow;
			*red = pc->red;
			return;
		}
	}
	*yellow = dfyellow;
	*red = dfred;
}

static void append_limits(char *a, size_t n)
{
	struct cfg *pc;

#if 0	/* this upsets Hobbit's rrd handler module do_disk.c */
	snprcat(a, n, "\n<b>Limits:</b>\n<table>\n");
	snprcat(a, n, "<tr><th>Drive</th><th>Yellow</th><th>Red</th></tr>\n");
	for (pc = pcfg; pc; pc = pc->next) {
		snprcat(a, n, "<tr><td>%s</td><td>%.1f%%</td><td>%.1f%%</td></tr>\n",
			pc->name, pc->yellow, pc->red);
	}
	snprcat(a, n, "<tr><td>Default</td><td>%.1f%%</td><td>%.1f%%</td></tr>\n",
		dfyellow, dfred);
	snprcat(a, n, "</table>\n");
#else	/* fixed width plain text should be safe */
	snprcat(a, n, "\nLimits:\n");
	snprcat(a, n, "%-10s %-10s %-10s\n", "Drive", "Yellow", "Red");
	for (pc = pcfg; pc; pc = pc->next) {
		snprcat(a, n, "%-10s %-10.1f %-10.1f\n", pc->name, pc->yellow, pc->red);
	}
	snprcat(a, n, "%-10s %-10.1f %-10.1f\n", "Default", dfyellow, dfred);
#endif
}

void disk(void)
{
	char b[5000];
	int n = sizeof b;
	int i, j, res;
	double yellow, red;
	char d[10], q[5000], r[5000], *color = "green";
	char cfgfile[1024], drive[10];
	DWORD drives;
	unsigned int dtype;
	double pct;
	uint64_t total_bytes, free_bytes;
	ULARGE_INTEGER fa, tb, fb;

	if (debug > 1) mrlog("disk(%p, %d)", b, n);

	if (get_option("no_disk", 0)) {
		mrsend(mrmachine, "disk", "clear", "option no_disk\n");
		return;
	}

	drives = GetLogicalDrives();

	cfgfile[0] = '\0';
	snprcat(cfgfile, sizeof cfgfile, "%s%c%s", cfgdir, dirsep, "disk.cfg");
	read_cfg("disk", cfgfile);
	read_diskcfg(/*cfgfile*/);

	r[0] = '\0';
	q[0] = '\0';
	snprcat(q, sizeof q, "%-11s %9s %9s %9s %9s  %s\n",
		"Filesystem", "1M-blocks", "Used", "Avail", "Capacity",
		"Mounted");
	for (i = 'A'; i <= 'Z'; i++) {
		if (drives & 1) {
			drive[0] = d[0] = '\0';
			snprcat(drive, sizeof drive, "%c", i);
			snprcat(d, sizeof d, "%c:\\", i);
			dtype = GetDriveType(d);
			if (debug) mrlog("%s is type %d", d, dtype);
			if (dtype == DRIVE_FIXED) {
				res = GetDiskFreeSpaceEx(d, &fa, &tb, &fb);
				if (debug) mrlog("GetDiskFreeSpaceEx returns %d", res);
				if (res == 0) {
					mrlog("No size info (%d)", GetLastError());
					total_bytes = free_bytes = 0;
					pct = 0;
				} else {
					total_bytes = tb.QuadPart;
					free_bytes = fb.QuadPart;
					pct = 100.0-100.0*free_bytes/total_bytes;
				}
				lookup_limits(drive, &yellow, &red);
				if (pct >= red) {
					snprcat(r, sizeof r,
						"&red %s (%.1f%%) has reached the PANIC level (%.1f%%)\n",
						d, pct, red);
					color = "red";
				} else if (!strcmp(color, "green")
					 && pct >= yellow) {
					snprcat(r, sizeof r,
						"&yellow %s (%.1f%%) has reached the WARNING level (%.1f%%)\n",
						d, pct, yellow);
					color = "yellow";
				}
				j = 0;
				/* Filesystem */
				snprcat(q, sizeof q,
					"%-11c % 9" PRIu64 " % 9" PRIu64 " % 9" PRIu64 " % 8.1f%%  /FIXED/%c\n",
					i,
					(total_bytes / 1024 / 1024),
					((total_bytes - free_bytes) / 1024 / 1024),
					(free_bytes / 1024 / 1024),
					pct, i);
			}
		}
		drives >>= 1;
	}
	b[0] = '\0';
	snprcat(b, n, "%s\n\n%s\n%s\n", now, r, q);
	append_limits(b, n);
	free_cfg();
	mrsend(mrmachine, "disk", color, b);
}
