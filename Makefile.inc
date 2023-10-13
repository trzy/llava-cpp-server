###############################################################################
# llama.cpp
###############################################################################

include llama.cpp/Makefile

# Take llama files and prepend llama.cpp/ to them
LLAMA_OBJS = $(foreach file,$(OBJS),llama.cpp/$(file))
LLAMA_DEPS = $(foreach file,$(COMMON_DEPS),llama.cpp/$(file))


###############################################################################
# cpp-httplib
###############################################################################

HTTP_CXXFLAGS = -O2 -std=c++11 -I.. -Wall -Wextra -pthread
HTTP_ZLIB_SUPPORT = -DCPPHTTPLIB_ZLIB_SUPPORT -lz


###############################################################################
# llava-server rules
###############################################################################

#
# bin/ and obj/ directories
#
bin:
	$(info Creating directory: bin/)
	mkdir bin

obj:
	$(info Creating directory: obj/)
	mkdir obj

#
# LLaVa dependencies in llama.cpp/examples/llava/ (e.g., clip.cpp)
#
obj/%.o:	llama.cpp/examples/llava/%.cpp
	$(CXX) -Illama.cpp -Illama.cpp/common -c -o $@ $< $(CXXFLAGS) -I. -std=c++17

#
# Our modules
#
obj/llava_server.o:	llava_server.cpp llama.cpp/examples/llava/llava-utils.h llama.cpp/examples/llava/clip.h llama.cpp/common/stb_image.h
	$(CXX) -Illama.cpp -Illama.cpp/common -c -o $@ $< $(CXXFLAGS) -I. -std=c++17

obj/web_server.o: web_server.cpp cpp-httplib/httplib.h
	$(CXX) -c -o $@ $< $(HTTP_CXXFLAGS) -I.

#
# Output binary
# 
bin/llava-server: obj/llava_server.o obj/web_server.o llama.cpp/ggml.o llama.cpp/llama.o obj/clip.o $(LLAMA_DEPS) $(LLAMA_OBJS)
	$(CXX) -Illama.cpp -Illama.cpp/common $(CXXFLAGS) -o $@ $(LDFLAGS) -Wno-cast-qual $(HTTP_ZLIB_SUPPORT) $(filter-out %.h,$^)

#
# Build llama.cpp
#
.PHONY: llama-base
llama-base:
	$(info Bulding llama.cpp...)
	cd llama.cpp && $(MAKE)

#
# Build all
#
build-all: llama-base obj bin bin/llava-server
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