
#include "mrbig.h"
#include "disphelper.h"
#include <wchar.h>

#define HR_TRY(func) if (FAILED(func)) { printf("\n## Fatal error on line %d.\n", __LINE__); goto cleanup; }

#define FIELDS_MAX 100

static struct fields {
	char field[100];
	char op1[100];
	char op2[100];
} fields[FIELDS_MAX];

static int nfields;

/* **************************************************************************
 * getWmiStr:
 *   Helper function to create wmi moniker incorporating computer name.
 *
 ============================================================================ */
static LPWSTR getWmiStr(LPCWSTR szComputer)
{
	static WCHAR szWmiStr[256];

	wcscpy(szWmiStr, L"winmgmts:{impersonationLevel=impersonate}!\\\\");

	if (szComputer) wcsncat(szWmiStr, szComputer, 128);
	else            wcscat (szWmiStr, L".");

	wcscat(szWmiStr, L"\\root\\cimv2");

	return szWmiStr;
}

/*
Configuration for this module appears under the [wmi] header.
Example with explanations:

[wmi]
begin wmi1 -- a test page with the name "wmi1" will be produced
text This is a wmi test page -- this page will appear on the report
query select * from Win32_foo -- any wql query
field xxx -- this field will appear on the report, value is ignored
field yyy > 0 -- this field will also appear, value must be > 0
go -- run the query
text Another query on the same page
query select * from Win32_bar
field aaa = "Ok" -- this field must have the string value Ok
field bbb
go
end -- page is finished, send the report
begin wmi2 -- another test page
always green -- the test will have green status regardless of individual fields
text Another wmi test page
query select * from Win32_baz
field qwe
field asd = 0 -- status will be indicated by red or green bullet
go
end
*/
void wmi(void)
{
	LPCWSTR szComputer = L".";	// local computer
	DISPATCH_OBJ(wmiSvc);
	DISPATCH_OBJ(colWmiTest);
	LPWSTR szWmiItem;
	wchar_t query[1000];
	int i, n, f;
	char b[1000], key[1000], value[1000];
	char *green = "green", *red = "red", *blue = "blue", *clear = "clear";
	char always[100], *color = green, *bullet;
	char page[100];
	char report[10000];

	query[0] = L'\0';
	report[0] = '\0';
	always[0] = '\0';
	page[0] = '\0';
	nfields = 0;

	if (debug) mrlog("wmi()");
	dhInitialize(TRUE);
	for (i = 0; get_cfg("wmi", b, sizeof b, i); i++) {
		if (debug) mrlog("wmi: %s", b);
		if (b[0] == '#') continue;
		key[0] = value[0] = '\0';
		n = sscanf(b, "%s %[^\n]", key, value);
		if (n < 1) continue;
		if (!strcmp(key, "always")) {
			strcpy(always, value);
		} else if (!strcmp(key, "begin")) {
			strcpy(page, value);
			always[0] = '\0';
			report[0] = '\0';
			color = green;
		} else if (!strcmp(key, "end")) {
			if (!page[0]) {
				mrlog("wmi end without begin");
				continue;
			}
			if (always[0]) color = always;
			mrsend(mrmachine, page, color, report);
		} else if (!strcmp(key, "field")) {
			fields[nfields].field[0] = '\0';
			fields[nfields].op1[0] = '\0';
			fields[nfields].op2[0] = '\0';
			n = sscanf(value, "%s %s %[^\n]",
				fields[nfields].field,
				fields[nfields].op1,
				fields[nfields].op2);
			if (n < 1) continue;
			nfields++;
		} else if (!strcmp(key, "go")) {
			if (!query[0]) {
				mrlog("no wmi query");
				continue;
			}
			HR_TRY(dhGetObject(getWmiStr(szComputer), NULL, &wmiSvc));
			HR_TRY(dhGetValue(L"%o", &colWmiTest, wmiSvc, L".ExecQuery(%S)", query));
			FOR_EACH(objWmiTest, colWmiTest, NULL) {
				wchar_t item[1000];
				for (f = 0; f < nfields; f++) {
					char *op1 = fields[f].op1;
					char *op2 = fields[f].op2;
					_snwprintf(item, sizeof item, L".%hs", fields[f].field);
					ZeroMemory(&szWmiItem, sizeof szWmiItem);
					dhGetValue(L"%S", &szWmiItem, objWmiTest, item);
					bullet = "red";
					if (szWmiItem) {
						char item[1000];
						snprintf(item, sizeof item, "%ls", szWmiItem);
						if (op1[0] && op2[0]) {
							if (isdigit(op2[0])) {
								int a1 = atoi(item), a2 = atoi(op2);
								if ((!strcmp(op1, "=") && (a1 == a2)) ||
								    (!strcmp(op1, "<") && (a1 < a2)) ||
								    (!strcmp(op1, "<=") && (a1 <= a2)) ||
								    (!strcmp(op1, ">") && (a1 > a2)) ||
								    (!strcmp(op1, ">=") && (a1 >= a2))) {
									bullet = green;
								}
							} else {
								int a = strcmp(item, op2);
								if ((!strcmp(op1, "=") && (a == 0)) ||
								    (!strcmp(op1, "<") && (a < 0)) ||
								    (!strcmp(op1, "<=") && (a <= 0)) ||
								    (!strcmp(op1, ">") && (a > 0)) ||
								    (!strcmp(op1, ">=") && (a >= 0)) ||
								    (!strcmp(op1, "contains") && strstr(item, op2))) {
									bullet = green;
								}
							}
						} else {
							bullet = blue;
						}
					} else {
						if (op1[0]) {
							bullet = red;
						} else {
							bullet = clear;
						}
					}
					if (bullet == red) color = bullet;

#if 1	/* this messes up the ncv rrd stuff */
					snprcat(report, sizeof report, "&%s %s: %ls\n",
						bullet, fields[f].field,
						szWmiItem?szWmiItem:L"");
#else
					snprcat(report, sizeof report, "%s: %ls &%s\n",
						fields[f].field, szWmiItem?szWmiItem:L"", bullet);
#endif
					dhFreeString(szWmiItem);
				}
				snprcat(report, sizeof report, "\n");
			} NEXT(objWmiTest);

cleanup:
			SAFE_RELEASE(colWmiTest);
			SAFE_RELEASE(wmiSvc);
		} else if (!strcmp(key, "query")) {
			_snwprintf(query, sizeof query, L"%hs", value);
			nfields = 0;
		} else if (!strcmp(key, "text")) {
			snprcat(report, sizeof report, "%s\n", value);
		} else {
			mrlog("Unknown key '%s'", key);
		}
	}
	if (debug) mrlog("wmi done");
	dhUninitialize(TRUE);
	return;
}
