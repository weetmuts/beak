
LIBTAR_A:=libtar/lib/.libs/libtar.a
LIBTAR_SOURCES:=$(shell find libtar -name "*.c" -o -name "*.h")

CCFLAGS=-g -std=c++11 -Wall -Wno-unused-function \
	-Ilibtar/lib -Ilibtar/listhash -I/usr/include \
	`pkg-config fuse --cflags` 

HEADERS=$(wildcard *.h)
OBJS=	build/util.o \
	build/log.o \
	build/tarentry.o \
	build/tarfile.o \
	build/forward.o \
	build/reverse.o \
	build/main.o

OBJS2=  build/util.o \
	build/log.o \
	build/diff.o

$(shell mkdir -p build)

all: build/tarredfs build/tarredfs-diff build/tarredfs-untar build/tarredfs-pack build/tarredfs-compare build/tarredfs-integrity-test

build/tarredfs: $(OBJS) $(LIBTAR_A)
	g++ -g -std=c++11  $(OBJS) $(LIBTAR_A) `pkg-config fuse --libs`  `pkg-config openssl --libs` -o build/tarredfs 

build/tarredfs-diff: $(OBJS2)
	g++ -g -std=c++11  $(OBJS2) -o build/tarredfs-diff 

build/%.o: %.cc $(HEADERS)
	g++ $(CCFLAGS) $< -c -o $@

build/tarredfs-untar: untar.sh
	cp untar.sh build/tarredfs-untar
	chmod a+x build/tarredfs-untar

build/tarredfs-pack: pack.sh
	cp pack.sh build/tarredfs-pack
	chmod a+x build/tarredfs-pack

build/tarredfs-compare: compare.sh
	cp compare.sh build/tarredfs-compare
	chmod a+x build/tarredfs-compare

build/tarredfs-integrity-test: integrity-test.sh
	cp integrity-test.sh build/tarredfs-integrity-test
	chmod a+x build/tarredfs-integrity-test

$(LIBTAR_A): $(LIBTAR_SOURCES)
	(cd libtar; autoreconf --force --install; ./configure ; make)

test:
	@./test.sh

install:
	echo Installing into /usr/local/bin
	cp build/tarredfs /usr/local/bin
	cp build/tarredfs-* /usr/local/bin
	mkdir -p /usr/local/lib/tarredfs
	cp format_find.pl /usr/local/lib/tarredfs/format_find.pl
	cp format_tar.pl /usr/local/lib/tarredfs/format_tar.pl
	cp tarredfs.1 /usr/local/share/man/man1

clean:
	rm -rf build/* *~

clean-all:
	(cd libtar; make clean)
	rm -rf build/* *~ 

.PHONY: clean clean-all
