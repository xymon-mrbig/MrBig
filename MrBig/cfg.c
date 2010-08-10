
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

	for (c = conf; c && strcmp(name, c->name); c = c->next);
	if (c == NULL) return 0;

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
	struct sockaddr_in in_addr;
	int n, s;
	char b[32000];
	FILE *fp;

	fp = fopen("cfg.cache", "w");
	if (fp == NULL) {
		mrlog("Can't open cfg.cache for writing");
		return;
	}
	if (!start_winsock()) {
		fclose(fp);
		return;
	}
	s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (s == -1) {
		mrlog("No socket for you!");
		goto Exit;
	}
	memset(&in_addr, 0, sizeof in_addr);
	in_addr.sin_family = AF_INET;
	in_addr.sin_port = htons(port);
	in_addr.sin_addr.s_addr = inet_addr(host);
	if (connect(s, (struct sockaddr *)&in_addr, sizeof in_addr) == -1) {
		mrlog("Can't connect");
		goto Exit;
	}
	while ((n = recv(s, b, sizeof b, 0)) > 0) {
		fwrite(b, 1, n, fp);
	}
Exit:
	fclose(fp);
	closesocket(s);
}

void read_cfg(char *cat, char *filename)
{
	FILE *fp;
	char *q, host[1000], category[100], b[1000];
	int n, port;

	if (filename == NULL) return;

	fp = fopen(filename, "r");
	if (fp == NULL) return;

	strncpy(category, cat, sizeof category);

	while (fgets(b, sizeof b, fp)) {
		chomp(b);
		if (b[0] == '[') {
			strncpy(category, b+1, sizeof category);
			q = strchr(category, ']');
			if (q) *q = '\0';
		} else if (!strncmp(b, ".config ", 8)) {
			n = sscanf(b, ".config %s %d", host, &port);
			if (n == 2 && port != 0) {
				recv_cfg(host, port);
				read_cfg(category, "cfg.cache");
			}
		} else if (!strncmp(b, ".include ", 9)) {
			read_cfg(category, b+9);
		} else if (b[0] && b[0] != '#') {
			add_cfg(category, b);
		}
	}
	fclose(fp);
}

