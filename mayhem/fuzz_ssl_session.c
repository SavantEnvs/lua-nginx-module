/* ssl.set_serialized_session(data) backend: de-serialize a DER SSL_SESSION onto a request's TLS connection. */
#include "fuzz_common.h"

#include <openssl/ssl.h>
#include <openssl/err.h>
#include "ngx_http_lua_ssl.h"

extern int ngx_http_lua_ffi_ssl_set_serialized_session(ngx_http_request_t *r,
    const unsigned char *data, int len, char **err);

static SSL_CTX                 *lnm_ssl_ctx;
static ngx_http_request_t       lnm_req;
static ngx_connection_t         lnm_conn;
static ngx_ssl_connection_t     lnm_sslc;
static ngx_http_lua_ssl_ctx_t   lnm_cctx;

static void
lnm_ssl_init(void)
{
    static int  done;

    if (done) {
        return;
    }

    done = 1;
    lnm_init();

    if (ngx_http_lua_ssl_init(&lnm_log) != NGX_OK) {
        abort();
    }

    lnm_ssl_ctx = SSL_CTX_new(TLS_server_method());
    lnm_sslc.connection = SSL_new(lnm_ssl_ctx);
    SSL_set_ex_data(lnm_sslc.connection, ngx_http_lua_ssl_ctx_index, &lnm_cctx);

    lnm_conn.ssl = &lnm_sslc;
    lnm_conn.log = &lnm_log;
    lnm_req.connection = &lnm_conn;
}

/* The Lua string `data` handed to the FFI call, with its exact length. */
int
LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    char    *err;
    u_char  *buf;

    lnm_ssl_init();

    if (size > INT_MAX) {
        return -1;
    }

    buf = lnm_dup(data, size);
    err = NULL;

    ngx_http_lua_ffi_ssl_set_serialized_session(&lnm_req, buf, (int) size,
                                                &err);

    if (lnm_cctx.session != NULL) {
        SSL_SESSION_free(lnm_cctx.session);
        lnm_cctx.session = NULL;
    }

    ERR_clear_error();
    free(buf);
    return 0;
}
