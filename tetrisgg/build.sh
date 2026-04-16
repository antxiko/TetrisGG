#!/bin/bash
# Wrapper para compilar con make de msys64 desde cualquier shell
cd "$(dirname "$0")"
/c/msys64/usr/bin/make.exe -f Makefile.gg "$@"
