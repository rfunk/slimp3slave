#
# Makefile:
# Makefile for iftop.
#
# $Id: Makefile,v 1.1 2003/12/13 15:11:29 pdw Exp $
#

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

#tarball: depend $(SRCS) $(HDRS) $(TXTS) $(SPECFILE)
#	mkdir iftop-$(VERSION)
#	set -e ; for i in Makefile depend $(SRCS) $(HDRS) $(TXTS) $(SPECFILE) ; do cp $$i iftop-$(VERSION)/$$i ; done
#	tar cvf - iftop-$(VERSION) | gzip --best > iftop-$(VERSION).tar.gz
#	rm -rf iftop-$(VERSION)
#
tags :
	etags *.c *.h

depend: $(SRCS)
	$(CPP) $(CFLAGS)  -MM $(SRCS) > depend

nodepend:
	rm -f depend

include depend
