all:
	$(MAKE) -f Makefile.inc build-all

.PHONY: clean
clean:
	$(MAKE) -f Makefile.inc build-clean