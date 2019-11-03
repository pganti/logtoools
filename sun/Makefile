
all: pkg

INSTROOT=`pwd`/tmp
PKGNAME=logtools

pkg:
	( cd .. ; ./configure --prefix=/usr --mandir=`pwd`/sun/tmp/usr/share/man )
	make -C .. WFLAGS="-Wall -W -Wshadow -Wpointer-arith -Wwrite-strings"
	make -C .. prefix=${INSTROOT}/usr install
	pkgmk -o -r ${INSTROOT}
	pkgtrans -s spool ${PKGNAME}.pkg ${PKGNAME}
	echo "/var/spool/pkg/${PKGNAME}.pkg complete!"
