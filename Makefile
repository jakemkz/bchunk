all: bchunk

# For systems with GCC (Linux, and others with GCC installed):
CC = gcc
LD = gcc
CFLAGS = -Wall -Wstrict-prototypes -O2
CFLAGS_PED = -Wall -Wstrict-prototypes -O2 -g -Wextra -pedantic  -Wformat -Wconversion -Wstrict-aliasing -Wundef -Wshadow -Wsign-conversion -fstrict-overflow

# For systems with a legacy CC:
#CC = cc
#LD = cc
#CFLAGS = -O

# For BSD install: Which install to use and where to put the files
INSTALL = install
INSTALL_DIR = $(INSTALL) -d -m 0755
INSTALL_DATA = $(INSTALL) -m 0644
INSTALL_EXEC = $(INSTALL) -m 0755
PREFIX  = /usr/local
BIN_DIR = $(PREFIX)/bin
MAN_DIR = $(PREFIX)/man

.c.o:
	$(CC) $(CFLAGS) -c $<

debug: CFLAGS = $(CFLAGS_PED)
debug: bchunk

clean:
	rm -f *.o *~ *.bak core
distclean: clean
	rm -f bchunk

install: installbin installman
installbin:
	$(INSTALL_DIR) $(DESTDIR)$(BIN_DIR)
	$(INSTALL_EXEC) -s bchunk $(DESTDIR)$(BIN_DIR)
installman:
	$(INSTALL_DIR) $(DESTDIR)$(MAN_DIR)/man1
	$(INSTALL_DATA) bchunk.1 $(DESTDIR)$(MAN_DIR)/man1

BITS = bchunk.o

bchunk: $(BITS)
	$(LD) $(LDFLAGS) -o bchunk $(BITS)

bchunk.o:	bchunk.c

