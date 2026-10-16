#!/bin/bash

set -e

export DBUILD_CLIENT="${DBUILD_CLIENT:-0}"
export DBUILD_SERVER="${DBUILD_SERVER:-1}"
export DUSE_HTTP="${DUSE_HTTP:-1}"
export DUSE_CODEC_OPUS="${DUSE_CODEC_OPUS:-1}"
export DUSE_VOIP="${DUSE_VOIP:-1}"
export DCMAKE_INSTALL_PREFIX="${DCMAKE_INSTALL_PREFIX:-~/openfakk2}"
SOURCE_DIR="${SOURCE_DIR:-$(cd "$(dirname "$0")/../.." && pwd)}"
CMAKE_OPTS="${CMAKE_OPTS:-}"

if ! [ -x "$(command -v cmake)" ]; then
        echo "This build script requires 'cmake' to be installed." >&2
        echo "Please install it through your normal package installation system." >&2
        exit 1
fi

echo " This build process requires all of the dependencies necessary for an OpenFAKK2 server.
 If you do not have the necessary dependencies the build will fail.

 We will be building from the source tree at ${SOURCE_DIR}
 The resulting binary will be installed to ${DCMAKE_INSTALL_PREFIX}

 If you need to change these, please set variables as follows:

 SOURCE_DIR=~/src/openfakk2 DCMAKE_INSTALL_PREFIX=~/somewhere $0"

BUILD_DIR="$(mktemp -d)"
trap "rm -rf $BUILD_DIR" EXIT

while true; do
        read -p "Are you ready to compile OpenFAKK2 from ${SOURCE_DIR}, and have it installed into $DCMAKE_INSTALL_PREFIX? " yn
        case $yn in
                [Yy]*)
                        cmake -S "$SOURCE_DIR" -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Release \
                        -DBUILD_CLIENT="$DBUILD_CLIENT" \
                        -DBUILD_SERVER="$DBUILD_SERVER" \
                        -DUSE_HTTP="$DUSE_HTTP" \
                        -DUSE_CODEC_OPUS="$DUSE_CODEC_OPUS" \
                        -DUSE_VOIP="$DUSE_VOIP" \
                        -DCMAKE_INSTALL_PREFIX="$DCMAKE_INSTALL_PREFIX" \
                        $CMAKE_OPTS
                        cmake --build "$BUILD_DIR"
                        cmake --install "$BUILD_DIR"
                        exit
                        ;;
                [Nn]*)
                        echo "aborting installation."
                        exit
                        ;;
                *)
                        echo "Please answer yes or no."
                        ;;
        esac
done
