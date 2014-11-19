FLAGS = -Wall -g -DDEBUG
BINARIES = dvda-debug-info dvda2wav
SRC = src
INCLUDE = include
UTILS = utils

BITSTREAM_OBJS = bitstream.o \
huffman.o \
func_io.o \
mini-gmp.o

DVDA_OBJS = dvd-audio.o \
aob.o \
audio_ts.o \
pcm.o \
mlp.o \
$(BITSTREAM_OBJS) \
array.o

CODEBOOKS = \
$(SRC)/mlp_codebook1.h \
$(SRC)/mlp_codebook2.h \
$(SRC)/mlp_codebook3.h

UNAME_S := $(shell uname -s)
ifeq ($(UNAME_S), Linux)
	DVDA_OBJS += cppm.o ioctl.o dvd_css.o
	AOB_FLAGS = -DHAS_CPPM
else
	AOB_FLAGS =
endif


all: $(BINARIES)

clean:
	rm -f $(BINARIES) $(CODEBOOKS) huffman *.o *.a

dvd-audio.a: $(DVDA_OBJS)
	$(AR) -r $@ $(DVDA_OBJS)

dvd-audio.o: $(INCLUDE)/dvd-audio.h $(SRC)/dvd-audio.c
	$(CC) $(FLAGS) -c $(SRC)/dvd-audio.c -I $(INCLUDE)

aob.o: $(SRC)/aob.h $(SRC)/aob.c
	$(CC) $(FLAGS) -c $(SRC)/aob.c $(AOB_FLAGS)

audio_ts.o: $(SRC)/audio_ts.h $(SRC)/audio_ts.c
	$(CC) $(FLAGS) -c $(SRC)/audio_ts.c

pcm.o: $(SRC)/pcm.h $(SRC)/pcm.c
	$(CC) $(FLAGS) -c $(SRC)/pcm.c

mlp.o: $(SRC)/mlp.h $(SRC)/mlp.c $(CODEBOOKS)
	$(CC) $(FLAGS) -c $(SRC)/mlp.c

$(SRC)/mlp_codebook1.h: $(SRC)/mlp_codebook1.json huffman
	./huffman -i $(SRC)/mlp_codebook1.json > $@

$(SRC)/mlp_codebook2.h: $(SRC)/mlp_codebook2.json huffman
	./huffman -i $(SRC)/mlp_codebook2.json > $@

$(SRC)/mlp_codebook3.h: $(SRC)/mlp_codebook3.json huffman
	./huffman -i $(SRC)/mlp_codebook3.json > $@

cppm.o: $(SRC)/cppm/cppm.h $(SRC)/cppm/cppm.c
	$(CC) $(FLAGS) -c $(SRC)/cppm/cppm.c

ioctl.o: $(SRC)/cppm/ioctl.h $(SRC)/cppm/ioctl.c
	$(CC) $(FLAGS) -c $(SRC)/cppm/ioctl.c -DHAVE_LINUX_DVD_STRUCT -DDVD_STRUCT_IN_LINUX_CDROM_H

dvd_css.o: $(SRC)/cppm/dvd_css.h $(SRC)/cppm/dvd_css.c
	$(CC) $(FLAGS) -c $(SRC)/cppm/dvd_css.c

dvda-debug-info: $(UTILS)/dvda-debug-info.c dvd-audio.a
	$(CC) $(FLAGS) -o $@ $(UTILS)/dvda-debug-info.c dvd-audio.a -I $(INCLUDE) -lm

dvda2wav: $(UTILS)/dvda2wav.c dvd-audio.a
	$(CC) $(FLAGS) -o $@ $(UTILS)/dvda2wav.c dvd-audio.a -I $(INCLUDE) -I $(SRC) -lm

huffman: $(SRC)/huffman.c $(SRC)/huffman.h parson.o
	$(CC) $(FLAGS) -o huffman $(SRC)/huffman.c parson.o -DEXECUTABLE

bitstream.o: $(SRC)/bitstream.c $(SRC)/bitstream.h
	$(CC) $(FLAGS) -c $(SRC)/bitstream.c

array.o: $(SRC)/array.h $(SRC)/array.c
	$(CC) $(FLAGS) -c $(SRC)/array.c

huffman.o: $(SRC)/huffman.c $(SRC)/huffman.h
	$(CC) $(FLAGS) -c $(SRC)/huffman.c -DSTANDALONE

func_io.o: $(SRC)/func_io.c $(SRC)/func_io.h
	$(CC) $(FLAGS) -c $(SRC)/func_io.c

mini-gmp.o: $(SRC)/mini-gmp.c $(SRC)/mini-gmp.h
	$(CC) $(FLAGS) -c $(SRC)/mini-gmp.c

bitstream.a: $(BITSTREAM_OBJS)
	$(AR) -r $@ $(BITSTREAM_OBJS)

bitstream: $(SRC)/bitstream.c $(SRC)/bitstream.h huffman.o func_io.o mini-gmp.o
	$(CC) $(FLAGS) $(SRC)/bitstream.c huffman.o func_io.o mini-gmp.o -DEXECUTABLE -o $@

array: $(SRC)/array.c $(SRC)/array.h
	$(CC) $(FLAGS) $(SRC)/array.c -DEXECUTABLE -o $@

parson.o: $(SRC)/parson.c $(SRC)/parson.h
	$(CC) $(FLAGS) -c $(SRC)/parson.c
