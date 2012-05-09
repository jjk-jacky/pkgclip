
CLEANFILES = index.html org.jjk.PkgClip.service

bin_PROGRAMS = pkgclip pkgclip-dbus

nodist_man_MANS = pkgclip.1
dist_doc_DATA = AUTHORS COPYING HISTORY README.md
doc_DATA = index.html

desktopdir = /usr/share/applications
dist_desktop_DATA = pkgclip.desktop

policydir = /usr/share/polkit-1/actions
dist_policy_DATA = org.jjk.pkgclip.policy
dbusservicedir = /usr/share/dbus-1/system-services
nodist_dbusservice_DATA = org.jjk.PkgClip.service
dbusconfdir = /etc/dbus-1/system.d
dist_dbusconf_DATA = org.jjk.PkgClip.conf

install-data-hook:
	mkdir $(DESTDIR)$(docdir)/html
	mv $(DESTDIR)$(docdir)/index.html $(DESTDIR)$(docdir)/html/

dist-hook:
	cp $(srcdir)/pkgclip.pod $(disdir)/
	cp $(srcdir)/org.jjk.PkgClip.service.tpl $(distdir)/

AM_CFLAGS = -g \
                -Wall -Wextra -pedantic -Wshadow -Wpointer-arith -Wcast-align \
                -Wwrite-strings -Wmissing-prototypes -Wmissing-declarations \
                -Wredundant-decls -Wnested-externs -Winline -Wno-long-long \
                -Wuninitialized -Wconversion -Wstrict-prototypes
AM_CFLAGS += -D_BSD_SOURCE

pkgclip_CFLAGS = ${AM_CFLAGS} @GTK_CFLAGS@
pkgclip_LDADD = @GTK_LIBS@ -lalpm
pkgclip_SOURCES = xpm.h pkgclip.h main.c util.h util.c

pkgclip_dbus_CFLAGS = ${AM_CFLAGS} @POLKIT_CFLAGS@
pkgclip_dbus_LDADD = -lalpm @POLKIT_LIBS@
pkgclip_dbus_SOURCES = pkgclip-dbus.c

org.jjk.PkgClip.service: org.jjk.PkgClip.service.tpl
	sed 's|@BINDIR@|$(bindir)|' org.jjk.PkgClip.service.tpl > org.jjk.PkgClip.service

pkgclip.1: pkgclip.pod
	pod2man --center='Cached Packages Trimmer Utility' --section=1 --release=$(PACKAGE_VERSION) pkgclip.pod pkgclip.1

index.html: pkgclip.1
	groff -T html -man pkgclip.1 > index.html

pkgclip.xpm: xpm.h
	echo "/* XPM */" > pkgclip.xpm
	cat xpm.h >> pkgclip.xpm

