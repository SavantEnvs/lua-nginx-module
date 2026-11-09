/* ngx.decode_args backend: parse a query string / urlencoded body into a Lua table. */
#include "fuzz_common.h"

#include <lua.h>
#include <lauxlib.h>

extern int ngx_http_lua_parse_args(lua_State *L, u_char *buf, u_char *last,
    int max);

/* [max][query...] */
int
LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    int         max;
    u_char     *buf;
    lua_State  *L;

    lnm_init();

    if (size < 1) {
        return -1;
    }

    max = data[0] < 200 ? data[0] : 0;
    size--;

    buf = lnm_dup(data + 1, size);
    L = luaL_newstate();
    lua_createtable(L, 0, 4);

    ngx_http_lua_parse_args(L, buf, buf + size, max);

    lua_close(L);
    free(buf);
    return 0;
}
