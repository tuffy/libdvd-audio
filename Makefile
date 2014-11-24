FLAGS = -Wall -O2 -g -fPIC
LIB_DIR = /usr/local/lib
INCLUDE_DIR = /usr/local/include
BIN_DIR = /usr/local/bin
PKG_CONFIG_DIR = /usr/lib/pkgconfig

BITSTREAM_OBJS = bitstream.o \
huffman.o \
func_io.o \
mini-gmp.o

DVDA_OBJS = dvd-audio.o \
aob.o \
packet.o \
audio_ts.o \
pcm.o \
mlp.o \
$(BITSTREAM_OBJS) \
array.o

CODEBOOKS = \
src/mlp_codebook1.h \
src/mlp_codebook2.h \
src/mlp_codebook3.h

# extract version numbers from main header file
MAJOR_VERSION = $(shell awk '$$2 == "LIBDVDAUDIO_MAJOR_VERSION" {print $$3;}' include/dvd-audio.h)
MINOR_VERSION = $(shell awk '$$2 == "LIBDVDAUDIO_MINOR_VERSION" {print $$3;}' include/dvd-audio.h)
RELEASE_VERSION = $(shell awk '$$2 == "LIBDVDAUDIO_RELEASE_VERSION" {print $$3;}' include/dvd-audio.h)

STATIC_LIBRARY = libdvd-audio.a

SHARED_LIBRARY = libdvd-audio.so.$(MAJOR_VERSION).$(MINOR_VERSION).$(RELEASE_VERSION)

SHARED_LIBRARY_LINK_1 = libdvd-audio.so.$(MAJOR_VERSION)

SHARED_LIBRARY_LINK_2 = libdvd-audio.so

SHARED_LIBRARIES = $(SHARED_LIBRARY) \
$(SHARED_LIBRARY_LINK_1) \
$(SHARED_LIBRARY_LINK_2)

BINARIES = dvda-debug-info dvda2wav

PKG_CONFIG_METADATA = libdvd-audio.pc

# extract system name from uname
UNAME_S := $(shell uname -s)
ifeq ($(UNAME_S), Linux)
	DVDA_OBJS += cppm.o ioctl.o dvd_css.o
	AOB_FLAGS = -DHAS_CPPM
else
	AOB_FLAGS =
endif

all: $(STATIC_LIBRARY) $(SHARED_LIBRARIES) $(BINARIES) $(PKG_CONFIG_METADATA)

install: $(STATIC_LIBRARY) $(SHARED_LIBRARIES) $(BINARIES) $(PKG_CONFIG_METADATA)
	install -m 644 $(SHARED_LIBRARY) $(LIB_DIR)
	cp -Pp $(SHARED_LIBRARY_LINK_1) $(LIB_DIR)
	cp -Pp $(SHARED_LIBRARY_LINK_2) $(LIB_DIR)
	install -m 644 $(STATIC_LIBRARY) $(LIB_DIR)
	install -m 644 include/dvd-audio.h $(INCLUDE_DIR)
	install -m 755 $(BINARIES) $(BIN_DIR)
	install -m 644 $(PKG_CONFIG_METADATA) $(PKG_CONFIG_DIR)

clean:
	rm -f $(BINARIES) $(CODEBOOKS) $(BINARIES) $(PKG_CONFIG_METADATA) huffman *.o *.a *.so*

libdvd-audio.a: $(DVDA_OBJS)
	$(AR) -r $@ $(DVDA_OBJS)

$(SHARED_LIBRARY): $(DVDA_OBJS)
	$(CC) $(FLAGS) -Wl,-soname,libdvd-audio.so.$(MAJOR_VERSION) -shared -o $@ $(DVDA_OBJS)

$(SHARED_LIBRARY_LINK_1): $(SHARED_LIBRARY)
	ln -sf $< $@

$(SHARED_LIBRARY_LINK_2): $(SHARED_LIBRARY)
	ln -sf $< $@

dvd-audio.o: include/dvd-audio.h src/dvd-audio.c
	$(CC) $(FLAGS) -c src/dvd-audio.c -I include

aob.o: src/aob.h src/aob.c
	$(CC) $(FLAGS) -c src/aob.c $(AOB_FLAGS)

packet.o: src/packet.h src/packet.c
	$(CC) $(FLAGS) -c src/packet.c

audio_ts.o: src/audio_ts.h src/audio_ts.c
	$(CC) $(FLAGS) -c src/audio_ts.c

pcm.o: src/pcm.h src/pcm.c
	$(CC) $(FLAGS) -c src/pcm.c

mlp.o: src/mlp.h src/mlp.c $(CODEBOOKS)
	$(CC) $(FLAGS) -c src/mlp.c

src/mlp_codebook1.h: src/mlp_codebook1.json huffman
	./huffman -i src/mlp_codebook1.json > $@

src/mlp_codebook2.h: src/mlp_codebook2.json huffman
	./huffman -i src/mlp_codebook2.json > $@

src/mlp_codebook3.h: src/mlp_codebook3.json huffman
	./huffman -i src/mlp_codebook3.json > $@

cppm.o: src/cppm/cppm.h src/cppm/cppm.c
	$(CC) $(FLAGS) -c src/cppm/cppm.c

ioctl.o: src/cppm/ioctl.h src/cppm/ioctl.c
	$(CC) $(FLAGS) -c src/cppm/ioctl.c -DHAVE_LINUX_DVD_STRUCT -DDVD_STRUCT_IN_LINUX_CDROM_H

dvd_css.o: src/cppm/dvd_css.h src/cppm/dvd_css.c
	$(CC) $(FLAGS) -c src/cppm/dvd_css.c

dvda-debug-info: utils/dvda-debug-info.c libdvd-audio.a
	$(CC) $(FLAGS) -o $@ utils/dvda-debug-info.c libdvd-audio.a -I include -lm

dvda2wav: utils/dvda2wav.c libdvd-audio.a
	$(CC) $(FLAGS) -o $@ utils/dvda2wav.c libdvd-audio.a -I include -I src -lm

$(PKG_CONFIG_METADATA): libdvd-audio.pc.m4
	m4 -DLIB_DIR=$(LIB_DIR) -DINCLUDE_DIR=$(INCLUDE_DIR) -DMAJOR_VERSION=$(MAJOR_VERSION) -DMINOR_VERSION=$(MINOR_VERSION) -DRELEASE_VERSION=$(RELEASE_VERSION) $< > $@

huffman: src/huffman.c src/huffman.h parson.o
	$(CC) $(FLAGS) -o huffman src/huffman.c parson.o -DEXECUTABLE

bitstream.o: src/bitstream.c src/bitstream.h
	$(CC) $(FLAGS) -c src/bitstream.c

array.o: src/array.h src/array.c
	$(CC) $(FLAGS) -c src/array.c

huffman.o: src/huffman.c src/huffman.h
	$(CC) $(FLAGS) -c src/huffman.c -DSTANDALONE

func_io.o: src/func_io.c src/func_io.h
	$(CC) $(FLAGS) -c src/func_io.c

mini-gmp.o: src/mini-gmp.c src/mini-gmp.h
	$(CC) $(FLAGS) -c src/mini-gmp.c

bitstream.a: $(BITSTREAM_OBJS)
	$(AR) -r $@ $(BITSTREAM_OBJS)

bitstream: src/bitstream.c src/bitstream.h huffman.o func_io.o mini-gmp.o
	$(CC) $(FLAGS) src/bitstream.c huffman.o func_io.o mini-gmp.o -DEXECUTABLE -o $@

array: src/array.c src/array.h
	$(CC) $(FLAGS) src/array.c -DEXECUTABLE -o $@

parson.o: src/parson.c src/parson.h
	$(CC) $(FLAGS) -c src/parson.c
