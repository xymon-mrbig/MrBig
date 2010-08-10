#include "mrbig.h"

static char cfgfile[256];
char mrmachine[256];
static struct display {
	struct sockaddr_in in_addr;
	struct display *next;
} *mrdisplay;
char cfgdir[256];
char pickupdir[256];
char now[1024];
static FILE *logfp = NULL;
static int mrport, mrsleep;
int bootyellow, bootred;
int dfyellow, dfred;
int cpuyellow, cpured;
int debug = 0;
int dirsep;
int msgage;

#ifdef DEBUG
/* nosy memory management */
struct memchunk {
	void *p;
	char cl[20];
} chunks[1000];

static void dump_chunks(void)
{
	int i, n;
	char b[100];

	mrlog("Chunks:");
	n = 0;
	for (i = 0; i < 1000; i++) {
		if (chunks[i].p) {
			snprintf(b, sizeof b, "%d: '%s' (%p)", i,
				chunks[i].cl, chunks[i].p);
			mrlog(b);
			n++;
		}
	}
	snprintf(b, sizeof b, "Total %d chunks", n);
	mrlog(b);
}

static void store_chunk(void *p, char *cl)
{
	int i;
	char b[100];

	for (i = 0; i < 1000; i++)
		if (chunks[i].p == NULL) break;
	if (i == 1000) {
		mrlog("No empty chunk slot, exiting");
		dump_chunks();
		exit(EXIT_FAILURE);
	}
	snprintf(b, sizeof b, "Storing chunk %p (%s) in slot %d", p, cl, i);
	mrlog(b);
	chunks[i].p = p;
	strncpy(chunks[i].cl, cl, 20);
	chunks[i].cl[19] = '\0';
}

static void remove_chunk(void *p)
{
	int i;
	char b[100];

	for (i = 0; i < 1000; i++)
		if (chunks[i].p == p) break;
	if (i == 1000) {
		mrlog("Can't find chunk, exiting");
		dump_chunks();
		exit(EXIT_FAILURE);
	}
	snprintf(b, sizeof b,
		"Removing chunk %p (%s) from slot %d", p, chunks[i].cl, i);
	mrlog(b);
	chunks[i].p = NULL;
}

void *big_malloc(char *p, size_t n)
{
	void *a = malloc(n);
	char b[1024];

	snprintf(b, sizeof b, "Allocating %ld bytes (%p) on behalf of %s",
		(long)n, a, p);
	mrlog(b);

	if (a == NULL) {
		mrlog("Allocation failed, exiting");
		exit(EXIT_FAILURE);
	}
	store_chunk(a, p);
	return a;
}

void *big_realloc(char *p, void *q, size_t n)
{
	void *a = realloc(q, n);
	char b[1024];

	snprintf(b, sizeof b, "Reallocating %ld bytes (%p => %p) on behalf of %s",
		(long)n, q, a, p);
	mrlog(b);

	if (a == NULL) {
		mrlog("Allocation failed, exiting");
		exit(EXIT_FAILURE);
	}
	remove_chunk(q);
	store_chunk(a, p);
	return a;
}

void big_free(char *p, void *q)
{
	char b[1024];
	if (q == NULL) {
		snprintf(b, sizeof b, "No need to free %p on behalf of %s", q, p);
		mrlog(b);
		return;
	}
	snprintf(b, sizeof b, "Freeing %p on behalf of %s", q, p);
	mrlog(b);

	free(q);
	remove_chunk(q);
}

char *big_strdup(char *p, char *q)
{
	char *a;
	char b[1024];
	a = strdup(q);
	snprintf(b, sizeof b, "Copying (%p => %p) on behalf of %s", q, a, p);
	mrlog(b);
	if (a == NULL) {
		mrlog("Allocation failed, exiting");
		exit(EXIT_FAILURE);
	}
	store_chunk(a, p);
	return a;
}

struct filechunk {
	FILE *p;
	char cl[20];
} files[100];

static void dump_files(void)
{
	int i, n;
	char b[100];

	mrlog("Files:");
	n = 0;
	for (i = 0; i < 100; i++) {
		if (files[i].p) {
			snprintf(b, sizeof b, "%d: '%s' (%p)", i,
				files[i].cl, files[i].p);
			mrlog(b);
			n++;
		}
	}
	snprintf(b, sizeof b, "Total %d files", n);
	mrlog(b);
}

static void store_file(FILE *p, char *cl)
{
	int i;
	char b[100];

	for (i = 0; i < 100; i++)
		if (files[i].p == NULL) break;
	if (i == 100) {
		mrlog("No empty files slot, exiting");
		dump_files();
		exit(EXIT_FAILURE);
	}
	snprintf(b, sizeof b, "Storing file %p (%s) in slot %d", p, cl, i);
	mrlog(b);
	files[i].p = p;
	strncpy(files[i].cl, cl, 20);
	files[i].cl[19] = '\0';
}

static void remove_file(FILE *p)
{
	int i;
	char b[100];

	for (i = 0; i < 100; i++)
		if (files[i].p == p) break;
	if (i == 100) {
		mrlog("Can't find file, exiting");
		dump_files();
		exit(EXIT_FAILURE);
	}
	snprintf(b, sizeof b,
		"Removing file %p (%s) from slot %d", p, files[i].cl, i);
	mrlog(b);
	files[i].p = NULL;
}


FILE *big_fopen(char *p, char *file, char *mode)
{
	char b[1024];
	FILE *fp = fopen(file, mode);
	snprintf(b, sizeof b, "Opening '%s' in mode %s (%p) on behalf of %s",
		file, mode, fp, p);
	mrlog(b);
	if (fp == NULL) {
		mrlog("Can't open");
	} else {
		store_file(fp, p);
	}
	return fp;
}

int big_fclose(char *p, FILE *fp)
{
	char b[1024];
	int n = fclose(fp);
	snprintf(b, sizeof b, "Closing %p on behalf of %s", fp, p);
	mrlog(b);
	if (n) {
		mrlog("Can't close file");
	} else {
		remove_file(fp);
	}
	return n;
}
#else
static void dump_chunks(void)
{
	;
}

void *big_malloc(char *p, size_t n)
{
	return malloc(n);
}

void *big_realloc(char *p, void *q, size_t n)
{
	return realloc(q, n);
}

void big_free(char *p, void *q)
{
	free(q);
}

char *big_strdup(char *p, char *q)
{
	return strdup(q);
}

FILE *big_fopen(char *p, char *file, char *mode)
{
	return fopen(file, mode);
}

int big_fclose(char *p, FILE *fp)
{
	return fclose(fp);
}

void dump_files(void)
{
	;
}
#endif	/* DEBUG */

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

	/* Set all defaults */
	strncpy(mrmachine, "localhost", sizeof mrmachine);
	mrport = 1984;
	while (mrdisplay) {
		mp = mrdisplay;
		mrdisplay = mp->next;
		big_free("readcfg: display", mp);
	}
	mrsleep = 300;
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
#if 0
		if (sscanf(b, "%s %s", key, value) == 2) {
#else
		if (sscanf(b, "%s %[^\n]", key, value) == 2) {
#endif
			if (!strcmp(key, "machine")) {
				strncpy(mrmachine, value, sizeof mrmachine);
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
			} else if (!strcmp(key, "bootyellow")) {
				bootyellow = atoi(value);
			} else if (!strcmp(key, "bootred")) {
				bootred = atoi(value);
			} else if (!strcmp(key, "cpuyellow")) {
				cpuyellow = atoi(value);
			} else if (!strcmp(key, "cpured")) {
				cpured = atoi(value);
			} else if (!strcmp(key, "dfyellow")) {
				dfyellow = atoi(value);
			} else if (!strcmp(key, "dfred")) {
				dfred = atoi(value);
			} else if (!strcmp(key, "cfgdir")) {
				strncpy(cfgdir, value, sizeof cfgdir);
			} else if (!strcmp(key, "msgage")) {
				msgage = atoi(value);
			} else if (!strcmp(key, "pickupdir")) {
				strncpy(pickupdir, value, sizeof pickupdir);
			} else if (!strcmp(key, "logfile")) {
				logfp = big_fopen("readcfg:logfile", value, "a");
			}
		}
	}

	/* Replace . with , in fqdn (historical reasons) */
	for (p = mrmachine; *p; p++) {
		if (*p == '.') *p = ',';
	}
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

void mrsend(char *p)
{
	int m, n, s, x;
	struct display *mp;

	if (debug) mrlog("mrsend");
	if (!start_winsock()) return;

	for (mp = mrdisplay; mp; mp = mp->next) {
		s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
		if (s == -1) {
			mrlog("No socket for you!");
			goto Exit;
		}

		if (connect(s, (struct sockaddr *)&mp->in_addr, sizeof mp->in_addr)
				== -1) {
			mrlog("Can't connect");
			goto Exit;
		}

		n = strlen(p);
		m = 0;
		while ((m < n) && (x = send(s, p+m, n-m, 0)) > 0) {
			m += x;
		}

	Exit:
		closesocket(s);
	}
}

void mrlog(char *p)
{
	if (!logfp) return;
	fprintf(logfp, "%s\n", p);
	fflush(logfp);
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
		clear_cfg();
		read_cfg("mrbig", cfgfile);
		readcfg();
		t = time(NULL);
		strncpy(now, ctime(&t), sizeof now);
		p = strchr(now, '\n');
		if (p) *p = '\0';
		hostsize = sizeof hostname;
		if (GetComputerName(hostname, &hostsize)) {
			for (i = 0; hostname[i]; i++)
				hostname[i] = tolower(hostname[i]);
			snprcat(now, sizeof now, " [%s]", hostname);
		}

		cpu(b, sizeof b);
		mrsend(b);

		disk(b, sizeof b);
		mrsend(b);

		msgs(b, sizeof b);
		mrsend(b);

		procs(b, sizeof b);
		mrsend(b);

		svcs(b, sizeof b);
		mrsend(b);

		if (pickupdir[0]) ext_tests();

		lastrun = t;
		t = time(NULL);
		sleeptime = mrsleep-(t-lastrun);
		snprintf(b, sizeof b,
			"started at %d, finished at %d, sleep for %d",
		(int)lastrun, (int)t, sleeptime);
		mrlog(b);
		dump_chunks();
#if 0	/* posix */
		if (sleeptime > 0) sleep(sleeptime);
#else
		if (sleeptime > 0) Sleep(sleeptime*1000);
#endif
	}
}

int main(int argc, char **argv)
{
	int i;
	char *p;

	mrlog("main()");
	dirsep = '\\';
	GetModuleFileName(NULL, cfgdir, sizeof cfgdir);
	p = strrchr(cfgdir, dirsep);
	if (p) *p = '\0';
	snprintf(cfgfile, sizeof cfgfile,
		"%s%c%s", cfgdir, dirsep, "mrbig.cfg");
	for (i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "-c")) {
			i++;
			if (argv[i] == NULL) {
				fprintf(stderr, "No cfg file\n");
				return EXIT_FAILURE;
			}
		} else if (!strcmp(argv[i], "-d")) {
			debug++;
		} else if (!strcmp(argv[i], "-i")) {
			install_service();
			return 0;
		} else if (!strcmp(argv[i], "-u")) {
			delete_service();
			return 0;
		} else if (!strcmp(argv[i], "-t")) {
			mrbig();
			return 0;
		} else {
			fprintf(stderr, "Bogus option '%s'\n", argv[i]);
			return 0;
		}
	}

	service_main(argc, argv);

dump_chunks();
dump_files();

	return 0;
}

