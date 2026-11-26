# syntax=docker/dockerfile:1.7
# =============================================================================
# RS918 N96K-Pro - reproducible firmware build image  (N96K P2-2, audit §9.3)
#
# Builds fw-mchf (firmware) + bl-mchf (bootloader) from the sources in this
# repository with a *pinned* ARM toolchain, verifies the artifacts and runs the
# host-side table tests, so "docker build ." is a complete reproducibility check.
#
#   docker build -t rs918-n96k-build .
#   docker create --name n96k rs918-n96k-build && \
#     docker cp n96k:/src/build/. ./artifacts/ && docker rm n96k
#
# Toolchain: xPack GNU Arm Embedded GCC 15.2.1-1.1 (the version verified for
# v1.0).  The URL format and the SHA256 below were checked against the GitHub
# release assets (the .sha256 asset published by xPack matches the API digest);
# the archive layout was checked with an HTTP range request - it extracts to
# xpack-arm-none-eabi-gcc-15.2.1-1.1/.  A full "docker build" has NOT been run
# on the development host (Docker is not installed there) - see docs/CI.md.
# =============================================================================
ARG UBUNTU_VERSION=22.04

# ---------------------------------------------------------------------------
# stage 1: download + verify + unpack the pinned cross toolchain
# ---------------------------------------------------------------------------
FROM ubuntu:${UBUNTU_VERSION} AS toolchain

ARG DEBIAN_FRONTEND=noninteractive
ARG XPACK_VERSION=15.2.1-1.1
ARG XPACK_ARCHIVE=xpack-arm-none-eabi-gcc-${XPACK_VERSION}-linux-x64.tar.gz
# sha256 of the linux-x64 archive, from the release's .sha256 asset
ARG XPACK_SHA256=da6a49ad4003944b823c6c93702a8787c922ab34bd7e918ec0eaf6933a9b1ff6
ARG XPACK_URL=https://github.com/xpack-dev-tools/arm-none-eabi-gcc-xpack/releases/download/v${XPACK_VERSION}/${XPACK_ARCHIVE}

RUN apt-get update \
 && apt-get install -y --no-install-recommends ca-certificates curl gzip \
 && rm -rf /var/lib/apt/lists/*

RUN curl -fsSL --retry 3 -o "/tmp/${XPACK_ARCHIVE}" "${XPACK_URL}" \
 && echo "${XPACK_SHA256}  /tmp/${XPACK_ARCHIVE}" | sha256sum -c - \
 && mkdir -p /opt/xpack \
 && tar -xzf "/tmp/${XPACK_ARCHIVE}" -C /opt/xpack \
 && rm -f "/tmp/${XPACK_ARCHIVE}" \
 && /opt/xpack/xpack-arm-none-eabi-gcc-${XPACK_VERSION}/bin/arm-none-eabi-gcc --version

# ---------------------------------------------------------------------------
# stage 2: build the firmware and verify the artifacts
# ---------------------------------------------------------------------------
FROM ubuntu:${UBUNTU_VERSION} AS build

ARG DEBIAN_FRONTEND=noninteractive
ARG XPACK_VERSION=15.2.1-1.1

# tools/build.py looks for $ARM_GCC_BIN before its Windows default path
ENV ARM_GCC_BIN=/opt/xpack/xpack-arm-none-eabi-gcc-${XPACK_VERSION}/bin
ENV PATH=${ARM_GCC_BIN}:${PATH}

RUN apt-get update \
 && apt-get install -y --no-install-recommends python3 ca-certificates \
 && rm -rf /var/lib/apt/lists/*

COPY --from=toolchain /opt/xpack /opt/xpack

WORKDIR /src
# only the directories the build and the tests need are copied: the build
# context stays small and stale build/ or release/ artifacts cannot leak in
COPY mchf-eclipse/ mchf-eclipse/
COPY tools/ tools/
COPY tests/ tests/
COPY docker/ docker/

# 1) host-side table tests (fast, no toolchain needed but cheap to run here)
# 2) full firmware + bootloader build with the pinned toolchain
# 3) artifact/size verification (flash/CCM budget, expected fw size, SHA256SUMS)
RUN python3 tests/run_all.py \
 && python3 tools/build.py all \
 && python3 docker/verify_artifacts.py build

# Artifacts stay in the image at /src/build; the CI workflow extracts them
# with "docker cp" (see .github/workflows/build.yml and docs/CI.md).
