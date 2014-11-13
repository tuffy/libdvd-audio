FLAGS = -Wall -g
BINARIES = array bitstream huffman dvdainfo
SRC = src
INCLUDE = include
UTILS = utils

DVDA_OBJS = dvda.o \
aob.o \
audio_ts.o \
pcm.o \
bitstream.o \
array.o \
huffman.o \
func_io.o \
mini-gmp.o

all: $(BINARIES)

clean:
	rm -f $(BINARIES) *.o

dvdainfo: $(UTILS)/dvdainfo.c dvda.a
	$(CC) $(FLAGS) $(UTILS)/dvdainfo.c -I $(INCLUDE) dvda.a -o $@

dvda.a: $(DVDA_OBJS)
	$(AR) -r $@ $(DVDA_OBJS)

dvda.o: $(INCLUDE)/dvda.h $(SRC)/dvda.c
	$(CC) $(FLAGS) -c $(SRC)/dvda.c -I $(INCLUDE)

aob.o: $(SRC)/aob.h $(SRC)/aob.c
	$(CC) $(FLAGS) -c $(SRC)/aob.c

audio_ts.o: $(SRC)/audio_ts.h $(SRC)/audio_ts.c
	$(CC) $(FLAGS) -c $(SRC)/audio_ts.c

pcm.o: $(SRC)/pcm.h $(SRC)/pcm.c
	$(CC) $(FLAGS) -c $(SRC)/pcm.c

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

bitstream.a: bitstream.o huffman.o func_io.o mini-gmp.o
	$(AR) -r $@ bitstream.o huffman.o func_io.o mini-gmp.o

bitstream: $(SRC)/bitstream.c $(SRC)/bitstream.h huffman.o func_io.o mini-gmp.o
	$(CC) $(FLAGS) $(SRC)/bitstream.c huffman.o func_io.o mini-gmp.o -DEXECUTABLE -DDEBUG -o $@

array: $(SRC)/array.c $(SRC)/array.h
	$(CC) $(FLAGS) $(SRC)/array.c -DEXECUTABLE -o $@

parson.o: $(SRC)/parson.c $(SRC)/parson.h
	$(CC) $(FLAGS) -c $(SRC)/parson.c
