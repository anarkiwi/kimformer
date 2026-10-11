FROM debian:trixie-20260918-slim AS builder
ARG INFORM6_VERSION=6.43
ARG FROTZ_VERSION=2.55
RUN apt-get update && apt-get install -y --no-install-recommends \
        build-essential ca-certificates curl pkg-config \
    && rm -rf /var/lib/apt/lists/*
WORKDIR /src
RUN curl -fsSL https://github.com/DavidKinder/Inform6/archive/refs/tags/v${INFORM6_VERSION}.tar.gz | tar xz \
    && cc -O2 -s -o /usr/local/bin/inform6 Inform6-${INFORM6_VERSION}/*.c
RUN curl -fsSL https://gitlab.com/DavidGriffith/frotz/-/archive/${FROTZ_VERSION}/frotz-${FROTZ_VERSION}.tar.gz | tar xz \
    && make -C frotz-${FROTZ_VERSION} -j"$(nproc)" dumb \
    && install -s frotz-${FROTZ_VERSION}/dfrotz /usr/local/bin/dfrotz

FROM debian:trixie-20260918-slim
COPY --from=builder /usr/local/bin/inform6 /usr/local/bin/dfrotz /usr/local/bin/
