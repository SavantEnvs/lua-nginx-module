/* ngx.re.* backend: compile a pattern, match it, and expand a replacement template. */
#include "fuzz_common.h"
#include "ngx_http_lua_script.h"

#define PCRE2_CODE_UNIT_WIDTH 8
#include <pcre2.h>

/* Mirrors the layout the module's own Lua FFI declares for ngx_http_lua_regex_t. */
typedef struct {
    ngx_pool_t   *pool;
    u_char       *name_table;
    int           name_count;
    int           name_entry_size;
    int           ncaptures;
    int          *captures;
    pcre2_code   *regex;
    void         *regex_sd;
    ngx_http_lua_complex_value_t *replace;
    const u_char *pattern;
} lnm_regex_t;

extern lnm_regex_t *ngx_http_lua_ffi_compile_regex(const unsigned char *pat,
    size_t pat_len, int flags, int pcre_opts, u_char *errstr,
    size_t errstr_size);
extern int ngx_http_lua_ffi_exec_regex(lnm_regex_t *re, int flags,
    const u_char *s, size_t len, int pos);
extern void ngx_http_lua_ffi_destroy_regex(lnm_regex_t *re);
extern int ngx_http_lua_ffi_compile_replace_template(lnm_regex_t *re,
    const u_char *replace_data, size_t replace_len);
extern ngx_http_lua_script_engine_t *ngx_http_lua_ffi_create_script_engine(void);
extern void ngx_http_lua_ffi_init_script_engine(ngx_http_lua_script_engine_t *e,
    const unsigned char *subj, lnm_regex_t *compiled, int count);
extern void ngx_http_lua_ffi_destroy_script_engine(ngx_http_lua_script_engine_t *e);
extern size_t ngx_http_lua_ffi_script_eval_len(ngx_http_lua_script_engine_t *e,
    ngx_http_lua_complex_value_t *val);
extern void ngx_http_lua_ffi_script_eval_data(ngx_http_lua_script_engine_t *e,
    ngx_http_lua_complex_value_t *val, u_char *dst);

static const uint32_t pcre_opt_table[8] = {
    PCRE2_CASELESS, PCRE2_MULTILINE, PCRE2_DOTALL, PCRE2_EXTENDED,
    PCRE2_UTF, PCRE2_UCP, PCRE2_ANCHORED, PCRE2_UNGREEDY,
};

extern void ngx_http_lua_regex_cleanup(void *data);

/*
 * The module keeps one process-wide pcre2 match data that only grows, and a DFA match returns several
 * matches only when it is large. Every input therefore starts from the same state: a worker that already
 * ran a regex with 8 capture groups (match data of 9 pairs).
 */
static void
lnm_regex_presize(void)
{
    u_char        err[128];
    lnm_regex_t  *re;

    re = ngx_http_lua_ffi_compile_regex((const u_char *) "()()()()()()()()",
                                        16, 0, 0, err, sizeof(err));
    if (re == NULL) {
        abort();
    }

    ngx_http_lua_ffi_exec_regex(re, 0, (const u_char *) "", 0, 0);
    ngx_http_lua_ffi_destroy_regex(re);
}

/*
 * [mode][opts][patlen][tpllen][pattern][template][subject...]
 *   mode: bit0 DFA, bit1 JIT, bit2 no-utf8-check, bit3 expand template
 *   opts: one bit per entry of pcre_opt_table
 */
static int
lnm_regex_one(const uint8_t *data, size_t size)
{
    int           flags, opts, rc, i, pos;
    size_t        patlen, tpllen, sublen, dlen;
    u_char        err[128], *pat, *tpl, *sub, *dst;
    lnm_regex_t  *re;

    if (size < 4) {
        return -1;
    }

    patlen = data[2];
    tpllen = data[3];

    if (size < 4 + patlen + tpllen) {
        return -1;
    }

    flags = ((data[0] & 1) ? 1 << 1 : 0) | ((data[0] & 2) ? 1 << 2 : 0)
            | ((data[0] & 4) ? 1 << 4 : 0);

    opts = 0;
    for (i = 0; i < 8; i++) {
        if (data[1] & (1 << i)) {
            opts |= pcre_opt_table[i];
        }
    }

    /* "U" promises the subject is valid UTF-8; PCRE2 is undefined on garbage then, so only honor it without /u. */
    if (opts & PCRE2_UTF) {
        flags &= ~(1 << 4);
    }

    pat = lnm_dup(data + 4, patlen);
    tpl = lnm_dup(data + 4 + patlen, tpllen);
    sublen = size - 4 - patlen - tpllen;
    sub = lnm_dup(data + 4 + patlen + tpllen, sublen);

    re = ngx_http_lua_ffi_compile_regex(pat, patlen, flags, opts, err,
                                        sizeof(err));
    if (re == NULL) {
        goto done;
    }

    if ((data[0] & 8) && ngx_http_lua_ffi_compile_replace_template(re, tpl,
                                                                   tpllen)
                         != NGX_OK)
    {
        ngx_http_lua_ffi_destroy_regex(re);
        goto done;
    }

    pos = 0;
    for (i = 0; i < 4 && (size_t) pos <= sublen; i++) {
        rc = ngx_http_lua_ffi_exec_regex(re, flags, sub, sublen, pos);
        if (rc < 0) {
            break;
        }

        if (rc == 0) {
            if (!(flags & (1 << 1))) {
                break;
            }

            rc = 1;
        }

        /* like resty.core, only templates with $-references go through the script engine */
        if ((data[0] & 8) && re->replace && re->replace->lengths) {
            ngx_http_lua_script_engine_t *e;

            e = ngx_http_lua_ffi_create_script_engine();
            ngx_http_lua_ffi_init_script_engine(e, sub, re, rc);
            dlen = ngx_http_lua_ffi_script_eval_len(e, re->replace);
            dst = malloc(dlen + 1);
            ngx_http_lua_ffi_script_eval_data(e, re->replace, dst);
            free(dst);
            ngx_http_lua_ffi_destroy_script_engine(e);
        }

        if (re->captures[1] > re->captures[0]) {
            pos = re->captures[1];

        } else {
            pos = re->captures[1] + 1;
        }
    }

    ngx_http_lua_ffi_destroy_regex(re);

done:
    free(pat);
    free(tpl);
    free(sub);
    return 0;
}

int
LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    int  rc;

    lnm_init();
    lnm_regex_presize();

    rc = lnm_regex_one(data, size);

    /* frees the global match data, the compile/match contexts and the JIT stack: nothing outlives the input */
    ngx_http_lua_regex_cleanup(lnm_http_ctx.main_conf[0]);
    return rc;
}
