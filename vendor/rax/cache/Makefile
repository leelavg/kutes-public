LIBNAME ?=	librax.so

PREFIX ?=	/usr
LIBDIR ?=	$(PREFIX)/lib
INCLUDEDIR ?=	$(PREFIX)/include

INSTALL ?=	install

CFLAGS?= -O2 -Wall -W -std=c99
LDFLAGS +=	-lm

# if DEBUG env var is set, we compile with "debug" cflags
ifeq ($(DEBUG),1)
	CFLAGS += -g -ggdb -fno-omit-frame-pointer
	LDFLAGS += -g -ggdb
endif

# if COV env var is set, we compile with "coverage" cflags
ifeq ($(COV),1)
	CFLAGS+=-fprofile-arcs -ftest-coverage
	LDFLAGS+=-lgcov
endif

all: $(LIBNAME)
test: rax-test rax-oom-test
	LD_LIBRARY_PATH=. ./rax-test --units
	LD_LIBRARY_PATH=. ./rax-oom-test > /dev/null

$(LIBNAME): rax.o
	$(CC) -shared $^ -o $@ -Wl,--soname,$@ $(LDFLAGS)

rax.o: rax.h
rax-test.o: rax.h
rax-oom-test.o: rax.h

rax-test: rax-test.o rc4rand.o crc16.o
	$(CC) -o $@ $^ $(CFLAGS) $(LDFLAGS) -L. -lrax

rax-oom-test: rax-oom-test.o
	$(CC) -o $@ $^ $(CFLAGS) $(LDFLAGS) -L. -lrax

.c.o:
	$(CC) -fPIC -shared -c $(CFLAGS) $<

clean:
	rm -f rax-test rax-oom-test *.gcda *.gcov *.gcno *.o $(LIBNAME)

install: $(LIBNAME)
	$(INSTALL) -m644 rax.h -D $(DESTDIR)$(INCLUDEDIR)/rax.h
	$(INSTALL) -m644 $(LIBNAME) -D $(DESTDIR)$(LIBDIR)/$(LIBNAME)
