FROM ghcr.io/toltec-dev/base:v3.1

# GTK version build argument (default to 2)
ARG NETSURF_GTK_MAJOR=2

# Install common dependencies
RUN apt-get update -y && apt-get install -y \
    automake \
    bison \
    build-essential \
    flex \
    gperf \
    git \
    libcurl3-dev \
    libevdev-dev \
    libexpat-dev \
    libhtml-parser-perl \
    libjpeg-dev \
    libpng-dev \
    libssl-dev \
    libtool \
    pkg-config

# Install GTK packages based on NETSURF_GTK_MAJOR
RUN apt-get update -y && apt-get install -y \
    librsvg2-dev \
    $([ "${NETSURF_GTK_MAJOR}" = "3" ] && echo "libgtk-3-dev" || echo "libgtk2.0-dev")

# Build libiconv 1.16
RUN echo "Building libiconv 1.16..." \
    && export DEBIAN_FRONTEND=noninteractive \
    && mkdir libiconv \
    && cd libiconv \
    && curl -L "https://ftp.gnu.org/pub/gnu/libiconv/libiconv-1.16.tar.gz" -o libiconv.tar.gz \
    && echo "e6a1b1b589654277ee790cce3734f07876ac4ccfaecbee8afa0b649cf529cc04  libiconv.tar.gz" > sha256sums \
    && tar --strip-components=1 -xf libiconv.tar.gz \
    && rm libiconv.tar.gz sha256sums \
    && ./configure --prefix=$SYSROOT/usr --host="$CHOST" --enable-static --disable-shared \
    && make \
    && make install \
    && cd .. \
    && rm -rf libiconv

# Build openssl 1.1.1k
RUN echo "Building openssl 1.1.1k..." \
    && export DEBIAN_FRONTEND=noninteractive \
    && mkdir openssl \
    && cd openssl \
    && curl -L https://www.openssl.org/source/openssl-1.1.1k.tar.gz -o openssl.tar.gz \
    && echo "892a0875b9872acd04a9fde79b1f943075d5ea162415de3047c327df33fbaee5  openssl.tar.gz" > sha256sums \
    && tar --strip-components=1 -xf openssl.tar.gz \
    && rm openssl.tar.gz sha256sums \
    && ./Configure no-shared no-comp --prefix=$SYSROOT/usr --openssldir=$SYSROOT/usr --cross-compile-prefix=$CHOST- linux-armv4 \
    && make \
    && DESTDIR="$SYSROOT" make install \
    && cd .. \
    && rm -rf openssl

# Build curl 7.75.0
RUN echo "Building curl 7.75.0..." \
    && export DEBIAN_FRONTEND=noninteractive \
    && mkdir curl \
    && cd curl \
    && curl -L https://curl.se/download/curl-7.75.0.tar.gz -o curl.tar.gz \
    && echo "4d51346fe621624c3e4b9f86a8fd6f122a143820e17889f59c18f245d2d8e7a6  curl.tar.gz" > sha256sums \
    && tar --strip-components=1 -xf curl.tar.gz \
    && rm curl.tar.gz sha256sums \
    && ./configure --prefix=/usr --host="$CHOST" --enable-static --disable-shared --with-openssl --with-ca-bundle=/etc/ssl/certs/ca-certificates.crt \
    && make \
    && DESTDIR="$SYSROOT" make install \
    && cd .. \
    && rm -rf curl

# Build FreeType 2.10.4
RUN echo "Building FreeType 2.10.4" \
    && export DEBIAN_FRONTEND=noninteractive \
    && mkdir freetype \
    && cd freetype \
    && curl -L "https://gitlab.freedesktop.org/freetype/freetype/-/archive/VER-2-10-4/freetype-VER-2-10-4.tar.gz" -o freetype.tar.gz \
    && echo "4d47fca95debf8eebde5d27e93181f05b4758701ab5ce3e7b3c54b937e8f0962  freetype.tar.gz" > sha256sums \
    && tar --strip-components=1 -xf freetype.tar.gz \
    && rm freetype.tar.gz sha256sums \
    && bash autogen.sh \
    && ./configure --without-zlib --without-png --enable-static=yes --enable-shared=no --without-bzip2 --host=arm-linux-gnueabihf --host="$CHOST" --disable-freetype-config \
    && make \
    && DESTDIR="$SYSROOT" make install \
    && cd .. \
    && rm -rf freetype

# Build libjpeg-turbo 2.0.90
RUN echo "Building libjpeg-turbo 2.0.90..." \
    && export DEBIAN_FRONTEND=noninteractive \
    && mkdir libjpeg-turbo \
    && cd libjpeg-turbo \
    && curl -L "https://codeload.github.com/libjpeg-turbo/libjpeg-turbo/tar.gz/refs/tags/2.0.90" -o libjpeg-turbo.tar.gz \
    && echo "6a965adb02ad898b2ae48214244618fe342baea79db97157fdc70d8844ac6f09  libjpeg-turbo.tar.gz" > sha256sums \
    && tar --strip-components=1 -xf libjpeg-turbo.tar.gz \
    && rm libjpeg-turbo.tar.gz sha256sums \
    && cmake -DCMAKE_SYSROOT="$SYSROOT" -DCMAKE_TOOLCHAIN_FILE=/usr/share/cmake/$CHOST.cmake -DCMAKE_INSTALL_LIBDIR=$SYSROOT/lib -DCMAKE_INSTALL_INCLUDEDIR=$SYSROOT/usr/include -DENABLE_SHARED=FALSE \
    && make \
    && make install \
    && cd .. \
    && rm -rf libjpeg-turbo

# Build and host architecture settings
ENV HOST="arm-remarkable-linux-gnueabihf"
ENV BUILD="x86_64-linux-gnu"
ENV MAKE="make"

# Set up USE_CPUS for parallel builds at build time
# Can be overridden with --build-arg USE_CPUS=-jN
ARG USE_CPUS
RUN if [ -z "$USE_CPUS" ]; then \
    NCPUS=$(getconf _NPROCESSORS_ONLN 2>/dev/null || getconf NPROCESSORS_ONLN 2>/dev/null || echo 1); \
    NCPUS=$((NCPUS * 2)); \
    echo "-j${NCPUS}"; \
    else \
    echo "$USE_CPUS"; \
    fi > /tmp/use_cpus.txt && cat /tmp/use_cpus.txt

ENV TARGET_WORKSPACE="/opt/netsurf/build"
ENV PREFIX="${TARGET_WORKSPACE}/inst-${HOST}"
ENV BUILD_PREFIX="${TARGET_WORKSPACE}/inst-${BUILD}"
ENV PKG_CONFIG_PATH="${PREFIX}/lib/pkgconfig:/opt/x-tools/arm-remarkable-linux-gnueabihf/arm-remarkable-linux-gnueabihf/sysroot/usr/lib/pkgconfig"
ENV PKG_CONFIG_LIBDIR="${PREFIX}/lib/pkgconfig:${SYSROOT}/usr/local/lib/pkgconfig"
ENV PKG_CONFIG_SYSROOT_DIR="${PREFIX}"
ENV LD_LIBRARY_PATH="${PREFIX}/lib"
ENV PATH="${BUILD_PREFIX}/bin:${PATH}"
ENV CFLAGS="-I${PREFIX}/include"
ENV LDFLAGS="-L${PREFIX}/lib"

RUN mkdir -p ${TARGET_WORKSPACE} ${PREFIX} ${BUILD_PREFIX}
WORKDIR ${TARGET_WORKSPACE}

# Build arguments for custom repository locations and versions
ARG BUILDSYSTEM_REPOSITORY="git://git.netsurf-browser.org/buildsystem.git"
ARG BUILDSYSTEM_VERSION="1fbac2b96208708bb6447a01f793248bc72e9ada"
RUN git clone ${BUILDSYSTEM_REPOSITORY} buildsystem && \
    (cd buildsystem && [ -n "${BUILDSYSTEM_VERSION}" ] && git checkout ${BUILDSYSTEM_VERSION} || true)
# Build tools (for BUILD architecture) - equivalent to ns-make-tools install
# Unset CHOST to prevent cross-compilation when building native tools
RUN unset CHOST && make -C ${TARGET_WORKSPACE}/buildsystem PREFIX=${BUILD_PREFIX} HOST=${BUILD} $(cat /tmp/use_cpus.txt) install
# Build buildsystem for HOST architecture (required by all libraries)
RUN make -C ${TARGET_WORKSPACE}/buildsystem PREFIX=${PREFIX} HOST=${HOST} $(cat /tmp/use_cpus.txt) install

# tools required to build the browser
ARG NSGENBIND_REPOSITORY="git://git.netsurf-browser.org/nsgenbind.git"
ARG NSGENBIND_VERSION="ecdd70336d64b21f57313c9c9e55e5f00f48f576"
RUN git clone ${NSGENBIND_REPOSITORY} nsgenbind && \
    (cd nsgenbind && [ -n "${NSGENBIND_VERSION}" ] && git checkout ${NSGENBIND_VERSION} || true)
# Skip nsgenbind build - has compilation issues with .base64 assembler pseudo-op
# This tool is only needed for JavaScript bindings, not for basic framebuffer build
# RUN unset CHOST && make -C ${TARGET_WORKSPACE}/nsgenbind PREFIX=${BUILD_PREFIX} HOST=${BUILD} CFLAGS="-O0 -fno-lto" $(cat /tmp/use_cpus.txt) install

# internal libraries all frontends require (order is important)
ARG LIBWAPCAPLET_REPOSITORY="git://git.netsurf-browser.org/libwapcaplet.git"
ARG LIBWAPCAPLET_VERSION="release/0.4.3"
RUN git clone ${LIBWAPCAPLET_REPOSITORY} libwapcaplet && \
    (cd libwapcaplet && [ -n "${LIBWAPCAPLET_VERSION}" ] && git checkout ${LIBWAPCAPLET_VERSION} || true)
RUN make -C ${TARGET_WORKSPACE}/libwapcaplet PREFIX=${PREFIX} HOST=${HOST} $(cat /tmp/use_cpus.txt) install

ARG LIBPARSERUTILS_REPOSITORY="git://git.netsurf-browser.org/libparserutils.git"
ARG LIBPARSERUTILS_VERSION="d101b2bb6dc98050f8f1b04d9d2bfeeff5a120c7"
RUN git clone ${LIBPARSERUTILS_REPOSITORY} libparserutils && \
    (cd libparserutils && [ -n "${LIBPARSERUTILS_VERSION}" ] && git checkout ${LIBPARSERUTILS_VERSION} || true)
RUN make -C ${TARGET_WORKSPACE}/libparserutils PREFIX=${PREFIX} HOST=${HOST} $(cat /tmp/use_cpus.txt) install

ARG LIBHUBBUB_REPOSITORY="git://git.netsurf-browser.org/libhubbub.git"
ARG LIBHUBBUB_VERSION="c4039d355598c9fabbdcc7ef5a663571ef40211d"
RUN git clone ${LIBHUBBUB_REPOSITORY} libhubbub && \
    (cd libhubbub && [ -n "${LIBHUBBUB_VERSION}" ] && git checkout ${LIBHUBBUB_VERSION} || true)
RUN make -C ${TARGET_WORKSPACE}/libhubbub PREFIX=${PREFIX} HOST=${HOST} $(cat /tmp/use_cpus.txt) install

ARG LIBDOM_REPOSITORY="git://git.netsurf-browser.org/libdom.git"
ARG LIBDOM_VERSION="ac5f4ce817d1421798aa4b94daee8deb84e40f76"
RUN git clone ${LIBDOM_REPOSITORY} libdom && \
    (cd libdom && [ -n "${LIBDOM_VERSION}" ] && git checkout ${LIBDOM_VERSION} || true)
RUN make -C ${TARGET_WORKSPACE}/libdom PREFIX=${PREFIX} HOST=${HOST} $(cat /tmp/use_cpus.txt) install

ARG LIBCSS_REPOSITORY="git://git.netsurf-browser.org/libcss.git"
ARG LIBCSS_VERSION="747cf5e859cd0f26c140c7687dca227f1e316781"
RUN git clone ${LIBCSS_REPOSITORY} libcss && \
    (cd libcss && [ -n "${LIBCSS_VERSION}" ] && git checkout ${LIBCSS_VERSION} || true)
RUN make -C ${TARGET_WORKSPACE}/libcss PREFIX=${PREFIX} HOST=${HOST} $(cat /tmp/use_cpus.txt) install

ARG LIBNSGIF_REPOSITORY="git://git.netsurf-browser.org/libnsgif.git"
ARG LIBNSGIF_VERSION="f29bbfbc5cbfe36a0f4f98d84bf1f84d6e4ee1d4"
RUN git clone ${LIBNSGIF_REPOSITORY} libnsgif && \
    (cd libnsgif && [ -n "${LIBNSGIF_VERSION}" ] && git checkout ${LIBNSGIF_VERSION} || true)
RUN make -C ${TARGET_WORKSPACE}/libnsgif PREFIX=${PREFIX} HOST=${HOST} $(cat /tmp/use_cpus.txt) install

ARG LIBNSBMP_REPOSITORY="git://git.netsurf-browser.org/libnsbmp.git"
ARG LIBNSBMP_VERSION="release/0.1.6"
RUN git clone ${LIBNSBMP_REPOSITORY} libnsbmp && \
    (cd libnsbmp && [ -n "${LIBNSBMP_VERSION}" ] && git checkout ${LIBNSBMP_VERSION} || true)
RUN make -C ${TARGET_WORKSPACE}/libnsbmp PREFIX=${PREFIX} HOST=${HOST} $(cat /tmp/use_cpus.txt) install

ARG LIBUTF8PROC_REPOSITORY="git://git.netsurf-browser.org/libutf8proc.git"
ARG LIBUTF8PROC_VERSION="release/2.4.0-1"
RUN git clone ${LIBUTF8PROC_REPOSITORY} libutf8proc && \
    (cd libutf8proc && [ -n "${LIBUTF8PROC_VERSION}" ] && git checkout ${LIBUTF8PROC_VERSION} || true)
RUN make -C ${TARGET_WORKSPACE}/libutf8proc PREFIX=${PREFIX} HOST=${HOST} $(cat /tmp/use_cpus.txt) install

ARG LIBNSUTILS_REPOSITORY="git://git.netsurf-browser.org/libnsutils.git"
ARG LIBNSUTILS_VERSION="release/0.1.0"
RUN git clone ${LIBNSUTILS_REPOSITORY} libnsutils && \
    (cd libnsutils && [ -n "${LIBNSUTILS_VERSION}" ] && git checkout ${LIBNSUTILS_VERSION} || true)
RUN make -C ${TARGET_WORKSPACE}/libnsutils PREFIX=${PREFIX} HOST=${HOST} $(cat /tmp/use_cpus.txt) install

ARG LIBNSPSL_REPOSITORY="git://git.netsurf-browser.org/libnspsl.git"
ARG LIBNSPSL_VERSION="release/0.1.6"
RUN git clone ${LIBNSPSL_REPOSITORY} libnspsl && \
    (cd libnspsl && [ -n "${LIBNSPSL_VERSION}" ] && git checkout ${LIBNSPSL_VERSION} || true)
RUN make -C ${TARGET_WORKSPACE}/libnspsl PREFIX=${PREFIX} HOST=${HOST} $(cat /tmp/use_cpus.txt) install

ARG LIBNSLOG_REPOSITORY="git://git.netsurf-browser.org/libnslog.git"
ARG LIBNSLOG_VERSION="release/0.1.3"
RUN git clone ${LIBNSLOG_REPOSITORY} libnslog && \
    (cd libnslog && [ -n "${LIBNSLOG_VERSION}" ] && git checkout ${LIBNSLOG_VERSION} || true)
RUN make -C ${TARGET_WORKSPACE}/libnslog PREFIX=${PREFIX} HOST=${HOST} $(cat /tmp/use_cpus.txt) install

ARG LIBSVGTINY_REPOSITORY="git://git.netsurf-browser.org/libsvgtiny.git"
ARG LIBSVGTINY_VERSION="f66051cab457438eefd23e1e2c6e2197894b2d52"
RUN git clone ${LIBSVGTINY_REPOSITORY} libsvgtiny && \
    (cd libsvgtiny && [ -n "${LIBSVGTINY_VERSION}" ] && git checkout ${LIBSVGTINY_VERSION} || true)
RUN make -C ${TARGET_WORKSPACE}/libsvgtiny PREFIX=${PREFIX} HOST=${HOST} $(cat /tmp/use_cpus.txt) install

# Create a template of the build directory structure for volume mount initialization
# This preserves the PREFIX directories (inst-*) that contain the build system and libraries
RUN cp -r ${TARGET_WORKSPACE} ${TARGET_WORKSPACE}.template

# libnsfb and netsurf are now git submodules and will be built via build.sh
# They are mounted into the container at build time
