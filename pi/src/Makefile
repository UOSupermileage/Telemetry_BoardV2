CFLAGS ?= -O2 -Wall -Wextra
BINS   = ntrip_rtk gpscmd can_reader

all: $(BINS)

ntrip_rtk: ntrip_rtk.c common.c common.h
	$(CC) $(CFLAGS) -o $@ ntrip_rtk.c common.c

gpscmd: gpscmd.c common.c common.h
	$(CC) $(CFLAGS) -o $@ gpscmd.c common.c

can_reader: can_reader.c
	$(CC) $(CFLAGS) -o $@ can_reader.c

install: all
	install -m 755 $(BINS) /usr/local/bin/

clean:
	rm -f $(BINS)

.PHONY: all install clean
