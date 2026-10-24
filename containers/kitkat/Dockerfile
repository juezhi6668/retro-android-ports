# KitKat (4.4.4) build container — Ubuntu 14.04 + JDK 6 era.
# Usage (on the build server):
#   docker build -f containers/kitkat/Dockerfile -t retro-kitkat:4.4.4 containers/kitkat
#   docker run --rm -it -v <aosp-kitkat>:/srv/aosp-kitkat retro-kitkat:4.4.4
# Then inside: cd /srv/aosp-kitkat && source build/envsetup.sh && lunch full_maple-eng
#
# This image provides the toolchain baseline only (Trusty packages + JDK 6
# + repo). The AOSP tree itself is bind-mounted, never baked in — same
# separation as /srv/donut-hardy (see docs/advanced/porting-notes/donut.md).
# Status: UNTESTED recipe (no KitKat sync has run yet). Log deviations in
# docs/advanced/porting-notes/kitkat.md as they are hit.

FROM ubuntu:14.04

ENV DEBIAN_FRONTEND=noninteractive

# Trusty is EOL: archive.ubuntu.com/security.ubuntu.com and
# old-releases.ubuntu.com no longer serve 14.04 (404 on dists/trusty).
# mirrors.aliyun.com still carries trusty main/universe/updates/security/
# backports; swap the host, keep the /ubuntu/ path. TUNA/USTC had dropped it.
RUN sed -i -e 's|http://archive.ubuntu.com|http://mirrors.aliyun.com|g' \
           -e 's|http://security.ubuntu.com|http://mirrors.aliyun.com|g' \
           /etc/apt/sources.list && \
    apt-get update && apt-get install -y --no-install-recommends \
    git-core gnupg flex bison gperf build-essential \
    zip curl zlib1g-dev libc6-dev-i386 libncurses5 lib32ncurses5-dev \
    x11proto-core-dev libx11-dev lib32z-dev libgl1-mesa-dev \
    libxml2-utils xsltproc unzip python2.7 openjdk-6-jdk \
    ccache schedtool \
    lib32stdc++6 lib32z1 \
    && apt-get clean && rm -rf /var/lib/apt/lists/* \
    && ln -sf python2.7 /usr/bin/python

# repo is NOT baked in: fetching it from storage.googleapis.com fails on this
# network (TLS handshake timeout) and the tree is already synced before the
# container starts. Mount the host's copy if you need it:
#   -v /usr/local/bin/repo:/usr/local/bin/repo:ro

# KitKat-era identity + ccache defaults (tune -M to the server's disk)
ENV USE_CCACHE=1 \
    CCACHE_DIR=/srv/ccache-kitkat
RUN git config --global user.name "retro-android-ports" && \
    git config --global user.email "retro-android-ports@localhost"

WORKDIR /srv/aosp-kitkat
CMD ["/bin/bash"]
