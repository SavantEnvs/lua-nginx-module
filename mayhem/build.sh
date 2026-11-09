#!/usr/bin/env bash
# Builds nginx + lua-nginx-module twice out of tree (sanitized, linked into in-process fuzz harnesses; and a
# normal build used by mayhem/test.sh). Everything it needs is in /opt/toolchains/deps, so it runs offline.
set -euo pipefail

[ -n "${SOURCE_DATE_EPOCH:-}" ] || unset SOURCE_DATE_EPOCH

: "${SANITIZER_FLAGS=-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer}"
: "${DEBUG_FLAGS:=-g -gdwarf-3}"
: "${CC:=clang}" ; : "${CXX:=clang++}" ; : "${LIB_FUZZING_ENGINE:=-fsanitize=fuzzer}"
: "${MAYHEM_JOBS:=$(nproc)}"
: "${COVERAGE_FLAGS=}"
: "${SRC:=/mayhem}"
export SANITIZER_FLAGS DEBUG_FLAGS CC CXX LIB_FUZZING_ENGINE MAYHEM_JOBS COVERAGE_FLAGS

cd "$SRC"

DEPS=/opt/toolchains/deps
NGINX_SRC=$DEPS/nginx-1.31.8/src
LUAJIT=$DEPS/luajit2-a9381a0912c2423a51fb1986d2d6f19ca9e8daed
export LUAJIT_LIB=$LUAJIT/lib LUAJIT_INC=$LUAJIT/include/luajit-2.1

# The compile line below must stay in sync with how nginx lays out its include dirs.
rm -rf build
mkdir -p build

# 1) Sanitized build: nginx + the module, with SanCov on every object.
cp -r "$NGINX_SRC" build/fuzz
(
  cd build/fuzz
  ./configure --with-cc="$CC" \
      --with-cc-opt="$SANITIZER_FLAGS $DEBUG_FLAGS -O1 -DNGX_DEBUG_PALLOC=1 -fno-sanitize=shift-base -fsanitize=fuzzer-no-link" \
      --with-ld-opt="-Wl,-E $SANITIZER_FLAGS" \
      --add-module="$SRC" > configure.log 2>&1 || { tail -40 configure.log; exit 1; }
  make -j"$MAYHEM_JOBS" > make.log 2>&1 || { tail -60 make.log; exit 1; }
)

# nginx.o carries main(); rename it so the libFuzzer driver provides the entry point.
objcopy --redefine-sym main=lnm_nginx_main build/fuzz/objs/src/core/nginx.o

OBJS=$(find build/fuzz/objs -name '*.o' | sort)
INCS="-Ibuild/fuzz/src/core -Ibuild/fuzz/src/event -Ibuild/fuzz/src/event/modules -Ibuild/fuzz/src/os/unix \
 -Ibuild/fuzz/objs -Ibuild/fuzz/src/http -Ibuild/fuzz/src/http/modules -I$LUAJIT_INC -Isrc -Isrc/api"
LIBS="$LUAJIT/lib/libluajit-5.1.a -lpcre2-8 -lz -lcrypt -ldl -lm -lpthread"

$CXX $SANITIZER_FLAGS $DEBUG_FLAGS -c mayhem/lsan_off.cc -o build/lsan_off.o
$CC $SANITIZER_FLAGS $DEBUG_FLAGS -c "$STANDALONE_FUZZ_MAIN" -o build/standalone_main.o

for h in fuzz_regex fuzz_string fuzz_args fuzz_lex; do
  # shellcheck disable=SC2086
  $CC $SANITIZER_FLAGS $DEBUG_FLAGS -O1 -DNDK_SET_VAR $INCS -c "mayhem/$h.c" -o "build/$h.o"
  # shellcheck disable=SC2086
  $CC $SANITIZER_FLAGS $DEBUG_FLAGS $LIB_FUZZING_ENGINE "build/$h.o" build/lsan_off.o $OBJS $LIBS \
      -o "/mayhem/$h"
  # shellcheck disable=SC2086
  $CC $SANITIZER_FLAGS $DEBUG_FLAGS build/standalone_main.o "build/$h.o" build/lsan_off.o $OBJS $LIBS \
      -o "/mayhem/$h-standalone"
done

# 2) Test build: the same server with the project's normal flags (no sanitizers), installed to build/oracle.
cp -r "$NGINX_SRC" build/oracle-src
(
  cd build/oracle-src
  ./configure --prefix="$SRC/build/oracle" --with-cc="$CC" \
      --with-cc-opt="-O2 $COVERAGE_FLAGS" --with-ld-opt="-Wl,-E $COVERAGE_FLAGS" \
      --add-module="$SRC" > configure.log 2>&1 || { tail -40 configure.log; exit 1; }
  make -j"$MAYHEM_JOBS" > make.log 2>&1 || { tail -60 make.log; exit 1; }
  make install > install.log 2>&1 || { tail -40 install.log; exit 1; }
)
