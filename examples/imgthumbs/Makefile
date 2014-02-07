# change PREFIX to install elsewhere (default: /usr/local)
PREFIX = /usr/local

src = $(wildcard src/*.c)
obj = $(src:.c=.o)
bin = imgthumbs

CFLAGS = -pedantic -Wall -g -I../../src
LDFLAGS = $(libgl) -limago $(resman) -lpthread

ifeq ($(shell uname -s), Darwin)
	libgl = -framework OpenGL -framework GLUT -lGLEW
else
	libgl = -lGL -lGLU -lglut -lGLEW
endif

resman = ../../libresman.a

$(bin): $(obj) resman
	$(CC) -o $@ $(obj) $(LDFLAGS)

.PHONY: resman
resman:
	$(MAKE) -C ../..

.PHONY: clean
clean:
	rm -f $(obj) $(bin)

.PHONY: install
install:
	mkdir -p $(DESTDIR)$(PREFIX)/bin
	cp $(bin) $(DESTDIR)$(PREFIX)/bin/$(bin)

.PHONY: uninstall
uninstall:
	rm -f $(DESTDIR)$(PREFIX)/bin/$(bin)
