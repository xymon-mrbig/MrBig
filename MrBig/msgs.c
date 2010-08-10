#include "mrbig.h"

#define TEST_TYPE 1
#define TEST_SOURCE 2
#define TEST_MESSAGE 3
#define TEST_ID 4

#define RULES_MAX 1000

static struct rules {
	char *action;
	int test;
	char *value;
} rules[RULES_MAX];

static int nrules;

static void sanitize_message(char *p)
{
	while (*p) {
		if (*p == '<' || *p == '&') *p = '_';
		p++;
	}
}

static char *match_rules(struct event *e)
{
	int i;
	char id[100];

	for (i = 0; i < nrules; i++) {
		switch (rules[i].test) {
		case TEST_TYPE:
			if ((e->type == EVENTLOG_ERROR_TYPE &&
				!strcmp(rules[i].value, "error")) ||
			    (e->type == EVENTLOG_WARNING_TYPE &&
				!strcmp(rules[i].value, "warning")) ||
			    (e->type == EVENTLOG_SUCCESS &&
				!strcmp(rules[i].value, "success")) ||
			    (e->type == EVENTLOG_AUDIT_FAILURE &&
				!strcmp(rules[i].value, "audit_failure")) ||
			    (e->type == EVENTLOG_AUDIT_SUCCESS &&
				!strcmp(rules[i].value, "audit_success")))
				return rules[i].action;
			break;
		case TEST_SOURCE:
			if (!strcmp(e->source, rules[i].value))
				return rules[i].action;
			break;
		case TEST_MESSAGE:
			if (strstr(e->message, rules[i].value))
				return rules[i].action;
			break;
		case TEST_ID:
			sprintf(id, "%ld", (long)e->id);
			if (!strcmp(id, rules[i].value))
				return rules[i].action;
		}
	}
	return NULL;
}

static void read_msgcfg(/*char *p*/)
{
	char b[1000], action[100], test[100], value[1000];
	int i, n;

	nrules = 0;


	for (i = 0; get_cfg("msgs", b, sizeof b, i) && nrules < RULES_MAX; i++) {
		if (b[0] == '#') continue;
		n = sscanf(b, "%s %s %[^\r\n]", action, test, value);
		if (n < 3) continue;
		if (!strcmp(action, "green")) {
			rules[nrules].action = "green";
		} else if (!strcmp(action, "yellow")) {
			rules[nrules].action = "yellow";
		} else if (!strcmp(action, "red")) {
			rules[nrules].action = "red";
		} else if (!strcmp(action, "ignore")) {
			rules[nrules].action = NULL;
		} else {
			continue;
		}
		if (!strcmp(test, "type")) {
			rules[nrules].test = TEST_TYPE;
		} else if (!strcmp(test, "source")) {
			rules[nrules].test = TEST_SOURCE;
		} else if (!strcmp(test, "message")) {
			rules[nrules].test = TEST_MESSAGE;
		} else if (!strcmp(test, "id")) {
			rules[nrules].test = TEST_ID;
		} else {
			mrlog("In read_msgcfg: bogus test '%s'", test);
			continue;
		}
		rules[nrules++].value = big_strdup("msgs.c/read_msgcfg", value);
	}
}

static void free_cfg(void)
{
	int i;

	for (i = 0; i < nrules; i++) big_free("msgs.c/free_cfg", rules[i].value);
}

/* msgs

red Mon Jul 05 23:38:48 VS 2004 [ntserver]

App: E 'Mon Jul 05 23:33:53 2004': LicenseService - " Det finns inga fler licenser för produkten Windows
NT Server. Se Administrationsverktyg, Licenshanteraren för mer information om vilka användare som saknar
licens och hur många licenser som bör köpas.  "
*/

void msgs(char *b, int n)
{
	char cfgfile[1024];
	char *mycolor, *color, p[5000];
	struct event *app, *sys, *sec, *e;
	int m;

	if (debug > 1) mrlog("msgs(%p, %d)", b, n);
	snprintf(cfgfile, sizeof cfgfile, "%s%c%s", cfgdir, dirsep, "msgs.cfg");
	read_cfg("msgs", cfgfile);
	read_msgcfg(/*cfgfile*/);

	time_t t0 = time(NULL);


	color = "green";
	p[0] = '\0';
	m = 0;
	app = read_log("Application", t0-msgage);
	for (e = app; e != NULL && m < 4000; e = e->next) {
		mycolor = match_rules(e);
		if (mycolor) {
			sanitize_message(e->message);
			snprcat(p, sizeof p, "&%s Application - %s - %s%s\n",
				mycolor, e->source,
				ctime(&e->gtime), e->message);
			if (!strcmp(color, "green") || !strcmp(mycolor, "red"))
				color = mycolor;
		}
	}
	free_log(app);
	sys = read_log("System", t0-msgage);
	for (e = sys; e != NULL && m < 4000; e = e->next) {
		mycolor = match_rules(e);
		if (mycolor) {
			sanitize_message(e->message);
			snprcat(p, sizeof p, "&%s Application - %s - %s%s\n",
				mycolor, e->source,
				ctime(&e->gtime), e->message);
			if (!strcmp(color, "green") || !strcmp(mycolor, "red"))
				color = mycolor;
		}
	}
	free_log(sys);
	sec = read_log("Security", t0-msgage);
	for (e = sec; e != NULL && m < 4000; e = e->next) {
		mycolor = match_rules(e);
		if (mycolor) {
			sanitize_message(e->message);
			snprcat(p, sizeof p, "&%s Application - %s - %s%s\n",
				mycolor, e->source,
				ctime(&e->gtime), e->message);
			if (!strcmp(color, "green") || !strcmp(mycolor, "red"))
				color = mycolor;
		}
	}
	free_log(sec);
	snprintf(b, n, "status %s.msgs %s %s\n\n%s\n",
		mrmachine, color, now, p);

	free_cfg();
}
