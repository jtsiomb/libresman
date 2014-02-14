# change this if you want to install elsewhere
PREFIX = /usr/local

src = $(wildcard src/*.c)
obj = $(src:.c=.o)
dep = $(obj:.o=.d)

name = resman
lib_a = lib$(name).a

api_major = 0
api_minor = 1

dbg = -g

CFLAGS = -pedantic -Wall $(dbg) $(opt) $(pic)

ifeq ($(shell uname -s), Darwin)
	lib_so = lib$(name).dylib
	shared = -dynamiclib
else
	devlink = lib$(name).so
	soname = lib$(name).so.$(api_major)
	lib_so = lib$(name).so.$(api_major).$(api_minor)
	shared = -shared -Wl,-soname,$(soname)
	pic = -fPIC
endif

.PHONY: all
all: $(lib_so) $(lib_a)

$(lib_so): $(obj)
	$(CC) -o $@ $(shared) $(obj) $(LDFLAGS)

$(lib_a): $(obj)
	$(AR) rcs $@ $(obj)

-include $(dep)

%.d: %.c
	@$(CPP) $(CFLAGS) $< -MM -MT $(@:.d=.o) >$@

.PHONY: clean
clean:
	rm -f $(obj) $(lib_so) $(lib_a)

.PHONY: install
install: $(lib_a) $(lib_so)
	mkdir -p $(DESTDIR)$(PREFIX)/lib $(DESTDIR)$(PREFIX)/include
	cp $(lib_a) $(DESTDIR)$(PREFIX)/lib/$(lib_a)
	cp $(lib_so) $(DESTDIR)$(PREFIX)/lib/$(lib_so)
	[ -n "$(devlink)" ] \
		&& cd $(DESTDIR)$(PREFIX)/lib \
		&& rm -f $(soname) $(devlink) \
		&& ln -s $(lib_so) $(soname) \
		&& ln -s $(soname) $(devlink) \
		|| true
	cp src/resman.h $(DESTDIR)$(PREFIX)/include/resman.h

.PHONY: uninstall
uninstall:
	rm -f $(DESTDIR)$(PREFIX)/lib/$(lib_a)
	rm -f $(DESTDIR)$(PREFIX)/lib/$(lib_so)
	rm -f $(DESTDIR)$(PREFIX)/include/resman.h
