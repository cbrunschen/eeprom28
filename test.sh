#!/bin/bash

cmake -G Ninja -S . -B build && cmake --build build --parallel && ./build/tests
