#!/bin/bash

set -e

mkdir -p build
cd build
cmake .. -DCMAKE_INSTALL_PREFIX=$HOME/opt/msgflo-cpp
make -j
make install
cd ..

mkdir -p build-examples
cd build-examples
cmake ../examples -DMsgFlo_DIR=$HOME/opt/msgflo-cpp/lib/cmake/MsgFlo
make
