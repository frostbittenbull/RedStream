#!/bin/sh
# Сборка RedStream.exe (кросс-компиляция mingw-w64 → Windows x64)
set -e
cd "$(dirname "$0")"
CXX=x86_64-w64-mingw32-g++
RC=x86_64-w64-mingw32-windres
FLAGS="-std=c++17 -O2 -DUNICODE -D_UNICODE -Wall -Wno-unused-parameter -Wno-unused-function -Wno-deprecated-declarations -Wno-unused-variable"
mkdir -p build
(cd res && $RC -O coff -i app.rc -o ../build/app_res.o)
for f in util gfx ui app popups; do $CXX $FLAGS -c src/$f.cpp -o build/$f.o; done
$CXX -o RedStream.exe build/util.o build/gfx.o build/ui.o build/app.o build/popups.o build/app_res.o \
  -municode -mwindows -static -static-libgcc -static-libstdc++ -s \
  -lgdiplus -lcomctl32 -lwininet -lwinmm -lshell32 -lshlwapi -lole32 -loleaut32 -luuid -lgdi32 -luser32 -ladvapi32 -ldwmapi
echo "OK: $(ls -la RedStream.exe)"
