
#include "mrbig.h"

#define CFG_MAX 32000

struct config {
	char *name;
	char *cfg;
	struct config *next;
};

static struct config *conf = NULL;

void clear_cfg(void)
{
	struct config *c;

	if (debug > 1) mrlog("clear_cfg()");

	while (conf) {
		c = conf;
		conf = c->next;
		big_free("clear_cfg", c->name);
		big_free("clear_cfg", c->cfg);
		big_free("clear_cfg", c);
	}
}

void add_cfg(char *name, char *cfg)
{
	struct config *c;

	if (debug > 1) mrlog("add_cfg(%s, %s)", name, cfg);
	for (c = conf; c && strcmp(name, c->name); c = c->next);
	if (c == NULL) {
		c = big_malloc("add_cfg", sizeof *c);
		c->name = big_strdup("add_cfg", name);
		c->cfg = big_malloc("add_cfg", CFG_MAX);
		c->cfg[0] = '\0';
		c->next = conf;
		conf = c;
	}
	snprcat(c->cfg, CFG_MAX, "%s\n", cfg);
}

int get_cfg(char *name, char *b, size_t n, int line)
{
	struct config *c;
	char *p, *q;
	size_t m;
	int i;
	int retval;

	if (debug > 1) mrlog("get_cfg(%s, %p, %ld, %d)", name, b, n, line);

	for (c = conf; c && strcmp(name, c->name); c = c->next);
	if (c == NULL) {
		if (debug) mrlog("get_cfg can't find key %s", name);
		return 0;
	}

	p = c->cfg;
	i = 0;
	for (;;) {
		q = strchr(p, '\n');
		if (i == line || q == NULL) break;
		p = q+1;
		i++;
	}
	if (q == NULL) {
		retval = 0;
	} else {
		m = q-p;
		if (m >= n) {
			m = n-1;
			retval = 2;
		} else {
			retval = 1;
		}
		memcpy(b, p, m);
		b[m] = '\0';
	}
	if (debug > 1) mrlog("get_cfg returns %d (%s)", retval, b);
	return retval;
}

static void chomp(char *p)
{
	char *q;

	if ((q = strchr(p, '\n'))) *q = '\0';
	if ((q = strchr(p, '\r'))) *q = '\0';
}

static void recv_cfg(char *host, int port)
{
	struct sockaddr_in in_addr, my_addr;
	int n, s, failure;
	char b[32000];
	FILE *fp;

	if (debug > 1) mrlog("recv_cfg(%s, %d)", host, port);
	if (debug > 1) mrlog("Opening cfg.cache");

	failure = 0;
	fp = big_fopen("recv_cfg", "cfg.cache-", "w");
	if (fp == NULL) {
		mrlog("In recv_cfg: can't open cfg.cache- for writing");
		return;
	}
	if (debug > 1) mrlog("Starting winsock");
	if (!start_winsock()) {
		mrlog("In recv_cfg: can't start winsock");
		fprintf(fp, "In recv_cfg: can't start winsock\n");
		big_fclose("recv_cfg", fp);
		return;
	}
	s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (s == -1) {
		mrlog("No socket for you! [%d]", WSAGetLastError());
		fprintf(fp, "No socket for you! [%d]\n", WSAGetLastError());
		failure = 1;
		goto Exit;
	}
	memset(&my_addr, 0, sizeof my_addr);
	my_addr.sin_family = AF_INET;
	my_addr.sin_port = 0;
	my_addr.sin_addr.s_addr = inet_addr(bind_addr);
	if (bind(s, (struct sockaddr *)&my_addr, sizeof my_addr) < 0) {
		mrlog("In recv_cfg: can't bind local address %s [%d]",
			bind_addr, WSAGetLastError());
		fprintf(fp, "In recv_cfg: can't bind local address %s [%d]\n",
			bind_addr, WSAGetLastError());
		failure = 1;
		goto Exit;
	}
	memset(&in_addr, 0, sizeof in_addr);
	in_addr.sin_family = AF_INET;
	in_addr.sin_port = htons(port);
	in_addr.sin_addr.s_addr = inet_addr(host);
	if (debug > 1) mrlog("Connecting to minicfg");
	if (connect(s, (struct sockaddr *)&in_addr, sizeof in_addr) == -1) {
		mrlog("Can't connect to %s:%d [%d]", host, port, WSAGetLastError());
		fprintf(fp, "Can't connect to %s:%d [%d]\n", host, port, WSAGetLastError());
		failure = 1;
		goto Exit;
	}
	while ((n = recv(s, b, sizeof b, 0)) > 0) {
		if (debug > 1) mrlog("Writing %d bytes to cfg.cache-", n);
		fwrite(b, 1, n, fp);
	}
Exit:
	if (debug > 1) mrlog("Closing cfg.cache-");
	big_fclose("recv_cfg", fp);
	if (debug > 1) mrlog("Closing socket");
	if (closesocket(s) != 0) {
		mrlog("Error closing socket [%d]", WSAGetLastError());
	}
	if (debug > 1) mrlog("recv_cfg done");
	if (failure == 0) {
		/* on windows, renaming to a name that exists is an error */
		remove("cfg.cache");
		rename("cfg.cache-", "cfg.cache");
	}
}

void read_cfg(char *cat, char *filename)
{
	FILE *fp;
	char *q, host[1000], category[100], b[1000];
	int n, port;

	if (filename == NULL) return;

	if (debug > 1) mrlog("read_cfg(%s, %s)", cat, filename);
	fp = big_fopen("read_cfg", filename, "r");
	if (fp == NULL) return;

	strlcpy(category, cat, sizeof category);

	while (fgets(b, sizeof b, fp)) {
		chomp(b);
		if (b[0] == '[') {
			strlcpy(category, b+1, sizeof category);
			q = strchr(category, ']');
			if (q) *q = '\0';
		} else if (!strncmp(b, ".bind ", 6)) {
			sscanf(b, ".bind %s", bind_addr);
		} else if (!strncmp(b, ".config ", 8)) {
			n = sscanf(b, ".config %s %d", host, &port);
			if (n == 2 && port != 0) {
				recv_cfg(host, port);
				read_cfg(category, "cfg.cache");
			} else {
				mrlog("In read_cfg: bogus .config line");
			}
		} else if (!strncmp(b, ".include ", 9)) {
			read_cfg(category, b+9);
		} else if (b[0] && b[0] != '#') {
			add_cfg(category, b);
		}
	}
	big_fclose("read_cfg", fp);
}

