#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <netdb.h>
#include <unistd.h>
#include <errno.h>
#include <assert.h>

#include <lua.h>
#include <lauxlib.h>

#include "compat.h"

#define MAX_BUFFER_SIZE 4096
#define SOCKET "SOCKET"

// Socket
typedef struct {
    int fd;
    int sock_family;
    int sock_type;
    int sock_protocol;
}socket_t;

#define get_sock_ptr(L) ((socket_t *)luaL_checkudata(L, 1, SOCKET));

static socket_t*
__socket_create(lua_State *L, int fd, int family, int type, int proto) {
    socket_t *s = (socket_t *)lua_newuserdata(L, sizeof(socket_t));
    if (!s) {
        luaL_error(L, "lua_newuserdata failed\n");
        return NULL;
    }
    s->fd = fd;
    s->sock_family = family;
    s->sock_type = type;
    s->sock_protocol = proto;
    luaL_setmetatable(L, SOCKET);
    return s;
}

static int
_socket(lua_State *L) {
    int family = (int)luaL_checknumber(L, 1);
    int type = (int)luaL_checknumber(L, 2);
    int proto = (int)luaL_optnumber(L, 3, 0);
    int fd;
    fd = socket(family, type, proto);
    if (fd < 0) {
        lua_pushnil(L);
        lua_pushstring(L, strerror(errno));
        return 2;
    }

    socket_t *s = __socket_create(L, fd, family, type, proto);
    if (!s) {
        return luaL_error(L, "__socket_create failed\n");
    }

    lua_pushboolean(L, 1);
    return 1;
}

static int
__get_sock_addr(lua_State *L, socket_t *s, struct sockaddr *addr, socklen_t *len) {
    switch (s->sock_family) {
        case AF_INET: {
            struct sockaddr_in *addr = (struct sockaddr_in *)addr;
            const char *host = luaL_checkstring(L, 2);
            int port = luaL_checknumber(L, 3);
            struct hostent *hostinfo = gethostbyname(host);
            if (hostinfo == NULL) {
                return 0;
            }
            addr->sin_family = AF_INET;
            addr->sin_port = htons(port);
            addr->sin_addr = *(struct in_addr *)hostinfo->h_addr_list[0];
            *len = sizeof(*addr);
            break;
        }
        case AF_INET6:
        case AF_UNIX:
        default:
            return 0;
    }
    return 1;
}

static int
_bind(lua_State *L) {
    socket_t *s = get_sock_ptr(L);
    struct sockaddr addr;
    socklen_t len;
    int rc;
    char *errStr = NULL;

    if (!__get_sock_addr(L, s, &addr, &len)) {
        goto err;
    }

    rc = bind(s->fd, &addr, len);
    if (rc < 0) {
        goto err;
    }

    lua_pushboolean(L, 1);
    return 1;
err:
    errStr = strerror(errno);
    lua_pushnil(L);
    lua_pushstring(L, errStr);
    return 2;
}

static int
_connect(lua_State *L) {
    socket_t *s = get_sock_ptr(L);
    struct sockaddr addr;
    socklen_t len;
    int rc;
    char *errStr = NULL;

    if (!__get_sock_addr(L, s, &addr, &len)) {
        errStr = strerror(errno);
        goto err;
    }

    rc = connect(s->fd, &addr, len);
    if (rc < 0) {
        errStr = strerror(errno);
        goto err;
    }

    lua_pushboolean(L, 1);
    return 1;
err:
    assert(errStr);
    lua_pushnil(L);
    lua_pushstring(L, errStr);
    return 2;
}

static int
_listen(lua_State *L) {
    socket_t *s = get_sock_ptr(L);
    int rc, num;
    char *errStr = NULL;
    num  = luaL_checknumber(L, 2);
    if (num < 0) {
        num = 0;
    }
    rc = listen(s->fd, num);
    if (rc < 0) {
        errStr = strerror(rc);
        goto err;
    }

    lua_pushboolean(L, 1);
    return 1;

err:
    assert(errStr);
    lua_pushnil(L);
    lua_pushstring(L, errStr);
    return 2;
}
    
static int
_accept(lua_State *L) {
    socket_t *s = get_sock_ptr(L);
    struct sockaddr addr;
    socklen_t len;
    int connectfd;
    char *errStr = NULL;

    connectfd = accept(s->fd, &addr, &len);
    if (connectfd == -1) {
        errStr = strerror(errno);
        goto err;
    }

    socket_t *c = __socket_create(L, connectfd, s->sock_family, s->sock_type, s->sock_protocol);
    if (!c) {
        luaL_error(L, "__socket_create failed\n");
        goto err;
    }

    return 1;
err:
    assert(errStr);
    lua_pushnil(L);
    lua_pushstring(L, errStr);
    return 2;
}

static int
_send(lua_State *L) {
	int fd = -1;
	if (!lua_isnil(L,1)) {
		fd = luaL_checkinteger(L,1);
	}

	size_t sz;
	int type = lua_type(L,2);
	const char * buffer = NULL;
	if (type == LUA_TSTRING) {
		buffer = lua_tolstring(L, 2, &sz);
	} else {
		luaL_checktype(L, 2, LUA_TLIGHTUSERDATA);
		buffer = lua_touserdata(L, 2);
		sz = luaL_checkinteger(L, 3);
	}

    while ((fd >= 0) && (sz > 0)) {
    	int bytes = send(fd, buffer, sz, 0);
    	if (bytes < 0) {
    		switch (errno) {
    		case EAGAIN:
    		case EINTR:
    			continue;
    		}
    		return 0;
    	}
    	sz -= bytes;
    	buffer += bytes;
    	sleep(0);
    }

	if (type == LUA_TSTRING) {
		lua_settop(L, 2);
	} else {
		lua_pushlstring(L, buffer, sz);
	}
	return 1;
}

static int
_recv(lua_State *L) {
	int fd = -1;
	if (!lua_isnil(L, 1)) {
		fd = luaL_checkinteger(L, 1);
	}

    int length = MAX_BUFFER_SIZE;
    while (fd >= 0) {
        char buffer[length + 1];
        for (;;) {
        	int bytes = recv(fd, buffer, length, MSG_WAITALL);
        	if (bytes < 0) {
                if (errno == EAGAIN || errno == EINTR)
                    continue;
                printf("recv error\n");
                return 0;
        	}
        }
        buffer[length] = '\0';
        printf("%s\n", buffer);
    }
    return 0;
}

static int
_close(lua_State *L) {
    socket_t *s = get_sock_ptr(L);
    if (s->fd != -1) {
        close(s->fd);
        lua_pushboolean(L, 1);
        return 1;
    }

    return 0;
}

int
luaopen_socket_c(lua_State *L) {
	luaL_Reg l[] = {
		{ "socket", _socket },
		{ "bind", _bind },
		{ "recv", _recv },
		{ "send", _send },
		{ "listen", _listen },
		{ "accept", _accept },
		{ "close", _close },
		{ "connect", _connect },	
		{ NULL, NULL }
	};
	luaL_checkversion(L);
	luaL_newlib(L,l);
	return 1;
}

