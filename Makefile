
PREFIX = /usr/local
RM = rm -f
INSTALL = install -p
INSTALL_EXEC = $(INSTALL) -m 0755
INSTALL_DATA = $(INSTALL) -m 0644
LUA_VERSION = 5.2
CFLAGS = -Wall -O2 -g -std=c99 -pedantic

uname_S := $(shell sh -c 'uname -s 2>/dev/null || echo not')

ifeq ($(uname_S), Darwin)
	SHARELIB_FLAGS = -fPIC -dynamiclib -Wl,-undefined,dynamic_lookup
else
	SHARELIB_FLAGS = -fPIC --shared
endif

all: luasocketlib.so

luasocketlib.so: luasocketlib.c
	$(CC) $(CFLAGS) $(SHARELIB_FLAGS) -o socket.so $^

install: all
	$(INSTALL_DATA) socket.so $(PREFIX)/lib/lua/$(LUA_VERSION)
	$(INSTALL_DATA) socket.lua $(PREFIX)/lib/lua/$(LUA_VERSION)

uninstall:
	$(RM) $(PREFIX)/lib/lua/$(LUA_VERSION)/socket.so
	$(RM) $(PREFIX)/lib/lua/$(LUA_VERSION)/socket.lua

clean:
	$(RM) socket.so
	$(RM) -r socket.so.dSYM

test: all
	@prove --exec=lua --timer t/*.lua

.PHONY: all install uninstall clean test
