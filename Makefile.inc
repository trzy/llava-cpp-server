###############################################################################
# llama.cpp
###############################################################################

include llama.cpp/Makefile

# Take llama files and prepend llama.cpp/ to them
LLAMA_OBJS = $(foreach file,$(OBJS),llama.cpp/$(file))

###############################################################################
# cpp-httplib
###############################################################################

HTTP_CXXFLAGS = -O2 -std=c++11 -I.. -Wall -Wextra -pthread
HTTP_ZLIB_SUPPORT = -DCPPHTTPLIB_ZLIB_SUPPORT -lz


###############################################################################
# llava-server rules
###############################################################################

bin:
	$(info Creating directory: bin/)
	mkdir bin

obj:
	$(info Creating directory: obj/)
	mkdir obj

obj/web_server.o: web_server.cpp cpp-httplib/httplib.h
	$(CXX) -c -o $@ $< $(HTTP_CXXFLAGS) $(HTTP_ZLIB_SUPPORT)

bin/llava-server: obj/web_server.o
	$(CXX) -Illama.cpp -Illama.cpp/common $(CXXFLAGS) -o $@ $(LDFLAGS) -Wno-cast-qual $< -lz

#llava-server: llama.cpp/examples/llava/llava.cpp llama.cpp/examples/llava/llava-utils.h llama.cpp/examples/llava/clip.cpp llama.cpp/examples/llava/clip.h llama.cpp/common/stb_image.h llama.cpp/ggml.o llama.cpp/llama.o $(COMMON_DEPS) $(OBJS)
#	$(CXX) $(CXXFLAGS) $(filter-out %.h,$^) -o $@ $(LDFLAGS) -Wno-cast-qual

$(LLAMA_OBJS):
	$(info Building llama.cpp...)
	cd llama.cpp && $(MAKE)

#
# Build all
#
build-all: obj bin bin/llava-server
	@echo $(LLAMA_OBJS)

#
# Clean all
#
.PHONY: build-clean
build-clean:
	-rm bin/*
	-rm obj/*
	-rmdir bin
	-rmdir obj
	cd llama.cpp && $(MAKE) clean