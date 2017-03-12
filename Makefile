# Makefile for ep_tmc6xx
#
INSTALL=install

CUPS_FILTERS=/usr/lib/cups/filter
CUPS_PPDS=/usr/share/ppd/tmc6xx


FILTERS=rastertotmc6xx

PPD=ep_tmc610 ep_tmc600

PPD_FILES=$(PPD:%=ppd/%.ppd)

all: $(PPD_FILES) $(FILTERS)

$(PPD_FILES): ep_tmc6xx.drv
	ppdc $<

rastertotmc6xx: rastertotmc6xx.c
	$(CC) -o $@ $< -lcupsimage -lcupsfilters -lcups

clean:
	rm -f $(PPD_FILES) $(FILTERS)

install: $(PPD_FILES) $(FILTERS)
	mkdir -p $(CUPS_PPDS)
	for p in $(PPD_FILES); do \
		$(INSTALL) $$p $(CUPS_PPDS); \
	done
	for f in $(FILTERS); do \
		$(INSTALL) $$f $(CUPS_FILTERS); \
	done
