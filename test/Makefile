# Host tests for Meridian's pure logic. Nothing here is compiled into the FAP:
# application.fam lists its sources explicitly so this directory stays out of
# the firmware build.
CC     ?= cc
CFLAGS ?= -std=c11 -Wall -Wextra -Werror -O2 -g
LDLIBS := -lm

H := ../helpers

BINS = host_nmea_test host_detect_test

.PHONY: all run clean
all: run

host_nmea_test: host_nmea_test.c $(H)/mrd_nmea.c $(H)/mrd_nmea.h
	$(CC) $(CFLAGS) -o $@ host_nmea_test.c $(H)/mrd_nmea.c $(LDLIBS)

host_detect_test: host_detect_test.c $(H)/mrd_detect.c $(H)/mrd_detect.h \
                  $(H)/mrd_nmea.c $(H)/mrd_nmea.h $(H)/mrd_sim.c $(H)/mrd_sim.h
	$(CC) $(CFLAGS) -o $@ host_detect_test.c $(H)/mrd_detect.c $(H)/mrd_nmea.c \
	      $(H)/mrd_sim.c $(LDLIBS)

run: $(BINS)
	@./host_nmea_test
	@echo
	@./host_detect_test

clean:
	rm -rf $(BINS) *.dSYM
