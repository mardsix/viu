FROM ubuntu:22.04

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt install -y software-properties-common

RUN add-apt-repository ppa:ubuntu-toolchain-r/test -y

RUN apt-get update && apt install -y \
    wget \
    software-properties-common \
    gnupg \
    libsystemd-dev \
    usbutils \
    cmake \
    autoconf \
    libtool \
    libssl-dev \
    libudev-dev \
    git

RUN wget https://apt.llvm.org/llvm.sh && chmod +x llvm.sh && ./llvm.sh 21
RUN update-alternatives --install /usr/bin/c++ c++ /usr/bin/clang++-21 1
RUN apt-get update && apt install -y libc++-21-dev clang-tidy-21 clang-format-21

WORKDIR /opt/viu
COPY update-alternatives-clang.py /opt/viu/

RUN chmod +x /opt/viu/update-alternatives-clang.py \
    && /opt/viu/update-alternatives-clang.py