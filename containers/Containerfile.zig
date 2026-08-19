# Zig verification image. Zig's bundled clang/libc++ has no native C++26 simd /
# linalg / execution / submdspan, so this exercises the backport inject path and
# (optionally) the cross-architecture matrix the project already supports.
#
#   podman build -t forge-zig --build-arg ZIG_VERSION=0.14.0 -f containers/Containerfile.zig .
#
# NOTE: adjust ZIG_VERSION / the tarball name to a release that exists; newer zig
# releases use the "zig-x86_64-linux-<ver>" naming instead of the older
# "zig-linux-x86_64-<ver>". When overriding ZIG_VERSION, also override
# ZIG_SHA256 with the official checksum from https://ziglang.org/download/
# (the build fails on mismatch by design).
FROM docker.io/library/debian:trixie

ARG ZIG_VERSION=0.14.0
ARG ZIG_TARBALL=zig-linux-x86_64-${ZIG_VERSION}.tar.xz
ARG ZIG_SHA256=473ec26806133cf4d1918caf1a410f8403a13d979726a9045b421b685031a982

RUN apt-get update \
    && apt-get install -y --no-install-recommends curl xz-utils cmake ninja-build ca-certificates binutils \
    && rm -rf /var/lib/apt/lists/* \
    && curl -fsSLo "/tmp/${ZIG_TARBALL}" "https://ziglang.org/download/${ZIG_VERSION}/${ZIG_TARBALL}" \
    && echo "${ZIG_SHA256}  /tmp/${ZIG_TARBALL}" | sha256sum -c - \
    && tar -xJf "/tmp/${ZIG_TARBALL}" -C /opt \
    && rm "/tmp/${ZIG_TARBALL}" \
    && ln -s "/opt/${ZIG_TARBALL%.tar.xz}/zig" /usr/local/bin/zig

# CMake invokes the compiler as a single executable, so wrap "zig c++".
RUN printf '#!/bin/sh\nexec zig cc "$@"\n'  > /usr/local/bin/zig-cc  && chmod +x /usr/local/bin/zig-cc \
 && printf '#!/bin/sh\nexec zig c++ "$@"\n' > /usr/local/bin/zig-c++ && chmod +x /usr/local/bin/zig-c++

ENV CC=zig-cc
ENV CXX=zig-c++

WORKDIR /src
