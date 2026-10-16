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

RUN apt-get update && apt-get install -y --no-install-recommends \
    git-core gnupg flex bison gperf build-essential \
    zip curl zlib1g-dev libc6-dev-i386 libncurses5 lib32ncurses5-dev \
    x11proto-core-dev libx11-dev lib32z-dev libgl1-mesa-dev \
    libxml2-utils xsltproc unzip python2.7 openjdk-6-jdk \
    ccache schedtool \
    lib32stdc++6 lib32z1 \
    && apt-get clean && rm -rf /var/lib/apt/lists/* \
    && ln -sf python2.7 /usr/bin/python

# repo tool (bind-mounted trees expect it on PATH)
ADD https://storage.googleapis.com/git-repo-downloads/repo /usr/local/bin/repo
RUN chmod +x /usr/local/bin/repo

# KitKat-era identity + ccache defaults (tune -M to the server's disk)
ENV USE_CCACHE=1 \
    CCACHE_DIR=/srv/ccache-kitkat
RUN git config --global user.name "retro-android-ports" && \
    git config --global user.email "retro-android-ports@localhost"

WORKDIR /srv/aosp-kitkat
CMD ["/bin/bash"]
