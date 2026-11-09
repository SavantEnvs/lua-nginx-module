# Known findings (fuzz_regex)

## `ngx_http_lua_ffi_script_eval_len`: heap-buffer-overflow READ for DFA matches with `$N` templates

`dfa-template-overread.bin` (kept out of `testsuite/` on purpose: seeds must not crash).

In DFA mode (`ngx.re.gsub` with the `d` option) `ngx_http_lua_ffi_compile_regex` sizes `re->captures` for a
single pair (2 ints), but `pcre2_dfa_match` may return `rc > 1` (one per alternative match). resty.core then
calls `ngx_http_lua_ffi_init_script_engine(e, subj, compiled, rc)`, which sets `e->ncaptures = rc * 2`, so a
template such as `$1` makes `ngx_http_lua_script_copy_capture_len_code` (`src/ngx_http_lua_script.c:434`) read
`captures[2]`, past the 8-byte allocation. ASan reports it because the fuzz build defines
`NGX_DEBUG_PALLOC=1` (every pool allocation becomes its own `malloc`).

The match data is a process-wide object that only grows, and a DFA match returns more than one match only
when it is large, so the overread needs a worker that already ran a capture-heavy regex. The harness
therefore resets the module's regex state after every input (`ngx_http_lua_regex_cleanup`) and starts every
input from a fixed state: it first compiles and runs a regex with 8 capture groups (9-pair match data). The
result then depends only on the input, and the finding reproduces in a fresh process.

Reproduce: `/mayhem/fuzz_regex mayhem/fuzz_regex/known-findings/dfa-template-overread.bin`
(mode `d`+expand, pattern `(?:(a)|b)+`, template `$1$2$3`, subject `ab...`).
