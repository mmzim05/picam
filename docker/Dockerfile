FROM ubuntu:22.04

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update -qq && \
    apt-get install -y --no-install-recommends \
        build-essential libncurses-dev bison flex libssl-dev \
        libelf-dev bc cpio rsync wget unzip git python3 \
        python3-pip python3-setuptools python3-wheel \
        dosfstools mtools ca-certificates && \
    rm -rf /var/lib/apt/lists/*

WORKDIR /build
