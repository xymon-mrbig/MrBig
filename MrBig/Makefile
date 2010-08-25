
PACKAGE=MrBig
VERSION=0.20.0
#CFLAGS=-Wall -O -g -DDEBUG
CFLAGS=-Wall -O2 -fno-omit-frame-pointer -mno-omit-leaf-frame-pointer -g -ggdb -DPACKAGE=\"$(PACKAGE)\" -DVERSION=\"$(VERSION)\"
DOCS=INSTALL EVENTS ChangeLog DEVELOPMENT TODO EXT LARRD logs.cmd testfile.txt
SRCS=cfg.c cpu.c disk.c memory.c msgs.c procs.c svcs.c mrbig.c \
	service.c readperf.c readlog.c ext_test.c \
	strlcpy.c disphelper.c wmi.c
HDRS=mrbig.h disphelper.h
OBJS=cfg.o cpu.o disk.o memory.o msgs.o procs.o svcs.o mrbig.o \
	service.o readperf.o readlog.o ext_test.o \
	strlcpy.o disphelper.o wmi.o
NTOBJS=cfg.o cpu.o disk.o memory.o msgs.o procsnt.o svcs.o mrbig.o \
	service.o readperf.o readlog.o ext_test.o
CFG=mrbig.cfg
DISTCFG=mrbig.cfg.dist mrbig.cfg.minicfg
DISTDIR=$(PACKAGE)-$(VERSION)
EXTRA_DIST=minicfg.c minibbd.c
DISTFILES=$(CFG) $(DISTCFG) $(SRCS) $(HDRS) $(DOCS) Makefile $(EXTRA_DIST)
ZIPFILES=$(DISTCFG) $(DOCS)
EXEFILES=X86/mrbig.exe X64/mrbig64.exe
ARCHIVE=ulric@tiffany.365-24.se:/usr/local/apache/vhosts/extranet.365-24.se/MrBig/Archive
BETAVERSION=`date +%y%m%d%H%M`

all:
	$(MAKE) -C X86 mrbig.exe
	$(MAKE) -C X64 mrbig64.exe

mrwmi:
	$(MAKE) -C X86 mrwmi.exe
	$(MAKE) -C X64 mrwmi.exe

mrwmi.exe: $(OBJS) wmi.o disphelper.o
	$(CC) -o mrwmi.exe $(OBJS) wmi.o disphelper.o -lws2_32 -lpsapi -lole32 -loleaut32 -luuid

mrbig.exe: $(OBJS)
	$(CC) -o mrbig.exe $(OBJS) -lws2_32 -lpsapi -lole32 -loleaut32 -luuid

mrbig64.exe: $(OBJS)
	$(CC) -o mrbig64.exe $(OBJS) -lws2_32 -lpsapi -lole32 -loleaut32 -luuid

mrbignt.exe: $(NTOBJS)
	$(CC) -o mrbignt.exe $(NTOBJS) -lws2_32 -lpsapi

evilbbd.exe: evilbbd.c
	$(CC) $(CFLAGS) -o evilbbd.exe evilbbd.c -lws2_32

minibbd.exe: minibbd.c
	$(CC) $(CFLAGS) -o minibbd.exe minibbd.c -lws2_32

minicfg.exe: minicfg.c
	$(CC) $(CFLAGS) -o minicfg.exe minicfg.c -lws2_32

version.exe: version.c
	$(CC) $(CFLAGS) -o version.exe version.c -lws2_32

mrconnect.exe: mrconnect.c
	$(CC) $(CFLAGS) -DDEBUG -o mrconnect.exe mrconnect.c -lws2_32

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
	make -C X86 .clean
	make -C X64 .clean
	make .clean

.clean:
	rm -f *.o *.exe *~ *.stackdump

#zip: $(ZIPFILES)
#	strip mrbig.exe
#	zip $(PACKAGE)-$(VERSION).zip $(ZIPFILES)
#

zip: $(ZIPFILES) all
	zip $(PACKAGE)-$(VERSION).zip $(ZIPFILES) $(EXEFILES)

dist: $(DISTFILES)
	rm -rf $(DISTDIR)
	mkdir $(DISTDIR) $(DISTDIR)/X86 $(DISTDIR)/X64
	cp $(DISTFILES) $(DISTDIR)
	cp X86/Makefile $(DISTDIR)/X86
	cp X64/Makefile $(DISTDIR)/X64
	tar cf $(DISTDIR).tar $(DISTDIR)
	gzip -f $(DISTDIR).tar
	rm -rf $(DISTDIR)

upload: zip dist
	scp $(EXEFILES) $(PACKAGE)-$(VERSION).zip $(ARCHIVE)/Binaries
	scp $(DISTDIR).tar.gz $(ARCHIVE)/Sources

beta:
	rm -f X86/mrbig.exe
	$(MAKE) all
	scp X86/mrbig.exe $(ARCHIVE)/Beta/mrbig-$(BETAVERSION)-beta.exe

