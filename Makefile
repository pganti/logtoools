# Generated automatically from Makefile.in by configure.

prefix=/home/rjc/debian/logtools-0.13c/debian/logtools/usr
WFLAGS=-Wall -W -Wshadow -Wpointer-arith -Wcast-align -Wwrite-strings -Wcast-qual -Woverloaded-virtual -pedantic -ffor-scope
CFLAGS=-O2 -g -DNDEBUG ${WFLAGS}
CXX=c++ ${CFLAGS}
LFLAGS=-lstdc++ 

INSTALL=/usr/bin/install -c

EXES=clfmerge logprn funnel clfsplit clfdomainsplit
MAN1=clfmerge.1 logprn.1 funnel.1 clfsplit.1 clfdomainsplit.1

all: $(EXES)

%: %.cpp
	$(CXX) $< -o $@ ${LFLAGS}

install-bin: ${EXES}
	mkdir -p ${prefix}/bin
	${INSTALL} -s ${EXES} ${prefix}/bin

install: install-bin
	mkdir -p ${prefix}/share/man/man1 ${prefix}/share/man/man8
	${INSTALL} -m 644 ${MAN1} ${prefix}/share/man/man1
	mkdir -p /home/rjc/debian/logtools-0.13c/debian/logtools/etc
	${INSTALL} -m 644 clfdomainsplit.cfg /home/rjc/debian/logtools-0.13c/debian/logtools/etc

clean:
	rm -f $(EXES) build-stamp install-stamp
	rm -rf debian/tmp core debian/*.debhelper
	rm -f debian/{substvars,files} config.log

realclean: clean
	rm -f configure-stamp config.* Makefile sun/pkginfo logtools.h

