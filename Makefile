#
# Makefile
# Bart Trzynadlowski, 2023
#
# Builds llava-server and its dependencies (namely, llama.cpp).
#


all:
	$(MAKE) -f Makefile.inc build-all

.PHONY: clean
clean:
	$(MAKE) -f Makefile.inc build-clean
