#include "mrbig.h"

static char cfgfile[256];
char mrmachine[256], bind_addr[256] = "0.0.0.0";
static struct display {
	struct sockaddr_in in_addr;
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
int debug = 0;
int dirsep;
int msgage;
int standalone = 0;

/* nosy memory management */
static int debug_memory = 0;

struct memchunk {
	void *p;
	size_t n;
	char cl[20];
} chunks[1000];

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

#define PATTERN_SIZE (sizeof big_pattern)

static int check_chunk(int i)
{
	unsigned char *q = chunks[i].p+chunks[i].n;
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
	for (i = 0; i < 1000; i++) {
		if (chunks[i].p) {
			corrupt |= check_chunk(i);
		}
	}
	if (corrupt) {
		mrlog("Memory corruption detected");
		exit(EXIT_FAILURE);
	} else {
		if (debug) mrlog("Memory checks out OK");
	}
}

static void dump_chunks(void)
{
	int i, n;

	mrlog("Chunks:");
	n = 0;
	for (i = 0; i < 1000; i++) {
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

	for (i = 0; i < 1000; i++)
		if (chunks[i].p == NULL) break;
	if (i == 1000) {
		mrlog("No empty chunk slot for %p (%s), exiting", p, cl);
		dump_chunks();
		exit(EXIT_FAILURE);
	}
	if (debug >= 3) mrlog("Storing chunk %p (%s) in slot %d", p, cl, i);
	chunks[i].p = p;
	chunks[i].n = n;
	strlcpy(chunks[i].cl, cl, 20);
	memcpy(p+n, big_pattern, PATTERN_SIZE);
}

static void remove_chunk(void *p, char *cl)
{
	int i;

	if (!debug_memory) return;

	for (i = 0; i < 1000; i++)
		if (chunks[i].p == p) break;
	if (i == 1000) {
		mrlog("Can't find chunk %p (%s), exiting", p, cl);
		dump_chunks();
		exit(EXIT_FAILURE);
	}
	if (check_chunk(i)) exit(EXIT_FAILURE);
	if (debug >= 3) {
		mrlog("Removing chunk %p (%s) from slot %d",
			p, chunks[i].cl, i);
	}
	chunks[i].p = NULL;
}

void *big_malloc(char *p, size_t n)
{
	void *a;

	if (debug_memory) a = malloc(n+PATTERN_SIZE);
	else a = malloc(n);

	if (debug > 2) {
		mrlog("Allocating %ld bytes (%p) on behalf of %s",
			(long)n, a, p);
	}
	if (a == NULL) {
		mrlog("Allocation '%s' failed, exiting", p);
		exit(EXIT_FAILURE);
	}
	store_chunk(a, n, p);
	return a;
}

void *big_realloc(char *p, void *q, size_t n)
{
	void *a;
	remove_chunk(q, p);
	if (debug_memory) a = realloc(q, n+PATTERN_SIZE);
	else a = realloc(q, n);

	if (debug > 2) {
		mrlog("Reallocating %ld bytes (%p => %p) on behalf of %s",	
			(long)n, q, a, p);
	}

	if (a == NULL) {
		mrlog("Allocation '%s' failed, exiting", p);
		exit(EXIT_FAILURE);
	}
	store_chunk(a, n, p);
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

	for (i = 0; i < 100; i++)
		if (files[i].p == NULL) break;
	if (i == 100) {
		mrlog("No empty files slot, exiting");
		dump_files();
		exit(EXIT_FAILURE);
	}
	mrlog("Storing file %p (%s) in slot %d", p, cl, i);
	files[i].p = p;
	strlcpy(files[i].cl, cl, 20);
}

static void remove_file(FILE *p)
{
	int i;

	if (debug < 2) return;

	for (i = 0; i < 100; i++)
		if (files[i].p == p) break;
	if (i == 100) {
		mrlog("Can't find file, exiting");
		dump_files();
		exit(EXIT_FAILURE);
	}
	mrlog("Removing file %p (%s) from slot %d", p, files[i].cl, i);
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
	int n;
	va_list ap;

	va_start(ap, fmt);
	n = strlen(str);
	return vsnprintf(str+n, size-n, fmt, ap);
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
	mrsleep = 300;
	mrloop = INT_MAX;
	bootyellow = 60;
	bootred = 30;
	dfyellow = 90;
	dfred = 95;
	cpuyellow = 80;
	cpured = 90;
	msgage = 3600;
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
			} else if (!strcmp(key, "cpuyellow")) {
				cpuyellow = atoi(value);
			} else if (!strcmp(key, "cpured")) {
				cpured = atoi(value);
			} else if (!strcmp(key, "dfyellow")) {
				dfyellow = atof(value);
			} else if (!strcmp(key, "dfred")) {
				dfred = atof(value);
			} else if (!strcmp(key, "cfgdir")) {
				strlcpy(cfgdir, value, sizeof cfgdir);
			} else if (!strcmp(key, "msgage")) {
				msgage = atoi(value);
			} else if (!strcmp(key, "pickupdir")) {
				strlcpy(pickupdir, value, sizeof pickupdir);
			} else if (!strcmp(key, "logfile")) {
				logfp = big_fopen("readcfg:logfile", value, "a");
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

	if (!ws_started) {
		n = WSAStartup(MAKEWORD(2, 2), &wsaData);
		if (n != NO_ERROR) {
			mrlog("Error at WSAStartup()");
		} else {
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
	struct teststatus *next;
};

static struct teststatus *teststatus = NULL;

/*
Insert status into list. Return non-zero if color has changed or
sleep time is exceeded.
*/
static int insert_status(char *machine, char *test, char *color)
{
	struct teststatus *s;
	time_t now = time(NULL);
	if (debug > 1) mrlog("insert_status(%s, %s, %s)", machine, test, color);
	for (s = teststatus; s; s = s->next) {
		if (!strcmp(machine, s->machine) &&
		    !strcmp(test, s->test)) {
			if (debug) mrlog("insert_status found match");
			break;
		}
	}
	if (s == NULL) {
		s = big_malloc("insert_status: node", sizeof *s);
		s->machine = big_strdup("insert_status: machine", machine);
		s->test = big_strdup("insert_status: test", test);
		s->color = NULL;
		s->next = teststatus;
		teststatus = s;
	}

	if (s->color == NULL) {
		if (debug) mrlog("insert_status: new test");
		s->color = big_strdup("insert_status: color", color);
		s->last = now;
		return 1;
	}
	if (strcmp(color, s->color)) {
		if (debug) mrlog("insert_status: new color");
		big_free("insert_status: color", s->color);
		s->color = big_strdup("insert_status: color", color);
		s->last = now;
		return 1;
	}
	if (now < s->last) {
		mrlog("insert_status: Time has decreased!");
		s->last = now;
		return 1;
	}
	if (now-s->last >= mrsleep) {
		if (debug) mrlog("insert_status: mrsleep exceeded");
		s->last = now;
		return 1;
	}

	return 0;
}

/*
Format is: "status machine,domain,com.test colour [message]"

We can optimise this by parsing out the test name and the colour
and only send something if the colour has changed for this test
*or* the bbsleep time is exceeded. Then we can run the main loop
as often as we want without putting any more load on the bbd.
*/
void mrsend(char *p)
{
	int m, n, s, x;
	struct display *mp;
	struct sockaddr_in my_addr;
	char machine[200], test[100], color[100];

	if (debug > 1) mrlog("mrsend(%s)", p);
	n = sscanf(p, "status %199[^.].%99[^ ] %99s",
		   machine, test, color);
	if (n != 3) {
		mrlog("Bogus string in mrsend, process anyway");
		if (debug) mrlog("%s", p);
	} else if (insert_status(machine, test, color) == 0) {
		if (debug) mrlog("mrsend: no change, nothing to do");
		return;
	}
	if (!start_winsock()) return;

	for (mp = mrdisplay; mp; mp = mp->next) {
		s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
		if (s == -1) {
			mrlog("No socket for you!");
			goto Exit;
		}
		memset(&my_addr, 0, sizeof my_addr);
		my_addr.sin_family = AF_INET;
		my_addr.sin_port = 0;
		my_addr.sin_addr.s_addr = inet_addr(bind_addr);
		if (bind(s, (struct sockaddr *)&my_addr, sizeof my_addr) < 0) {
			mrlog("In mrsend: can't bind local address %s", bind_addr);
			goto Exit;
		}

		if (connect(s, (struct sockaddr *)&mp->in_addr, sizeof mp->in_addr)
				== -1) {
			mrlog("Can't connect");
			goto Exit;
		}

		n = strlen(p)-1;	// send the final \0 as well
		m = 0;
		while ((m < n) && (x = send(s, p+m, n-m, 0)) > 0) {
			m += x;
		}

	Exit:
		shutdown(s, SD_SEND);
		closesocket(s);
	}
}

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

void mrbig(void)
{
	char b[5000], *p;
	time_t t, lastrun;
	int sleeptime, i;
	char hostname[256];
	DWORD hostsize;

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

		cpu(b, sizeof b);
check_chunks("after cpu test");
		mrsend(b);

		disk(b, sizeof b);
check_chunks("after disk test");
		mrsend(b);

		msgs(b, sizeof b);
check_chunks("after msgs test");
		mrsend(b);

		procs(b, sizeof b);
check_chunks("after procs test");
		mrsend(b);

		svcs(b, sizeof b);
check_chunks("after svcs test");
		mrsend(b);
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

int main(int argc, char **argv)
{
	int i;
	char *p;

fprintf(stderr, "main()\n");
	dirsep = '\\';
	GetModuleFileName(NULL, cfgdir, sizeof cfgdir);
fprintf(stderr, "cfgdir = '%s'\n", cfgdir);
	p = strrchr(cfgdir, dirsep);
	if (p) *p = '\0';
	snprintf(cfgfile, sizeof cfgfile,
		"%s%c%s", cfgdir, dirsep, "mrbig.cfg");
fprintf(stderr, "cfgfile = '%s'\n", cfgfile);
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
		} else if (!strcmp(argv[i], "-i")) {
			install_service();
			return 0;
		} else if (!strcmp(argv[i], "-u")) {
			delete_service();
			return 0;
		} else if (!strcmp(argv[i], "-t")) {
			standalone = 1;
		} else {
			fprintf(stderr, "Bogus option '%s'\n", argv[i]);
			return 0;
		}
	}

	if (standalone) {
		mrbig();
		return 0;
	}

	service_main(argc, argv);

	dump_chunks();
	check_chunks("just before exit");
	dump_files();

	return 0;
}

