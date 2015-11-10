# Makefile for tleds and xtleds.
# GNU GPL (c) 1997, 1998 Jouni.Lohikoski@iki.fi

all:	tleds xtleds say_install

# Where to install programs and man pages
BINDIR	=	/usr/local/bin/
MANDIR	=	/usr/local/man/
INSTALL =	install
INSTALL_DATA = $(INSTALL) -m 0644
INSTALL_BIN  = $(INSTALL) -m 0755
INSTALL_DIR  = $(INSTALL) -d
LN	=	ln
LN_S	=	$(LN) -sf

# For 2.1.x kernels, you have to include -DKERNEL2_1 option for gcc

GCCOPTS = -D_GNU_SOURCE -O3 -Wall -DKERNEL2_1

# The first one is if you want to include X code
xtleds:	tleds.c
	# Making xtleds
	gcc $(GCCOPTS) -o xtleds tleds.c -I /usr/X11R6/include/ -L /usr/X11R6/lib/ -lX11

# This second one works only when started in VT. Check the REMOVE_X_CODE
# in the source code.
tleds:	tleds.c
	# Making tleds
	gcc -DNO_X_SUPPORT $(GCCOPTS) -o tleds tleds.c

help:
	# make help	-	this.
	# make tleds	-	makes tleds.
	# make xtleds	-	makes xtleds.
	# make strip	-	strips them.
	# make install	-	installs tleds, xtleds and tleds man page
	#			if EUID root.
	# make all	-	make tleds and xtleds

strip:
	strip --strip-all tleds
	strip --strip-all xtleds
	
say_install:
	# Now su root and run:  make install
	# If you want to strip them first, say: make strip install

install: tleds xtleds
	# EUID root needed !
	# installing ....
	# If you get an error here, you are not root or may have tleds running
	# on the system. tleds -k  first and then make install again as root. 
	$(INSTALL_DIR) $(DESTDIR)$(BINDIR)
	$(INSTALL_BIN) tleds $(DESTDIR)$(BINDIR)/tleds
	$(INSTALL_BIN) xtleds $(DESTDIR)$(BINDIR)/xtleds
	$(INSTALL_DIR) $(DESTDIR)$(MANDIR)/man1
	$(INSTALL_DATA) tleds.1 $(DESTDIR)$(MANDIR)/man1/tleds.1
	$(LN_S) tleds.1 $(DESTDIR)$(MANDIR)/man1/xtleds.1
	# ....Done.

uninstall:
	$(RM) $(DESTDIR)$(BINDIR)/tleds
	$(RM) $(DESTDIR)$(BINDIR)/xtleds
	$(RM) $(DESTDIR)$(MANDIR)/man1/xtleds.1
	$(RM) $(DESTDIR)$(MANDIR)/man1/tleds.1
