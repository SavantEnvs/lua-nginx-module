#!/usr/bin/env bash
#
# Runs the nginx built by mayhem/build.sh (normal flags, lua-nginx-module compiled in) and checks known
# request -> response pairs through real HTTP, so a build whose Lua API no longer behaves fails here.
set -uo pipefail
[ -n "${SOURCE_DATE_EPOCH:-}" ] || unset SOURCE_DATE_EPOCH
: "${SRC:=/mayhem}"
cd "$SRC"

emit_ctrf() {
  local tool="$1" passed="$2" failed="$3" skipped="${4:-0}" pending="${5:-0}" other="${6:-0}"
  local tests=$(( passed + failed + skipped + pending + other ))
  cat > "${CTRF_REPORT:-$SRC/ctrf-report.json}" <<JSON
{
  "results": {
    "tool": { "name": "$tool" },
    "summary": {
      "tests": $tests,
      "passed": $passed,
      "failed": $failed,
      "pending": $pending,
      "skipped": $skipped,
      "other": $other
    }
  }
}
JSON
  printf 'CTRF {"results":{"tool":{"name":"%s"},"summary":{"tests":%d,"passed":%d,"failed":%d,"pending":%d,"skipped":%d,"other":%d}}}\n' \
    "$tool" "$tests" "$passed" "$failed" "$pending" "$skipped" "$other"
  [ "$failed" -eq 0 ]
}

NGINX=$SRC/build/oracle/sbin/nginx
DEPS=/opt/toolchains/deps
if [ ! -x "$NGINX" ]; then
  echo "missing $NGINX: mayhem/build.sh did not build it" >&2
  emit_ctrf "lua-nginx-module" 0 1
  exit 1
fi

TMP=$(mktemp -d /tmp/lnm-test.XXXXXX)
PORT=$(( 20000 + RANDOM % 20000 ))
mkdir -p "$TMP/body" "$TMP/proxy" "$TMP/fastcgi" "$TMP/uwsgi" "$TMP/scgi"
sed -e "s#@TMP@#$TMP#g" -e "s#@PORT@#$PORT#g" \
    -e "s#@CORE@#$(echo $DEPS/lua-resty-core-*/src)#" \
    -e "s#@LRU@#$(echo $DEPS/lua-resty-lrucache-*/src)#" \
    mayhem/test/nginx.conf > "$TMP/nginx.conf"

"$NGINX" -p "$TMP" -c "$TMP/nginx.conf" > "$TMP/stdout.log" 2>&1 &
NGX_PID=$!
trap 'kill "$NGX_PID" 2>/dev/null; wait "$NGX_PID" 2>/dev/null; rm -rf "$TMP"' EXIT

up=0
for _ in $(seq 1 50); do
  if curl -s -o /dev/null "http://127.0.0.1:$PORT/md5"; then up=1; break; fi
  kill -0 "$NGX_PID" 2>/dev/null || break
  sleep 0.2
done

passed=0; failed=0
check() {  # <path> <expected body>
  local got
  got=$(curl -s --max-time 10 "http://127.0.0.1:$PORT$1" || true)
  if [ -n "$got" ] && [ "$got" = "$2" ]; then
    passed=$((passed + 1))
  else
    failed=$((failed + 1))
    echo "FAIL $1: expected [$2] got [$got]" >&2
  fi
}

if [ "$up" = 1 ]; then
  check "/b64enc?s=hello" "aGVsbG8="
  check "/b64dec?s=aGVsbG8=" "hello"
  check "/unesc?s=a%20b%2Fc" "a b/c"
  check "/esc" "a%20b%26c%2Fd"
  check "/match" "123-xyz|123|xyz"
  check "/gsub" "hell[o] w[o]rld 2"
  check "/args" "1,2,hello world"
  check "/time" "Tue, 14 Nov 2023 22:13:20 GMT 1700000000"
  check "/md5" "5d41402abc4b2a76b9719d911017c592"
  check "/lexer" "}{ ]] }"
else
  echo "nginx did not start; see:" >&2
  tail -n 20 "$TMP/error.log" "$TMP/stdout.log" >&2
  failed=1
fi

emit_ctrf "lua-nginx-module" "$passed" "$failed"
