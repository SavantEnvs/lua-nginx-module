/* ngx.encode/decode_base64, ngx.escape/unescape_uri, ngx.http_time and friends. */
#include "fuzz_common.h"

extern size_t ngx_http_lua_ffi_encode_base64(const u_char *src, size_t slen,
    u_char *dst, int no_padding);
extern int ngx_http_lua_ffi_decode_base64(const u_char *src, size_t slen,
    u_char *dst, size_t *dlen);
extern int ngx_http_lua_ffi_decode_base64mime(const u_char *src, size_t slen,
    u_char *dst, size_t *dlen);
extern size_t ngx_http_lua_ffi_unescape_uri(const u_char *src, size_t len,
    u_char *dst);
extern size_t ngx_http_lua_ffi_uri_escaped_length(const u_char *src,
    size_t len, int type);
extern void ngx_http_lua_ffi_escape_uri(const u_char *src, size_t len,
    u_char *dst, int type);
extern void ngx_http_lua_ffi_parse_http_time(const u_char *str, size_t len,
    long *time);
extern void ngx_http_lua_ffi_http_time(u_char *buf, long t);
extern int ngx_http_lua_ffi_cookie_time(u_char *buf, long t);
extern void ngx_http_lua_ffi_str_replace_char(u_char *buf, size_t len,
    const u_char find, const u_char replace);
extern void ngx_http_lua_ffi_md5(const u_char *src, size_t len, u_char *dst);

/* [op][arg][payload...]; buffer sizes follow resty.core's own computations. */
int
LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    int      op, arg;
    size_t   n, dlen, r, i;
    u_char  *src, *dst, buf[32];
    long     t;

    lnm_init();

    if (size < 2) {
        return -1;
    }

    op = data[0] % 8;
    arg = data[1];
    n = size - 2;
    src = lnm_dup(data + 2, n);

    switch (op) {

    case 0:
        dlen = (arg & 1) ? (n * 8 + 5) / 6 : (n + 2) / 3 * 4;
        dst = malloc(dlen + 1);
        r = ngx_http_lua_ffi_encode_base64(src, n, dst, arg & 1);
        if (r > dlen) {
            abort();
        }

        if (!(arg & 1)) {
            u_char *back = malloc((r + 3) / 4 * 3 + 1);
            size_t  blen;

            if (!ngx_http_lua_ffi_decode_base64(dst, r, back, &blen)
                || blen != n || (n && memcmp(back, src, n) != 0))
            {
                abort();
            }

            free(back);
        }

        free(dst);
        break;

    case 1:
    case 2:
        dlen = (n + 3) / 4 * 3;
        dst = malloc(dlen + 1);
        if (op == 1) {
            ngx_http_lua_ffi_decode_base64(src, n, dst, &r);

        } else {
            ngx_http_lua_ffi_decode_base64mime(src, n, dst, &r);
        }

        free(dst);
        break;

    case 3:
        dst = malloc(n + 1);
        ngx_http_lua_ffi_unescape_uri(src, n, dst);
        free(dst);
        break;

    case 4:
        arg %= 7;
        dlen = ngx_http_lua_ffi_uri_escaped_length(src, n, arg);
        dst = malloc(dlen + 1);
        ngx_http_lua_ffi_escape_uri(src, n, dst, arg);
        free(dst);
        break;

    case 5:
        ngx_http_lua_ffi_parse_http_time(src, n, &t);
        break;

    case 6:
        t = 0;
        memcpy(&t, data + 2, n < sizeof(t) ? n : sizeof(t));
        if (t < 0 || t > 253402300799L) {
            t = (long) (uint32_t) t;
        }

        ngx_http_lua_ffi_http_time(buf, t);
        ngx_http_lua_ffi_cookie_time(buf, t);
        break;

    case 7:
        dst = malloc(n + 1);
        memcpy(dst, src, n);
        ngx_http_lua_ffi_str_replace_char(dst, n, (u_char) arg,
                                          (u_char) (arg + 1));
        free(dst);
        dst = malloc(33);
        ngx_http_lua_ffi_md5(src, n, dst);
        for (i = 0; i < 32; i++) {
            if (!((dst[i] >= '0' && dst[i] <= '9')
                  || (dst[i] >= 'a' && dst[i] <= 'f')))
            {
                abort();
            }
        }

        free(dst);
        break;
    }

    free(src);
    return 0;
}
