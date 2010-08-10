
PACKAGE=MrBig
VERSION=0.11.0
#CFLAGS=-Wall -O -g -DDEBUG
CFLAGS=-Wall -O3 -g -DPACKAGE=\"$(PACKAGE)\" -DVERSION=\"$(VERSION)\"
DOCS=INSTALL EVENTS ChangeLog TODO EXT LARRD logs.cmd testfile.txt
SRCS=cfg.c cpu.c disk.c msgs.c procs.c svcs.c mrbig.c \
	service.c readperf.c readlog.c ext_test.c \
	strlcpy.c strlcat.c
HDRS=mrbig.h
OBJS=cfg.o cpu.o disk.o msgs.o procs.o svcs.o mrbig.o \
	service.o readperf.o readlog.o ext_test.o \
	strlcpy.o strlcat.o
NTOBJS=cfg.o cpu.o disk.o msgs.o procsnt.o svcs.o mrbig.o \
	service.o readperf.o readlog.o ext_test.o
CFG=mrbig.cfg
DISTCFG=mrbig.cfg.dist mrbig.cfg.minicfg
DISTDIR=$(PACKAGE)-$(VERSION)
EXTRA_DIST=minicfg.c minibbd.c
DISTFILES=$(CFG) $(DISTCFG) $(SRCS) $(HDRS) $(DOCS) Makefile $(EXTRA_DIST)
ZIPFILES=$(DISTCFG) $(DOCS) mrbig.exe

all: mrbig.exe

mrbig.exe: $(OBJS)
	$(CC) -o mrbig.exe $(OBJS) -lws2_32 -lpsapi

mrbignt.exe: $(NTOBJS)
	$(CC) -o mrbignt.exe $(NTOBJS) -lws2_32 -lpsapi

minibbd.exe: minibbd.c
	$(CC) $(CFLAGS) -o minibbd.exe minibbd.c -lws2_32

minicfg.exe: minicfg.c
	$(CC) $(CFLAGS) -o minicfg.exe minicfg.c -lws2_32

version.exe: version.c
	$(CC) $(CFLAGS) -o version.exe version.c -lws2_32

proclist.exe: proclist.c
	$(CC) $(CFLAGS) -o proclist.exe proclist.c -lws2_32

svcscfg.exe: svcscfg.c
	$(CC) $(CFLAGS) -o svcscfg.exe svcscfg.c -lws2_32

procs.exe: procs.c
	$(CC) -DTESTING $(CFLAGS) -o procs.exe procs.c -lws2_32

procs2.exe: procs2.c
	$(CC) $(CFLAGS) -o procs2.exe procs2.c -lws2_32 -lpsapi

enumproc.exe: enumproc.c readperf.c
	$(CC) $(CFLAGS) -o $@ enumproc.c readperf.c -lws2_32

pdhtest.exe: pdhtest.c
	$(CC) $(CFLAGS) -o $@ pdhtest.c -lpdh

$(OBJS): $(HDRS)

procsnt.o: procs.c
	$(CC) -DMrBigNT $(CFLAGS) -c -o procsnt.o procs.c

stop:
	net stop mrbig

start:
	net start mrbig

clean:
	rm -f *.o *.exe *~ *.stackdump

# This is not a very good idea
#mrbig.cfg.dist: mrbig.cfg
#	todos < mrbig.cfg > mrbig.cfg.dist
#	#cp mrbig.cfg mrbig.cfg.dist
#
#procs.cfg.dist: procs.cfg
#	todos < procs.cfg > procs.cfg.dist
#	#cp procs.cfg procs.cfg.dist
#
#services.cfg.dist: services.cfg
#	todos < services.cfg > services.cfg.dist
#	#cp services.cfg services.cfg.dist
#
#msgs.cfg.dist: msgs.cfg
#	todos < msgs.cfg > msgs.cfg.dist
#	#cp msgs.cfg msgs.cfg.dist
#
#ext.cfg.dist: ext.cfg
#	todos < ext.cfg > ext.cfg.dist
#	#cp ext.cfg ext.cfg.dist
#
#disk.cfg.dist: disk.cfg
#	todos < disk.cfg > disk.cfg.dist
#	#cp disk.cfg disk.cfg.dist

zip: $(ZIPFILES)
	strip mrbig.exe
	zip $(PACKAGE)-$(VERSION).zip $(ZIPFILES)

dist: $(DISTFILES)
	rm -rf $(DISTDIR)
	mkdir $(DISTDIR)
	cp $(DISTFILES) $(DISTDIR)
	tar cf $(DISTDIR).tar $(DISTDIR)
	gzip -f $(DISTDIR).tar
	rm -rf $(DISTDIR)
