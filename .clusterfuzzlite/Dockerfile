FROM gcr.io/oss-fuzz-base/base-builder:v1
RUN apt-get update && \
   apt-get install -y make autoconf automake libtool build-essential pkg-config bison flex patchelf \
    python3-pip ninja-build && \
   pip3 install meson==0.61.3

RUN git clone --depth 1 https://gitlab.xiph.org/xiph/vorbis.git vorbis
RUN git clone --depth 1 https://gitlab.xiph.org/xiph/ogg.git ogg
RUN git clone --depth 1 https://gitlab.xiph.org/xiph/theora.git theora

# Copy repository
COPY . $SRC/gstreamer

WORKDIR gstreamer
COPY .clusterfuzzlite/build.sh $SRC/
