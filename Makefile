.PHONY = all pkgclip pkgclip-dbus doc help install uninstall clean

WARNINGS := -Wall -Wextra -pedantic -Wshadow -Wpointer-arith -Wcast-align \
			-Wwrite-strings -Wmissing-prototypes -Wmissing-declarations \
			-Wredundant-decls -Wnested-externs -Winline -Wno-long-long \
			-Wuninitialized -Wconversion -Wstrict-prototypes
CFLAGS := -g -std=c99 $(WARNINGS)

PROGRAMS = pkgclip pkgclip-dbus
DOCS = pkgclip.1.gz
HELP = index.html

SRCFILES = main.c util.c
HDRFILES = pkgclip.h util.h xpm.h
OBJFILES = main.o util.o

DBUSSRCFILES = pkgclip-dbus.c
#DBUSHDRFILES = 
DBUSOBJFILES = pkgclip-dbus.o

MANFILES = pkgclip.1

all: $(PROGRAMS) $(DOCS) $(HELP) pkgclip.xpm

pkgclip: $(OBJFILES)
	$(CC) -o pkgclip $(OBJFILES) `pkg-config --cflags --libs gtk+-3.0` -lalpm

pkgclip-dbus: $(DBUSOBJFILES)
	$(CC) -o pkgclip-dbus $(DBUSOBJFILES) `pkg-config --cflags --libs polkit-gobject-1 gio-2.0`

main.o: main.c pkgclip.h xpm.h util.h
	$(CC) -c $(CFLAGS) `pkg-config --cflags --libs gtk+-3.0` -lalpm main.c

util.o: util.c pkgclip.h util.h
	$(CC) -c $(CFLAGS) `pkg-config --cflags --libs gtk+-3.0` -lalpm util.c

pkgclip-dbus.o: pkgclip-dbus.c
	$(CC) -c $(CFLAGS) `pkg-config --cflags --libs polkit-gobject-1 gio-2.0` pkgclip-dbus.c

doc: $(DOCS)

pkgclip.1.gz: $(MANFILES)
	gzip -c pkgclip.1 > pkgclip.1.gz

help: $(HELP)

index.html:
	groff -T html -man pkgclip.1 > index.html

pkgclip.xpm: xpm.h
	echo "/* XPM */" >pkgclip.xpm
	cat xpm.h >>pkgclip.xpm

install:
	install -D -m755 pkgclip $(DESTDIR)usr/bin/pkgclip
	install -D -m755 pkgclip-dbus $(DESTDIR)usr/bin/pkgclip-dbus
	install -D -m644 pkgclip.1.gz $(DESTDIR)usr/share/man/man1/pkgclip.1.gz
	install -D -m644 index.html $(DESTDIR)usr/share/doc/pkgclip/html/index.html
	install -D -m644 pkgclip.xpm $(DESTDIR)usr/share/pixmaps/pkgclip.xpm
	install -D -m644 org.jjk.pkgclip.policy $(DESTDIR)usr/share/polkit-1/actions/org.jjk.pkgclip.policy
	install -D -m644 org.jjk.PkgClip.service $(DESTDIR)usr/share/dbus-1/system-services/org.jjk.PkgClip.service
	install -D -m644 org.jjk.PkgClip.conf $(DESTDIR)etc/dbus-1/system.d/org.jjk.PkgClip.conf
	install -D -m644 pkgclip.desktop $(DESTDIR)usr/share/applications/pkgclip.desktop

uninstall:
	rm -f $(DESTDIR)usr/bin/pkgclip
	rm -f $(DESTDIR)usr/bin/pkgclip-dbus
	rm -f $(DESTDIR)usr/share/man/man1/pkgclip.1.gz
	rm -rf $(DESTDIR)usr/share/doc/pkgclip
	rm -f $(DESTDIR)usr/share/pixmaps/pkgclip.xpm
	rm -f $(DESTDIR)usr/share/polkit-1/actions/org.jjk.pkgclip.policy
	rm -f $(DESTDIR)usr/share/dbus-1/system-services/org.jjk.PkgClip.service
	rm -f $(DESTDIR)etc/dbus-1/system.d/org.jjk.PkgClip.conf
	rm -f $(DESTDIR)usr/share/applications/pkgclip.desktop

clean:
	rm -f $(PROGRAMS)
	rm -f $(OBJFILES)
	rm -f $(DBUSOBJFILES)
	rm -f $(DOCS)
	rm -f $(HELP)
