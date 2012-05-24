# Copyright 2000, 2001, 2003, 2006-2012  Nominum, Inc.  All Rights Reserved.

prefix = /usr/local
exec_prefix = ${prefix}
bindir = ${exec_prefix}/bin
mandir = ${datarootdir}/man
datarootdir = ${prefix}/share

INSTALL = /usr/bin/install -c
INSTALL_PROGRAM = ${INSTALL} 
INSTALL_DATA = ${INSTALL} -m 644 
RANLIB = ranlib

CC = gcc
CFLAGS = -g -O2 -I../bind-9.9.9/lib/isc/include -I/usr/local/include -D_REENTRANT -D_GNU_SOURCE -DPACKAGE_NAME=\"\" -DPACKAGE_TARNAME=\"\" -DPACKAGE_VERSION=\"\" -DPACKAGE_STRING=\"\" -DPACKAGE_BUGREPORT=\"\" -DHAVE_LIBNSL=1 -DHAVE_PTHREAD=1 -pthread
LIBS = libperf.a -lnsl  -L/usr/local/lib -lbind9 -ldns -lisccfg -lisc -ldl -lpthread  -lm
LIBOBJS = 
LDFLAGS =  -pthread

PERFOBJS = datafile.o dns.o log.o net.o opt.o os.o

all: dnsperf resperf

libperf.a: ${PERFOBJS}
	${AR} ${ARFLAGS} $@ ${PERFOBJS}
	${RANLIB} $@

dnsperf: dnsperf.o libperf.a $(LIBOBJS)
	$(CC) $(LDFLAGS) dnsperf.o $(LIBOBJS) $(LIBS) -o dnsperf

resperf: resperf.o libperf.a $(LIBOBJS)
	$(CC) $(LDFLAGS) resperf.o $(LIBOBJS) $(LIBS) -o resperf

.c.o:
	$(CC) $(CFLAGS) -c $<

installdirs:
	mkdir -p ${DESTDIR}${bindir}
	mkdir -p ${DESTDIR}${mandir}/man1

install: all installdirs
	${INSTALL_PROGRAM} dnsperf ${DESTDIR}${bindir}
	${INSTALL_PROGRAM} resperf ${DESTDIR}${bindir}
	${INSTALL_PROGRAM} resperf-report ${DESTDIR}${bindir}
	${INSTALL_DATA} dnsperf.1 ${DESTDIR}${mandir}/man1
	${INSTALL_DATA} resperf.1 ${DESTDIR}${mandir}/man1

clean:
	rm -f *.o dnsperf resperf libperf.a

distclean: clean
	rm -f config.log
	rm -f config.cache
	rm -f config.status
	rm -f Makefile
