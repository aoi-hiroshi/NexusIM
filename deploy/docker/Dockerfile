ARG UBUNTU_VERSION=24.04

FROM ubuntu:${UBUNTU_VERSION} AS builder
ARG DEBIAN_FRONTEND=noninteractive
ARG MUDUO_COMMIT=b55b0ac6a10d4ded1301c3172e779ec46ada2ab3

RUN apt-get update && apt-get install -y --no-install-recommends \
        build-essential ca-certificates cmake git pkg-config \
        libboost-all-dev libcurl4-openssl-dev libhiredis-dev \
        default-libmysqlclient-dev libprotobuf-dev protobuf-compiler \
        libssl-dev libzookeeper-mt-dev && \
    rm -rf /var/lib/apt/lists/*

ARG BUILD_JOBS=2
RUN apt-get update && apt-get install -y --no-install-recommends curl && \
    rm -rf /var/lib/apt/lists/*

RUN curl -fsSL --retry 3 \
        "https://codeload.github.com/chenshuo/muduo/tar.gz/${MUDUO_COMMIT}" \
        -o /tmp/muduo.tar.gz && \
    echo "49466c5b09ac70a9813fd7c103fed8d818fcef43b92c6e4df82749e236f88809  /tmp/muduo.tar.gz" \
        | sha256sum -c - && \
    mkdir -p /tmp/muduo && \
    tar -xzf /tmp/muduo.tar.gz -C /tmp/muduo --strip-components=1 && \
    cd /tmp/muduo && \
    cmake -S . -B build \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_INSTALL_PREFIX=/usr/local \
        -DMUDUO_BUILD_EXAMPLES=OFF && \
    cmake --build build --parallel "${BUILD_JOBS}" && \
    cmake --install build && \
    ldconfig

WORKDIR /src
COPY . .

RUN cmake -S . -B /tmp/nexusim-build \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_INSTALL_PREFIX=/opt/nexusim/stage \
        -DNEXUSIM_BUILD_TESTS=OFF && \
    cmake --build /tmp/nexusim-build --parallel "${BUILD_JOBS}" \
        --target ChatServer UserService AIService && \
    cmake --install /tmp/nexusim-build

# Copy the exact shared-library closure of the three executables. The final
# image uses the same Ubuntu release, so this avoids shipping compilers and
# development headers while keeping the runtime reproducible.
RUN set -eux; \
    mkdir -p /opt/nexusim/runtime-libs /opt/nexusim/licenses; \
    cp /tmp/muduo/License /opt/nexusim/licenses/Muduo-LICENSE; \
    for binary in \
        /opt/nexusim/stage/bin/ChatServer \
        /opt/nexusim/stage/bin/UserService \
        /opt/nexusim/stage/bin/AIService; do \
      ldd "${binary}"; \
    done | awk '/=> \/[^ ]+/ {print $3} /^\// {print $1}' \
      | while IFS= read -r library; do realpath -e "${library}"; done \
      | sort -u \
      | xargs -r -I '{}' cp -L --parents '{}' /opt/nexusim/runtime-libs

FROM ubuntu:${UBUNTU_VERSION} AS runtime
ARG DEBIAN_FRONTEND=noninteractive
ARG BUILD_REVISION=unknown
LABEL org.opencontainers.image.title="NexusIM backend" \
      org.opencontainers.image.description="ChatServer, UserService and AIService runtime" \
      org.opencontainers.image.revision="${BUILD_REVISION}"

RUN apt-get update && apt-get install -y --no-install-recommends \
        ca-certificates netcat-openbsd && \
    rm -rf /var/lib/apt/lists/* && \
    groupadd --system --gid 10001 nexusim && \
    useradd --system --uid 10001 --gid nexusim --home-dir /nonexistent \
        --shell /usr/sbin/nologin nexusim

COPY --from=builder /opt/nexusim/runtime-libs/ /
COPY --from=builder /opt/nexusim/stage/ /opt/nexusim/
COPY --from=builder /opt/nexusim/licenses/ /opt/nexusim/licenses/
COPY deploy/docker/backend/healthcheck.sh /usr/local/bin/nexusim-healthcheck

RUN chmod 0555 /usr/local/bin/nexusim-healthcheck /opt/nexusim/bin/* && ldconfig

USER nexusim:nexusim
WORKDIR /opt/nexusim
