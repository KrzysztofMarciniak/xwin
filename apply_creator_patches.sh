#!/bin/sh
# apply creator.patch to the xwin source tree. run from the source directory.
set -e
patch -p1 < "$(dirname "$0")/creator.patch"
