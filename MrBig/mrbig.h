
/* Designed for Win2k and above */
#define _WIN32_WINNT 0x0500

/* All required headers */
#include <windows.h>
#include <winsock2.h>
#include <psapi.h>
#include <limits.h>
#include <tlhelp32.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <stdint.h>
#include <unistd.h>
#include <ctype.h>

/* Never sleep for less than 30 seconds */
#define SLEEP_MIN (10)

/* from readperf.c */
struct perfcounter {
	char *instance;
	uint64_t *value;
};
extern struct perfcounter *read_perfcounters(DWORD object, DWORD *counters,
			long long *perf_time, long long *perf_freq);
extern void free_perfcounters(struct perfcounter *pc);
extern void print_perfcounters(struct perfcounter *pc, int ncounters);

/* from readlog.c */
struct event {
	time_t gtime, wtime;
	long long record, id;
	long type;
	char *source;
	char *message;
	struct event *next;
};
extern struct event *read_log(char *log, int maxage);
extern void free_log(struct event *e);
extern void print_log(struct event *e);

extern char mrmachine[256], bind_addr[256];
//extern char mrdisplay[256];
extern char cfgdir[256];
extern char now[1024];
extern char pickupdir[256];
//extern int mrport;
//extern int mrsleep;
extern int bootyellow, bootred;
extern double dfyellow, dfred;
extern int memyellow, memred;
extern int dirsep;
extern int msgage;
extern int cpuyellow, cpured;
extern int debug;
extern int start_winsock();
extern void stop_winsock();

extern int install_service(void);
extern int delete_service(void);
extern void check_chunks(char *);
extern void *big_malloc(char *, size_t);
extern void *big_realloc(char *, void *, size_t);
extern void big_free(char *, void *);
extern char *big_strdup(char *, char *);
extern FILE *big_fopen(char *, char *, char *);
extern int big_fclose(char *, FILE *);
extern int snprcat(char *str, size_t size, const char *fmt, ...);
extern void no_return(char *);
extern void mrsend(char *p);
extern void mrlog(char *fmt, ...);
extern void cpu(char *b, int n);
extern void disk(char *b, int n);
extern void msgs(char *b, int n);
extern void procs(char *b, int n);
extern void svcs(char *b, int n);
extern int service_main(int argc, char **argv);
extern void mrbig(void);
extern void SvcDebugOut(LPSTR, DWORD);
extern void ext_tests(void);
extern void clear_cfg(void);
extern void add_cfg(char *name, char *cfg);
extern int get_cfg(char *name, char *b, size_t n, int line);
extern void read_cfg(char *cat, char *filename);

/* snarfed from openbsd */
extern size_t strlcat(char *, const char *, size_t);
extern size_t strlcpy(char *, const char *, size_t);
