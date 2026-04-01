#!/bin/bash

cmake -DCMAKE_BUILD_TYPE=RelWithDebInfo -G Ninja -S . -B build && cmake --build build --parallel && gdb ./build/tests
