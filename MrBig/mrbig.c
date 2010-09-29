#include "mrbig.h"

#define MEMSIZE 4
#define PATTERN_SIZE (sizeof big_pattern)
#define CHUNKS_MAX 10000

static char cfgfile[256];
char mrmachine[256], bind_addr[256] = "0.0.0.0";
static struct display {
	struct sockaddr_in in_addr;
	int s;
	char* pdata;
	int remaining;
	struct display *next;
} *mrdisplay;
char cfgdir[256];
char pickupdir[256];
char now[1024];
static FILE *logfp = NULL;
static int mrport, mrsleep, mrloop;
int bootyellow, bootred;
double dfyellow, dfred;
int cpuyellow, cpured;
int memyellow, memred;
int debug = 0;
int dirsep;
int msgage;
int memsize = MEMSIZE;
int standalone = 0;
int report_size = 5000;

/* nosy memory management */
static int debug_memory = 0;

struct memchunk {
	void *p;
	size_t n;
	char cl[20];
} chunks[CHUNKS_MAX];

static unsigned char big_pattern[] = {
	1,2,3,4,5,6,7,8,9,0, 1,2,3,4,5,6,7,8,9,0,
//	1,2,3,4,5,6,7,8,9,0, 1,2,3,4,5,6,7,8,9,0,
//	1,2,3,4,5,6,7,8,9,0, 1,2,3,4,5,6,7,8,9,0,
//	1,2,3,4,5,6,7,8,9,0, 1,2,3,4,5,6,7,8,9,0,
//	1,2,3,4,5,6,7,8,9,0, 1,2,3,4,5,6,7,8,9,0,
//	1,2,3,4,5,6,7,8,9,0, 1,2,3,4,5,6,7,8,9,0,
//	1,2,3,4,5,6,7,8,9,0, 1,2,3,4,5,6,7,8,9,0,
//	1,2,3,4,5,6,7,8,9,0, 1,2,3,4,5,6,7,8,9,0,
//	1,2,3,4,5,6,7,8,9,0, 1,2,3,4,5,6,7,8,9,0,
//	1,2,3,4,5,6,7,8,9,0, 1,2,3,4,5,6,7,8,9,0,
	'1','2','3','4','5','6','7','8','9','0',
//	'1','2','3','4','5','6','7','8','9','0',
//	'1','2','3','4','5','6','7','8','9','0',
//	'1','2','3','4','5','6','7','8','9','0',
//	'1','2','3','4','5','6','7','8','9','0',
	'q','w','e','r','t','y'
};

void mrlog(char *fmt, ...)
{
	FILE *fp;
	va_list ap;
	if (standalone) fp = stderr;
	else fp = logfp;
	if (!fp) return;
	va_start(ap, fmt);
	vfprintf(fp, fmt, ap);
	va_end(ap);
	fprintf(fp, "\n");
	fflush(fp);
}

#if 1
// Use this for logs that need to be written before there is a logfp
void startup_log(char *fmt, ...)
{
	FILE *fp;
	va_list ap;

	fp = fopen("mrlog.log", "a");
	if (!fp) return;
	va_start(ap, fmt);
	vfprintf(fp, fmt, ap);
	va_end(ap);
	fprintf(fp, "\n");
	fclose(fp);
}
#else
void startup_log(char *fmt, ...)
{
	return;
}
#endif

static void mrexit(char *reason, int status)
{
	mrlog("mrexit(%s, %d)", reason, status);
	startup_log("mrexit(%s, %d)", reason, status);
	if (logfp) fclose(logfp);
	exit(status);
}

static int check_chunk(int i)
{
	unsigned char *q = chunks[i].p+chunks[i].n-PATTERN_SIZE;
	if (memcmp(q, big_pattern, PATTERN_SIZE)) {
		mrlog("Chunk %p (%s) has been tampered with",
			chunks[i].p, chunks[i].cl);
		for (i = 0; i < PATTERN_SIZE; i++) {
			if (q[i] != big_pattern[i]) {
				if (isprint(q[i])) {
					mrlog("%d: '%c'", i, (int)q[i]);
				} else {
					mrlog("%d: %d", i, (int)q[i]);
				}
			}
		}
		return 1;
	}
	return 0;
}

void check_chunks(char *msg)
{
	int i, corrupt = 0;
	if (debug) mrlog("check_chunks(%s)", msg);
	for (i = 0; i < CHUNKS_MAX; i++) {
		if (chunks[i].p) {
			corrupt |= check_chunk(i);
		}
	}
	if (corrupt) {
		mrlog("Memory corruption detected");
		mrexit("check_chunks found memory corruption", EXIT_FAILURE);
	} else {
		if (debug) mrlog("Memory checks out OK");
	}
}

static void dump_chunks(void)
{
	int i, n;

	mrlog("Chunks:");
	n = 0;
	for (i = 0; i < CHUNKS_MAX; i++) {
		if (chunks[i].p) {
			mrlog("%d: '%s' (%p) %ld bytes", i,
				chunks[i].cl, chunks[i].p, (long)chunks[i].n);
			n++;
		}
	}
	mrlog("Total %d chunks", n);
}

static void store_chunk(void *p, size_t n, char *cl)
{
	int i;

	if (!debug_memory) return;

	for (i = 0; i < CHUNKS_MAX; i++)
		if (chunks[i].p == NULL) break;
	if (i == CHUNKS_MAX) {
		mrlog("No empty chunk slot for %p (%s), exiting", p, cl);
		dump_chunks();
		mrexit("store_chunk is out of slots", EXIT_FAILURE);
	}
	if (debug >= 3) mrlog("Storing chunk %p (%s) of %ld bytes in slot %d",
				p, cl, (long)n, i);
	chunks[i].p = p;
	chunks[i].n = n;
mrlog("In store_chunk: i = %d", i);
	strlcpy(chunks[i].cl, cl, 20);
mrlog("In store_chunk: i = %d", i);
	memcpy(p+n-PATTERN_SIZE, big_pattern, PATTERN_SIZE);
}

static void remove_chunk(void *p, char *cl)
{
	int i;

	if (!debug_memory) return;

	for (i = 0; i < CHUNKS_MAX; i++)
		if (chunks[i].p == p) break;
	if (i == CHUNKS_MAX) {
		mrlog("Can't find chunk %p (%s)", p, cl);
		dump_chunks();
		mrlog("Continuing even though I can't find chunk %p (%s)",
			p, cl);
	}
	if (check_chunk(i)) mrexit("remove_chunk: check not ok", EXIT_FAILURE);
	if (debug >= 3) {
		mrlog("Removing chunk %p (%s) from slot %d",
			p, chunks[i].cl, i);
	}
	chunks[i].p = NULL;
}

void *big_malloc(char *p, size_t n)
{
	void *a;
	size_t m;

	if (debug_memory) {
		mrlog("big_malloc(%s, %ld)", p, (long)n);
		m = memsize*(n+PATTERN_SIZE);
	} else {
		m = memsize*n;
	}

	a = malloc(m);

	if (debug > 2) {
		mrlog("Allocating %ld bytes (%p) on behalf of %s",
			(long)m, a, p);
	}
	if (a == NULL) {
		mrlog("Allocation '%s' failed, exiting", p);
		mrexit("Out of memory", EXIT_FAILURE);
	}
	store_chunk(a, m, p);
	return a;
}

void *big_realloc(char *p, void *q, size_t n)
{
	void *a;
	size_t m;

	if (debug_memory) {
		mrlog("big_realloc(%s, %p, %ld)", p, q, n);
		m = memsize*(n+PATTERN_SIZE);
	} else {
		m = memsize*n;
	}

	remove_chunk(q, p);
	a = realloc(q, m);

	if (debug > 2) {
		mrlog("Reallocating %ld bytes (%p => %p) on behalf of %s",	
			(long)m, q, a, p);
	}

	if (a == NULL) {
		mrlog("Allocation '%s' failed, exiting", p);
		mrexit("Out of memory", EXIT_FAILURE);
	}
	store_chunk(a, m, p);
	return a;
}

void big_free(char *p, void *q)
{
	if (q == NULL) {
		if (debug) mrlog("No need to free %p on behalf of %s", q, p);
		return;
	}
	if (debug > 2) mrlog("Freeing %p on behalf of %s", q, p);

	remove_chunk(q, p);
	free(q);
}

char *big_strdup(char *p, char *q)
{
	char *a = big_malloc(p, strlen(q)+1);
	return strcpy(a, q);
}

struct filechunk {
	FILE *p;
	char cl[20];
} files[100];

static void dump_files(void)
{
	int i, n;

	mrlog("Files:");
	n = 0;
	for (i = 0; i < 100; i++) {
		if (files[i].p) {
			mrlog("%d: '%s' (%p)", i, files[i].cl, files[i].p);
			n++;
		}
	}
	mrlog("Total %d files", n);
}

static void store_file(FILE *p, char *cl)
{
	int i;

	if (debug < 2) return;
	mrlog("store_file(%p, %s)", p, cl);

	for (i = 0; i < 100; i++)
		if (files[i].p == NULL) break;
	if (i == 100) {
		mrlog("No empty files slot, exiting");
		dump_files();
		mrexit("Out of file slots", EXIT_FAILURE);
	}
	mrlog("Storing file %p (%s) in slot %d", p, cl, i);
	dump_files();
	files[i].p = p;
	strlcpy(files[i].cl, cl, 20);
}

static void remove_file(FILE *p)
{
	int i;

	if (debug < 2) return;

	mrlog("remove_file(%p)", p);
	for (i = 0; i < 100; i++)
		if (files[i].p == p) break;
	if (i == 100) {
		mrlog("Can't find file, exiting");
		dump_files();
		mrlog("Continuing even though I can't find file %p", p);
	}
	mrlog("Removing file %p (%s) from slot %d", p, files[i].cl, i);
	dump_files();
	files[i].p = NULL;
}


FILE *big_fopen(char *p, char *file, char *mode)
{
	FILE *fp = fopen(file, mode);
	if (debug > 1) {
		mrlog("Opening '%s' in mode %s (%p) on behalf of %s",
			file, mode, fp, p);
	}
	if (fp == NULL) {
		mrlog("Can't open");
	} else {
		store_file(fp, p);
	}
	return fp;
}

int big_fclose(char *p, FILE *fp)
{
	int n = fclose(fp);
	if (debug > 1) mrlog("Closing %p on behalf of %s", fp, p);
	if (n) {
		mrlog("Can't close file");
	} else {
		remove_file(fp);
	}
	return n;
}

/* Remove the annoying carriage returns that Windows puts in text files */
void no_return(char *p)
{
	char *q = p;
	while (*p) {
		if (*p != '\r') *q++ = *p;
		p++;
	}
	*q = '\0';
}

int snprcat(char *str, size_t size, const char *fmt, ...)
{
	int n, r;
	va_list ap;

	va_start(ap, fmt);
	n = strlen(str);
	r = vsnprintf(str+n, size-n, fmt, ap);
	str[size-1] = '\0';
	return r;
}

struct gracetime {
        char *test;
        time_t grace;
        struct gracetime *next;
} *gracetime;

struct option {
	char *name;
	struct option *next;
} *options;

char *get_option(char *n, int partial)
{
	struct option *o;
	int l = strlen(n);

	for (o = options; o; o = o->next) {
		if ((partial && !strncmp(n, o->name, l))
			|| !strcmp(n, o->name)) return o->name;
	}
	return NULL;
}

static struct option *insert_option(char *n)
{
	struct option *o = big_malloc("insert_option", sizeof *o);
	o->name = big_strdup("insert_option", n);
	o->next = options;
	options = o;
	return o;
}

static void free_options(void)
{
	struct option *o;

	while ((o = options)) {
		options = o->next;
		big_free("free_optionlist", o->name);
		big_free("free_optionlist", o);
	}
}

static void insert_grace(char *test, time_t grace)
{
        struct gracetime *gt = big_malloc("insert_grace", sizeof *gt);
        gt->test = big_strdup("insert_grace", test);
        gt->grace = grace;
        gt->next = gracetime;
        gracetime = gt;
}

static void free_grace(void)
{
        struct gracetime *gt;

        while (gracetime) {
                gt = gracetime;
                gracetime = gracetime->next;
                big_free("free_grace", gt->test);
                big_free("free_grace", gt);
        }
}

static time_t lookup_grace(char *test)
{
        struct gracetime *gt;

        for (gt = gracetime; gt; gt = gt->next) {
                if (!strcmp(test, gt->test)) return gt->grace;
        }
        return 0;
}

static void readcfg(void)
{
	char b[256], key[256], value[256], *p;
	struct display *mp;
	int i;

	if (debug > 1) mrlog("readcfg()");

	/* Set all defaults */
	strlcpy(mrmachine, "localhost", sizeof mrmachine);
	mrport = 1984;
	while (mrdisplay) {
		mp = mrdisplay;
		mrdisplay = mp->next;
		big_free("readcfg: display", mp);
	}
	free_grace();
	free_options();
	mrsleep = 300;
	mrloop = INT_MAX;
	bootyellow = 60;
	bootred = 30;
	dfyellow = 90;
	dfred = 95;
	cpuyellow = 80;
	cpured = 90;
	memyellow = 100;
	memred = 100;
	msgage = 3600;
	report_size = 5000;
	memsize = MEMSIZE;
	pickupdir[0] = '\0';
	if (logfp) big_fclose("readcfg:logfile", logfp);
	logfp = NULL;

	for (i = 0; get_cfg("mrbig", b, sizeof b, i); i++) {
		if (b[0] == '#') continue;
		if (sscanf(b, "%s %[^\n]", key, value) == 2) {
			if (!strcmp(key, "machine")) {
				strlcpy(mrmachine, value, sizeof mrmachine);
			} else if (!strcmp(key, "port")) {
				mrport = atoi(value);
			} else if (!strcmp(key, "display")) {
				mp = big_malloc("readcfg: display", sizeof *mp);
				memset(&mp->in_addr, 0, sizeof mp->in_addr);
				mp->in_addr.sin_family = AF_INET;
				p = strchr(value, ':');
				if (p) {
					*p++ = '\0';
					mp->in_addr.sin_port = htons(atoi(p));
				} else {
					mp->in_addr.sin_port = htons(mrport);
				}
				mp->in_addr.sin_addr.s_addr = inet_addr(value);
				mp->next = mrdisplay;
				mrdisplay = mp;
			} else if (!strcmp(key, "sleep")) {
				mrsleep = atoi(value);
			} else if (!strcmp(key, "loop")) {
				mrloop = atoi(value);
			} else if (!strcmp(key, "bootyellow")) {
				bootyellow = atoi(value);
			} else if (!strcmp(key, "bootred")) {
				bootred = atoi(value);
			} else if (!strcmp(key, "debug")) {
				debug = atoi(value);
			} else if (!strcmp(key, "cpuyellow")) {
				cpuyellow = atoi(value);
			} else if (!strcmp(key, "cpured")) {
				cpured = atoi(value);
			} else if (!strcmp(key, "dfyellow")) {
				dfyellow = atof(value);
			} else if (!strcmp(key, "dfred")) {
				dfred = atof(value);
			} else if (!strcmp(key, "memyellow")) {
				memyellow = atof(value);
			} else if (!strcmp(key, "memred")) {
				memred = atof(value);
			} else if (!strcmp(key, "cfgdir")) {
				strlcpy(cfgdir, value, sizeof cfgdir);
			} else if (!strcmp(key, "msgage")) {
				msgage = atoi(value);
			} else if (!strcmp(key, "pickupdir")) {
				strlcpy(pickupdir, value, sizeof pickupdir);
			} else if (!strcmp(key, "logfile")) {
				logfp = big_fopen("readcfg:logfile", value, "a");
			} else if (!strcmp(key, "gracetime")) {
				char test[1000];
				int grace = 0;
				sscanf(value, "%s %d", test, &grace);
				insert_grace(test, grace);
			} else if (!strcmp(key, "report_size")) {
				report_size = atoi(value);
			} else if (!strcmp(key, "option")) {
				insert_option(value);
			} else if (!strcmp(key, "memsize")) {
				memsize = atoi(value);
			} else if (!strcmp(key, "set")) {
				char key[1000], value[1000];
				key[0] = value[0] = '\0';
				sscanf(value, "%s %s", key, value);
				mrlog("This doesn't actually do anything");
			}
		}
	}

	/* Replace . with , in fqdn (historical reasons) */
	for (p = mrmachine; *p; p++) {
		if (*p == '.') *p = ',';
	}

	/* Make sure the main loop executes at least every mrsleep seconds */
	if (mrloop > mrsleep) mrloop = mrsleep;
}

static WSADATA wsaData;
static int ws_started = 0;

int start_winsock(void)
{
	int n;

	if (debug > 1) mrlog("start_winsock()");
	if (!ws_started) {
		n = WSAStartup(MAKEWORD(2, 2), &wsaData);
		if (n != NO_ERROR) {
			mrlog("Error at WSAStartup() [%d]", WSAGetLastError());
		} else {
			if (debug) mrlog("Winsock started");
			ws_started = 1;
		}
	}
	return ws_started;
}

void stop_winsock(void)
{
	WSACleanup();
	ws_started = 0;
}

struct teststatus {
	char *machine, *test, *color;
	time_t last;
	time_t grace;
	struct teststatus *next;
};

static struct teststatus *teststatus = NULL;

/*
Insert status into list.

Return 0 if color is unchanged and sleep time is not exceeded.
Return 1 if color is non-green but grace time is not exceeded.
Return 2 otherwise.
*/
static int insert_status(char *machine, char *test, char *color)
{
	struct teststatus *s;
	time_t now = time(NULL);
	time_t grace = lookup_grace(test);
	if (debug > 1) mrlog("insert_status(%s, %s, %s)", machine, test, color);
	for (s = teststatus; s; s = s->next) {
		if (!strcmp(machine, s->machine) &&
		    !strcmp(test, s->test)) {
			if (debug) mrlog("insert_status found match");
			break;
		}
	}
	if (s == NULL) {
		/* We have never seen this test before.
		   Allocate a new structure with initial values
		   and tell mrsend to report the real status to the bbd.
		*/
		s = big_malloc("insert_status: node", sizeof *s);
		s->machine = big_strdup("insert_status: machine", machine);
		s->test = big_strdup("insert_status: test", test);
		s->next = teststatus;
		if (debug) mrlog("insert_status: new test");
		s->color = big_strdup("insert_status: color", color);
		s->last = now;
		s->grace = 0;
		teststatus = s;
		return 2;
	}

	if (!strcmp(color, s->color)) {
		/* If status is unchanged, check the time.
		*/
		if (now < s->last) {
			/* Someone adjusted the time or something.
			   Reset and tell mrsend to report the real status.
			*/
			mrlog("insert_status: Time has decreased!");
			s->last = now;
			s->grace = 0;
			return 2;
		}
		if (now-s->last >= mrsleep) {
			/* We must send a report or the display will
			   turn purple.
			*/
			if (debug) mrlog("insert_status: mrsleep exceeded");
			s->last = now;
			s->grace = 0;
			return 2;
		}
		/* No need to do anything.
		*/
		return 0;
	}

	/* We now know that the current status is different from the
	   one last reported to the bbd.
	*/

	if (!strcmp(color, "green")) {
		/* If the new status is green, insert the new status,
		   reset gracetime and report the real status.
		*/
		big_free("insert_status: color", s->color);
		s->color = big_strdup("insert_status: color", color);
		s->last = now;
		s->grace = 0;
		return 2;
	}

	if (strcmp(s->color, "green")) {
		/* If the old status was anything but green, insert
		   the new status, reset gracetime and report the
		   real status.
		*/
		big_free("insert_status: color", s->color);
		s->color = big_strdup("insert_status: color", color);
		s->last = now;
		s->grace = 0;
		return 2;
	}

	/* We now know that the old status was green and the new
	   status is non-green.
	*/

	if (s->grace == 0) {
		/* Start the clock ticking.
		*/
		s->grace = now+grace;
	}

	if (now >= s->grace) {
		/* Grace time expired, send a real status report.
		*/
		big_free("insert_status: color", s->color);
		s->color = big_strdup("insert_status: color", color);
		s->last = now;
		return 2;
	}

	/* Sit on our hands for a while. Tell mrsend to send a
	   green status to the bbd.
	*/
	s->last = now;
	return 1;
}

/*
Format is: "status machine,domain,com.test colour [message]"

We can optimise this by parsing out the test name and the colour
and only send something if the colour has changed for this test
*or* the bbsleep time is exceeded. Then we can run the main loop
as often as we want without putting any more load on the bbd.
*/
//void mrsend(char *p)
void mrsend(char *machine, char *test, char *color, char *message)
{
	struct display *mp;
	struct sockaddr_in my_addr;
	struct linger l_optval;
	unsigned long nonblock;

//	char machine[200], test[100], color[100];
//	char p[5000];
	char *p = NULL;
	int is;

	if (debug > 1) mrlog("mrsend(%s, %s, %s, %s)", machine, test, color, message);
#if 0
	n = sscanf(p, "status %199[^.].%99[^ ] %99s",
		   machine, test, color);
	if (n != 3) {
		mrlog("Bogus string in mrsend, process anyway");
		if (debug) mrlog("%s", p);
	}
#endif
	is = insert_status(machine, test, color);
	if (is == 0) {
		if (debug) mrlog("mrsend: no change, nothing to do");
		return;
	}
	if (!start_winsock()) return;

	/* Prepare the report */
	p = big_malloc("mrsend()", report_size+1);
	p[0] = '\0';
	if (is == 1) {
		snprcat(p, report_size, "status %s.%s green %s",
			machine, test, message);
	} else {
		snprcat(p, report_size, "status %s.%s %s %s",
			machine, test, color, message);
	}

	for (mp = mrdisplay; mp; mp = mp->next) {
		mp->s = -1;
	}
	for (mp = mrdisplay; mp; mp = mp->next) {
		mp->s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
		if (mp->s == -1) {
			mrlog("mrsend: socket failed: %d", WSAGetLastError());
			continue;
		}

		memset(&my_addr, 0, sizeof(my_addr));
		my_addr.sin_family = AF_INET;
		my_addr.sin_port = 0;
		my_addr.sin_addr.s_addr = inet_addr(bind_addr);
		if (bind(mp->s, (struct sockaddr *)&my_addr, sizeof my_addr) < 0) {
			mrlog("mrsend: bind(%s) failed: [%d]", bind_addr, WSAGetLastError());
			closesocket(mp->s);
			mp->s = -1;
			continue;
		}

		l_optval.l_onoff = 1;
		l_optval.l_linger = 5;
		nonblock = 1;
		if (ioctlsocket(mp->s, FIONBIO, &nonblock) == SOCKET_ERROR) {
			mrlog("mrsend: ioctlsocket failed: %d", WSAGetLastError());
			closesocket(mp->s);
			mp->s = -1;
			continue;
		}
		if (setsockopt(mp->s, SOL_SOCKET, SO_LINGER, (const char*)&l_optval, sizeof(l_optval)) == SOCKET_ERROR) {
			mrlog("mrsend: setsockopt failed: %d", WSAGetLastError());
			closesocket(mp->s);
			mp->s = -1;
			continue;
		}

		if (debug) mrlog("Using address %s, port %d\n",
				inet_ntoa(mp->in_addr.sin_addr),
				ntohs(mp->in_addr.sin_port));
		if (connect(mp->s, (struct sockaddr *)&mp->in_addr, sizeof(mp->in_addr)) == SOCKET_ERROR) {
			if (WSAGetLastError() != WSAEWOULDBLOCK) {
				mrlog("mrsend: connect: %d", WSAGetLastError());
				l_optval.l_onoff = 0;
				l_optval.l_linger = 0;
				setsockopt(mp->s, SOL_SOCKET, SO_LINGER, (const char*)&l_optval, sizeof(l_optval));
				closesocket(mp->s);
				mp->s = -1;
				continue;
			}
		}

		mp->pdata = p;
		mp->remaining = strlen(p);
	}

	time_t start_time = time(NULL);

	for(;;) {
		struct timeval timeo;
		fd_set wfds;
		int len;
		int tot_remaining;
		timeo.tv_sec = 1;
		timeo.tv_usec = 0;
		FD_ZERO(&wfds);
		tot_remaining = 0;
		for (mp = mrdisplay; mp; mp = mp->next) {
			if (mp->s != -1) {
				if (mp->remaining > 0) {
					FD_SET(mp->s, &wfds);
					tot_remaining += mp->remaining;
				}
			}
		}
		if (tot_remaining == 0) {
			/* all data sent to displays */
			goto cleanup;
		}
		if (time(NULL) > start_time + 10 || time(NULL) < start_time) {
			mrlog("mrsend: send loop timed out");
			/* this should not take more than 10 seconds. Network problem, so bail out */
			goto cleanup;
		}
		select(255 /* ignored on winsock */, NULL, &wfds, NULL, &timeo);
		for (mp = mrdisplay; mp; mp = mp->next) {
			if (mp->s != -1) {
				if (mp->remaining > 0) {
					len = send(mp->s, mp->pdata, mp->remaining, 0);
					if (len == SOCKET_ERROR) {
						continue;
					}
					mp->pdata += len;
					mp->remaining -= len;
					if (mp->remaining == 0) {
						shutdown(mp->s, SD_BOTH);
					}
				}
			}
		}
	}

	cleanup:

	/* initiate socket shutdowns */
	for (mp = mrdisplay; mp; mp = mp->next) {
		if (mp->s != -1) {
			shutdown(mp->s, SD_BOTH);
		}
	}
	/* gracefully terminate sockets, finally applying force */
	for (mp = mrdisplay; mp; mp = mp->next) {
		int i;
		if (mp->s != -1) {
			for (i = 0; i < 10; i++) {
				if (closesocket(mp->s) == WSAEWOULDBLOCK) {
					Sleep(1000); /* wait for all data to be sent */
				}
			}
			/* force the socket shut */
			l_optval.l_onoff = 0;
			l_optval.l_linger = 0;
			setsockopt(mp->s, SOL_SOCKET, SO_LINGER, (const char*)&l_optval, sizeof(l_optval));
			closesocket(mp->s);
			mp->s = -1;
		}
	}
	big_free("mrsend()", p);
}

#ifdef _WIN64
#define DWORD_REG uint64_t
#else
#define DWORD_REG uint32_t
#endif

static LONG CALLBACK
VectoredExceptionHandler(PEXCEPTION_POINTERS ExceptionInfo) {
	EXCEPTION_RECORD* e = ExceptionInfo->ExceptionRecord;
	CONTEXT* ctx = ExceptionInfo->ContextRecord;
	DWORD_REG frame;
	DWORD_REG prevframe;
	DWORD_REG ret;
	int i;
	FILE* f;
	time_t t;

	f = fopen("c:\\mrbig_crash.txt", "a");
	if (!f)
		return EXCEPTION_CONTINUE_SEARCH;

	t = time(NULL);
	fprintf(f, "MrBig detected an unhandled Exception at %s", ctime(&t));
	fprintf(f, "ExceptionCode: 0x%08X\n", (unsigned int)e->ExceptionCode);
	fprintf(f, "ExceptionFlags: 0x%08X\n", (unsigned int)e->ExceptionFlags);
	fprintf(f, "ExceptionAddress: 0x%p\n", e->ExceptionAddress);
	fprintf(f, "NumberParameters: 0x%08X\n", (unsigned int)e->NumberParameters);
	for (i = 0; i < e->NumberParameters; i++) {
		fprintf(f, "ExceptionInformation[%i]: 0x%08X\n", i, (unsigned int)e->ExceptionInformation[i]);
	}

	fprintf(f, "CALL STACK:\n");
	fprintf(f, "crash: 0x%p\n", e->ExceptionAddress);

#ifdef _WIN64
	frame = ctx->Rbp;
#else
	frame = ctx->Ebp;
#endif

	for (i = 0; i < 32; i++) {
		fflush(f);
		if (frame < 0x1000) {
			fprintf(f, "frame %p looks invalid. stop.\n", (void*)frame);
			break;
		}
		prevframe = *(DWORD_REG*)frame;
		ret = *(((DWORD_REG*)frame) + 1);
		fprintf(f, "frame %p (called from %p) (%p, %p, %p, %p)\n", (void*)frame, (void*)ret,
		      (void*)*(((DWORD_REG*)frame) + 2),
		      (void*)*(((DWORD_REG*)frame) + 3),
		      (void*)*(((DWORD_REG*)frame) + 4),
		      (void*)*(((DWORD_REG*)frame) + 5));
		frame = prevframe;
	}

	fprintf(f, "CONTEXT:\n");
	for (i = 0; i < sizeof(CONTEXT); i += 4) {
		fprintf(f, " %08X", *(uint32_t*)((uintptr_t)ctx + i));
		if ((i % 32) == 28)
			fprintf(f, "\n");
	}
	fprintf(f, "\n");
	fclose(f);

	return EXCEPTION_CONTINUE_SEARCH;
};

void mrbig(void)
{
	char *p;
	time_t t, lastrun;
	int sleeptime, i;
	char hostname[256];
	DWORD hostsize;

	/* install exception logging/stacktrace handler */
	AddVectoredExceptionHandler(1, VectoredExceptionHandler);

	if (debug) {
		mrlog("mrbig()");
	}
	for (i = 0; _environ[i]; i++) {
		startup_log("%s", _environ[i]);
	}
	for (;;) {
		if (debug) mrlog("main loop");
		read_cfg("mrbig", cfgfile);
		readcfg();
		t = time(NULL);
		strlcpy(now, ctime(&t), sizeof now);
		p = strchr(now, '\n');
		if (p) *p = '\0';
		hostsize = sizeof hostname;
		if (GetComputerName(hostname, &hostsize)) {
			for (i = 0; hostname[i]; i++)
				hostname[i] = tolower(hostname[i]);
			snprcat(now, sizeof now, " [%s]", hostname);
		}

		cpu();
		check_chunks("after cpu test");

		disk();
		check_chunks("after disk test");

		memory();
		check_chunks("after memory test");

		msgs();
		check_chunks("after msgs test");

		procs();
		check_chunks("after procs test");

		svcs();
		check_chunks("after svcs test");

		wmi();
		check_chunks("after wmi test");

		if (pickupdir[0]) ext_tests();

		lastrun = t;
		t = time(NULL);
		if (t < lastrun) {
			mrlog("mainloop: timewarp detected, sleep for %d",
				mrloop);
			sleeptime = mrloop;
		} else {
			sleeptime = mrloop-(t-lastrun);
		}
		if (sleeptime < SLEEP_MIN) sleeptime = SLEEP_MIN;
		if (debug) mrlog("started at %d, finished at %d, sleep for %d",
			(int)lastrun, (int)t, sleeptime);
		clear_cfg();
		if (debug) {
			dump_chunks();
			check_chunks("after main loop");
		}
		Sleep(sleeptime*1000);
	}
}

void usage(void)
{
	fprintf(stderr, "mrbig [-dmiut]\n");
	fprintf(stderr, "	-d	enable debugging (can appear several times on command line)\n");
	fprintf(stderr, "	-m	enable memory allocation debugging\n");
	fprintf(stderr, "	-i	install as service\n");
	fprintf(stderr, "	-u	uninstall service\n");
	fprintf(stderr, "	-t	run in standalone mode\n");
	exit(1);
}

int main(int argc, char **argv)
{
	int i;
	char *p;

	startup_log("main()");
	dirsep = '\\';
	GetModuleFileName(NULL, cfgdir, sizeof cfgdir);
	startup_log("cfgdir = '%s'", cfgdir);
	p = strrchr(cfgdir, dirsep);
	if (p) *p = '\0';
	cfgfile[0] = '\0';
	snprcat(cfgfile, sizeof cfgfile,
		"%s%c%s", cfgdir, dirsep, "mrbig.cfg");
	startup_log("cfgfile = '%s'", cfgfile);
	startup_log("SystemRoot = '%s'", getenv("SystemRoot"));

	for (i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "-c")) {
			i++;
			if (argv[i] == NULL) {
				fprintf(stderr, "No cfg file\n");
				return EXIT_FAILURE;
			}
		} else if (!strcmp(argv[i], "-d")) {
			debug++;
		} else if (!strcmp(argv[i], "-m")) {
			debug_memory = 1;
		} else if (!strncmp(argv[i], "-i", 2)) {
			if (argv[i][2] == '\0') {
				install_service("MrBig", "Mr Big Monitoring Agent");
			} else {
				install_service(argv[i]+2, argv[i]+2);
			}
			return 0;
		} else if (!strncmp(argv[i], "-u", 2)) {
			if (argv[i][2] == '\0') {
				delete_service("MrBig");
			} else {
				delete_service(argv[i]+2);
			}
			return 0;
		} else if (!strcmp(argv[i], "-t")) {
			standalone = 1;
		} else {
			fprintf(stderr, "Bogus option '%s'\n", argv[i]);
			usage();
		}
	}

	if (standalone) {
		mrbig();
		return 0;
	}

	startup_log("We want to become a service");
	service_main(argc, argv);

	dump_chunks();
	check_chunks("just before exit");
	dump_files();

	return 0;
}

