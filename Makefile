#
# Makefile:
# Makefile for iftop.
#
# $Id: Makefile,v 1.3 2003/12/15 20:42:38 pdw Exp $
#

VERSION = 0.2

# C compiler to use.
#CC = gcc

# CFLAGS += -I/usr/pkg/include
# CFLAGS += -pg -a

# LDFLAGS += -pg -a

# PREFIX specifies the base directory for the installation.
PREFIX = /usr/local
#PREFIX = /software

# BINDIR is where the binary lives relative to PREFIX (no leading /).
BINDIR = sbin

# MANDIR is where the manual page goes.
MANDIR = man
#MANDIR = share/man     # FHS-ish

# You shouldn't need to change anything below this point.
CFLAGS  += -g -Wall 
LDFLAGS += -g 
# LDLIBS += 

SRCS = slimp3slave.c util.c
HDRS = util.h
TXTS = 
SPECFILE = 

OBJS = $(SRCS:.c=.o)

slimp3slave: $(OBJS) Makefile
	$(CC) $(LDFLAGS) -o $@ $(OBJS) $(LDLIBS) 

#install: iftop
#	install -D iftop   $(PREFIX)/$(BINDIR)/iftop
#	install -D iftop.8 $(PREFIX)/$(MANDIR)/man8/iftop.8

#uninstall:
#	rm -f $(PREFIX)/$(BINDIR)/iftop $(PREFIX)/$(MANDIR)/man8/iftop.8

%.o: %.c Makefile
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f *~ *.o core 

tarball: depend $(SRCS) $(HDRS) $(TXTS) $(SPECFILE)
	mkdir slimp3slave-$(VERSION)
	set -e ; for i in Makefile depend $(SRCS) $(HDRS) $(TXTS) $(SPECFILE) ; do cp $$i slimp3slave-$(VERSION)/$$i ; done
	tar cvf - slimp3slave-$(VERSION) | gzip --best > slimp3slave-$(VERSION).tar.gz
	rm -rf slimp3slave-$(VERSION)

tags :
	etags *.c *.h

depend: $(SRCS)
	$(CPP) $(CFLAGS)  -MM $(SRCS) > depend

nodepend:
	rm -f depend

include depend
