/* Tokenizer that finds the end of a *_by_lua_block { ... } body in nginx.conf. */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef unsigned char u_char;

extern int ngx_http_lua_lex(const u_char *const s, size_t len, int *const ovec);

int
LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    int      ovec[2], rc, steps;
    size_t   pos;
    u_char  *buf;

    if (size < 1) {
        return -1;
    }

    buf = malloc(size + 1);
    memcpy(buf, data, size);
    buf[size] = 0;

    pos = 0;
    for (steps = 0; steps < 4096 && pos < size; steps++) {
        ovec[0] = ovec[1] = -1;
        rc = ngx_http_lua_lex(buf + pos, size - pos, ovec);
        if (rc < 0 || ovec[1] <= 0 || (size_t) ovec[1] > size - pos) {
            break;
        }

        pos += ovec[1];
    }

    free(buf);
    return 0;
}
