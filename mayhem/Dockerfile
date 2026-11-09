# syntax=docker/dockerfile:1.7
# Commit-image Dockerfile: builds on the org C/C++ base; adds pinned static dependencies, then the commit's build.
FROM ghcr.io/savantenvs/base:20261023

ARG COMMIT
ARG REPO_URL
ARG SOURCE_DATE_EPOCH
ARG SANITIZER_FLAGS
ARG COVERAGE_FLAGS=
ARG MAYHEM_JOBS=

USER root
RUN [ -n "$SOURCE_DATE_EPOCH" ] && repro-sources-list.sh || true ; \
    apt-get update && apt-get install -yq --no-install-recommends libpcre2-dev \
 && rm -rf /var/lib/apt/lists/*

# nginx (the host the module is compiled into): sources only; build.sh builds them out of tree.
ARG NGINX_VER=1.31.8
ARG NGINX_SHA256=0652b760b3b772d2ec17b47c2fc15d9a95e6fe35cd5b877eadc1a62d9bc75af1
RUN d=/opt/toolchains/deps/nginx-${NGINX_VER} && mkdir -p "$d/src" \
 && curl -fsSL -o /tmp/dep.tgz https://nginx.org/download/nginx-${NGINX_VER}.tar.gz \
 && echo "${NGINX_SHA256}  /tmp/dep.tgz" | sha256sum -c - \
 && tar -xzf /tmp/dep.tgz --strip-components=1 -C "$d/src" \
 && rm -f /tmp/dep.tgz && chmod -R a+rX "$d"

# OpenResty's LuaJIT fork (required by the module), static only.
ARG LUAJIT_REV=a9381a0912c2423a51fb1986d2d6f19ca9e8daed
ARG LUAJIT_SHA256=f9bb0d12dbf99d574b734331861fa9d85ff9cdc53bffd7d9d69a502d91625098
RUN d=/opt/toolchains/deps/luajit2-${LUAJIT_REV} && mkdir -p "$d/src" \
 && curl -fsSL -o /tmp/dep.tgz https://github.com/openresty/luajit2/archive/${LUAJIT_REV}.tar.gz \
 && echo "${LUAJIT_SHA256}  /tmp/dep.tgz" | sha256sum -c - \
 && tar -xzf /tmp/dep.tgz --strip-components=1 -C "$d/src" \
 && cd "$d/src" \
 && make -j"$(nproc)" BUILDMODE=static CC=clang CCDEBUG="-g -gdwarf-3" PREFIX="$d" \
 && make install BUILDMODE=static CC=clang PREFIX="$d" \
 && make clean && rm -f /tmp/dep.tgz && chmod -R a+rX "$d"

# Lua libraries the module needs at run time (used only by the real-nginx test); lua-resty-core is the last one before it moved on to ngx_http_lua_module 0.10.32, which BASE predates.
ARG RESTY_CORE_REV=603cfbec943d02dc4ba51c9def268abf7b64900b
ARG RESTY_CORE_SHA256=019cf0a5ed62bf83c8bb4bafc9c0508575b98abb74cf672088f2db215d13e717
RUN d=/opt/toolchains/deps/lua-resty-core-${RESTY_CORE_REV} && mkdir -p "$d/src" \
 && curl -fsSL -o /tmp/dep.tgz https://github.com/openresty/lua-resty-core/archive/${RESTY_CORE_REV}.tar.gz \
 && echo "${RESTY_CORE_SHA256}  /tmp/dep.tgz" | sha256sum -c - \
 && tar -xzf /tmp/dep.tgz --strip-components=1 -C "$d/src" && rm -f /tmp/dep.tgz && chmod -R a+rX "$d"
ARG RESTY_LRU_REV=ad373f457a091ffa85de984938f948bc1721d922
ARG RESTY_LRU_SHA256=6b64a742bdc60ca56280493bd9e5f66bca0efaf3ec571fb13ed14d366433acd7
RUN d=/opt/toolchains/deps/lua-resty-lrucache-${RESTY_LRU_REV} && mkdir -p "$d/src" \
 && curl -fsSL -o /tmp/dep.tgz https://github.com/openresty/lua-resty-lrucache/archive/${RESTY_LRU_REV}.tar.gz \
 && echo "${RESTY_LRU_SHA256}  /tmp/dep.tgz" | sha256sum -c - \
 && tar -xzf /tmp/dep.tgz --strip-components=1 -C "$d/src" && rm -f /tmp/dep.tgz && chmod -R a+rX "$d"

USER mayhem
WORKDIR /mayhem
COPY --chown=2000:2000 . /mayhem

RUN if [ -x /opt/mayhem/ccache/prefill ]; then /opt/mayhem/ccache/prefill bash mayhem/build.sh; \
    else bash mayhem/build.sh; fi

RUN if [ -f mayhem/test.sh ]; then bash mayhem/test.sh; else echo "no mayhem/test.sh"; fi

LABEL org.opencontainers.image.source="${REPO_URL}" \
      org.opencontainers.image.revision="${COMMIT}" \
      rlenv.fix="4e2fa113181cee25c9b4833284a8d97e69e8bcd5" \
      rlenv.tier="commit" \
      rlenv.source_date_epoch="${SOURCE_DATE_EPOCH}" \
      rlenv.user="mayhem"

USER mayhem
WORKDIR /mayhem
