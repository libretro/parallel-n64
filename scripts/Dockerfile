FROM ubuntu:xenial

ENV DEBIAN_FRONTEND="noninteractive"

ARG uid
ARG branch=master
ENV branch=$branch

ARG NUMPROC

ENV PACKAGES \
    build-essential \
    bash \
    bc \
    bzip2 \
    diffutils \
    ccache \
    curl \
    doxygen \
    fuse \
    gcc \
    g++ \
    g++-multilib \
    gettext \
    git \
    gnupg \
    gnupg2 \
    gawk \
    make \
    nsis \
    texinfo \
    autoconf \
    automake \
    autopoint \
    bison \
    bzip2 \
    gzip \
    flex \
    gettext \
    gperf \
    intltool \
    less \
    libasound2 \
    libass-dev \
    libbz2-dev \
    libc6-dev-i386 \
    libffi-dev \
    libgdbm-dev \
    libgdk-pixbuf2.0-dev \
    libglib2.0-dev \
    libglu1-mesa-dev \
    libltdl-dev \
    libpcap-dev \
    libssl-dev \
    libreadline-dev \
    libtool \
    libtool-bin \
    libxml-parser-perl \
    libjson-perl \
    libncurses5-dev \
    libssl-dev \
    libx11-dev \
    lzip \
    lzop \
    mesa-common-dev \
    meson \
    p7zip-full \
    patch \
    patchutils \
    perl \
    pkg-config \
    ruby \
    sed \
    sudo \
    swig \
    tar \
    texinfo \
    unzip \
    u-boot-tools \
    uuid-dev \
    wget \
    nasm \
    bsdmainutils \
    xfonts-utils \
    xsltproc \
    xz-utils \
    libwxgtk3.0-dev \
    libgtk2.0-dev \
    libgtk-3-dev \
    liblzma-dev \
    libaio-dev

RUN set -eux; \
    apt-get -y update; \
    apt-get -y install -y unzip; \
    useradd -d /developer -m developer; \
    chown -R developer:developer /developer

RUN set -eux; \
    apt-get update -y; \
    apt-get -y install $PACKAGES; \
    apt-get -y autoremove; \
    apt-get -y clean; \
    rm -rf /var/lib/apt/lists/*
    
RUN set -eux; \
    apt-get update -y; \
    apt-get -y install software-properties-common;

RUN set -eux; \
    apt-get upgrade -y; \
    apt-get dist-upgrade -y; \
    add-apt-repository -y --update ppa:ubuntu-toolchain-r/test; \
    apt-get update -y; \
    apt-get -y install gcc-9 g++-9; \
    rm -rf /var/lib/apt/lists/*

RUN set -eux; \
    echo "1" | update-alternatives --install /usr/bin/gcc gcc /usr/bin/gcc-9 60 --slave /usr/bin/g++ g++ /usr/bin/g++-9; \
    echo "1" | update-alternatives --config gcc

RUN set -eux; \
    apt-get update -y; \
    apt-get -y install apt-transport-https ca-certificates; \
    wget -O - https://apt.kitware.com/keys/kitware-archive-latest.asc 2>/dev/null | gpg --dearmor - | sudo tee /etc/apt/trusted.gpg.d/kitware.gpg >/dev/null; \
    apt-add-repository -y 'deb https://apt.kitware.com/ubuntu/ xenial main'; \
    apt-get update -y; \
    apt-get -y install kitware-archive-keyring; \
    rm /etc/apt/trusted.gpg.d/kitware.gpg; \
    apt-get -y install cmake; \
    rm -rf /var/lib/apt/lists/*

RUN set -eux; \
    cd /tmp; \
    wget --no-check-certificate -nv "https://www.openssl.org/source/openssl-1.1.1o.tar.gz" -O- | tar xz; \
    cd openssl-*; \
    CC=gcc-9 CXX=g++-9 ./config --prefix=/usr/local; \
    make -j$(nproc); \
    make install -j$(nproc); \
    cd ..; \
    rm -rf *; \
    rm -rf /usr/local/share/doc/*; \
    rm -rf /usr/local/share/man/*; \
    ldconfig

RUN set -eux; \
    cd /tmp; \
    wget -nv "https://www.python.org/ftp/python/3.10.4/Python-3.10.4.tar.xz" -O- | tar xJ; \
    mkdir build; \
    cd build; \
    CC=gcc-9 CXX=g++-9 ../Python-*/configure \
        --prefix=/usr/local \
        --enable-optimizations \
    ; \
    make -j$(nproc); \
    make altinstall; \
    ln -sf /usr/local/bin/python3.10 /usr/local/bin/python3; \
    cd ..; \
    rm -rf *; \
    rm -rf /usr/local/share/doc/*; \
    rm -rf /usr/local/share/man/*; \
    ldconfig

RUN set -eux; \
    python3 -m pip install --upgrade --no-cache-dir pip; \
    python3 -m pip install --no-cache-dir \
        mako \
        ninja \
        pyyaml \
        setuptools \
        sphinx \
    ;

RUN echo "developer:developer" | chpasswd && adduser developer sudo

ENV HOME=/developer

USER root
WORKDIR /developer
VOLUME /developer

CMD /bin/bash
