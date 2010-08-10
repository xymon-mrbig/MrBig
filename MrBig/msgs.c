#include "mrbig.h"

#define TEST_TYPE 1
#define TEST_SOURCE 2
#define TEST_MESSAGE 3
#define TEST_ID 4
#define TEST_LOG 5

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

static char *match_rules(struct event *e, char *log)
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
			    (e->type == EVENTLOG_INFORMATION_TYPE &&
				!strcmp(rules[i].value, "information")) ||
			    (e->type == EVENTLOG_AUDIT_FAILURE &&
				!strcmp(rules[i].value, "audit_failure")) ||
			    (e->type == EVENTLOG_AUDIT_SUCCESS &&
				!strcmp(rules[i].value, "audit_success"))) {
				if (debug > 1) {
					mrlog("returning %s because type is %s",
						rules[i].action,
						rules[i].value);
				}
				return rules[i].action;
			}
			break;
		case TEST_SOURCE:
			if (!strcmp(e->source, rules[i].value)) {
				if (debug > 1) {
					mrlog("returning %s because source is %s",
						rules[i].action,
						rules[i].value);
				}
				return rules[i].action;
			}
			break;
		case TEST_MESSAGE:
			if (strstr(e->message, rules[i].value)) {
				if (debug > 1) {
					mrlog("returning %s because message matches %s",
						rules[i].action,
						rules[i].value);
				}
				return rules[i].action;
			}
			break;
		case TEST_ID:
			sprintf(id, "%ld", (long)e->id);
			if (!strcmp(id, rules[i].value)) {
				if (debug > 1) {
					mrlog("returning %s because id is %s",
						rules[i].action,
						rules[i].value);
				}
				return rules[i].action;
			}
			break;
		case TEST_LOG:
			if (!strcmp(log, rules[i].value)) {
				if (debug > 1) {
					mrlog("returning %s because log is %s",
						rules[i].action,
						rules[i].value);
				}
				return rules[i].action;
			}
			break;
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
		} else if (!strcmp(test, "log")) {
			rules[nrules].test = TEST_LOG;
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

App: E 'Mon Jul 05 23:33:53 2004': LicenseService - " Det finns inga fler licenser f�r produkten Windows
NT Server. Se Administrationsverktyg, Licenshanteraren f�r mer information om vilka anv�ndare som saknar
licens och hur m�nga licenser som b�r k�pas.  "
*/

#define MAX_KEY_LENGTH 255
#define MAX_VALUE_NAME 16383

void msgs(void)
{
	char b[5000];
	int n = sizeof b;
	char cfgfile[1024];
	char *mycolor, *color, p[5000];
	struct event /* *app, *sys, *sec, */ *e;
//struct event *ulric_fibbar;
	struct event *events;
	int m;

	HKEY hTestKey;
    	TCHAR    achKey[MAX_KEY_LENGTH+1];   // buffer for subkey name
    	DWORD    cbName;                   // size of name string
    	TCHAR    achClass[MAX_PATH] = TEXT("");  // buffer for class name
    	DWORD    cchClassName = MAX_PATH;  // size of class string
    	DWORD    cSubKeys=0;               // number of subkeys
    	DWORD    cbMaxSubKey;              // longest subkey size
    	DWORD    cchMaxClass;              // longest class string
    	DWORD    cValues;              // number of values for key
    	DWORD    cchMaxValue;          // longest value name
    	DWORD    cbMaxValueData;       // longest value data
    	DWORD    cbSecurityDescriptor; // size of security descriptor
    	FILETIME ftLastWriteTime;      // last write time

    	DWORD i, retCode;

//    	TCHAR  achValue[MAX_VALUE_NAME+1];
//    	DWORD cchValue = MAX_VALUE_NAME;


	if (debug > 1) mrlog("msgs(%p, %d)", b, n);
	cfgfile[0] = '\0';
	snprcat(cfgfile, sizeof cfgfile, "%s%c%s", cfgdir, dirsep, "msgs.cfg");
	read_cfg("msgs", cfgfile);
	read_msgcfg(/*cfgfile*/);

	time_t t0 = time(NULL);


	color = "green";
	p[0] = '\0';
	m = 0;
#if 0
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
			snprcat(p, sizeof p, "&%s System - %s - %s%s\n",
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
			snprcat(p, sizeof p, "&%s Security - %s - %s%s\n",
				mycolor, e->source,
				ctime(&e->gtime), e->message);
			if (!strcmp(color, "green") || !strcmp(mycolor, "red"))
				color = mycolor;
		}
	}
	free_log(sec);
	ulric_fibbar = read_log("File Replication Service", t0-msgage);
	for (e = ulric_fibbar; e != NULL && m < 4000; e = e->next) {
		mycolor = match_rules(e);
		if (mycolor) {
			sanitize_message(e->message);
			snprcat(p, sizeof p, "&%s File Replication Service - %s - %s%s\n",
				mycolor, e->source,
				ctime(&e->gtime), e->message);
			if (!strcmp(color, "green") || !strcmp(mycolor, "red"))
				color = mycolor;
		}
	}
	free_log(ulric_fibbar);
#else
	if (RegOpenKeyEx(HKEY_LOCAL_MACHINE,
			TEXT("System\\CurrentControlSet\\Services\\EventLog"),
			0,
			KEY_READ,
			&hTestKey) == ERROR_SUCCESS) {
		retCode = RegQueryInfoKey(
		        hTestKey,                    // key handle
		        achClass,                // buffer for class name
		        &cchClassName,           // size of class string
		        NULL,                    // reserved
		        &cSubKeys,               // number of subkeys
		        &cbMaxSubKey,            // longest subkey size
		        &cchMaxClass,            // longest class string
		        &cValues,                // number of values for this key
		        &cchMaxValue,            // longest value name
		        &cbMaxValueData,         // longest value data
		        &cbSecurityDescriptor,   // security descriptor
		        &ftLastWriteTime);       // last write time

		for (i = 0; i < cSubKeys; i++) {
			cbName = MAX_KEY_LENGTH;
			retCode = RegEnumKeyEx(hTestKey, i,
					achKey, &cbName, NULL,
					NULL, NULL, &ftLastWriteTime);
			if (retCode == ERROR_SUCCESS) {
				if (debug) mrlog("Reading log %s", achKey);
				events = read_log(achKey, t0-msgage);
				for (e = events; e && m < 4000; e = e->next) {
					mycolor = match_rules(e, achKey);
					if (mycolor) {
						sanitize_message(e->message);
						snprcat(p, sizeof p, "&%s %s - %s - %s%s\n",
							mycolor, achKey, e->source,
							ctime(&e->gtime), e->message);
						if (!strcmp(color, "green") || !strcmp(mycolor, "red"))
							color = mycolor;
					}
				}
				free_log(events);
			}
		}
	}
#endif
//	memset(b, 0, sizeof b);
	b[0] = '\0';
	snprcat(b, n-1, "%s\n\n%s\n", now, p);

	free_cfg();
	mrsend(mrmachine, "msgs", color, b);
}

