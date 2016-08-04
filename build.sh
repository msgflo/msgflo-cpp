#!/bin/bash

set -e

mkdir -p build
cd build
cmake .. -DCMAKE_INSTALL_PREFIX=$HOME/opt/msgflo-cpp
make -j
make install
cd ..
