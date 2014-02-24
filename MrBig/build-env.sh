#! /bin/bash
W32_TOOLS_DIR=/cygdrive/d/mingw/x32-4.8.1-posix-sjlj-rev5/mingw32/bin
W64_TOOLS_DIR=/cygdrive/d/mingw/x64-4.8.1-posix-seh-rev5/mingw64/bin
export PATH=${W32_TOOLS_DIR}:${W64_TOOLS_DIR}:/bin
echo "Type make now."
export PS1='build-env:${PWD##*/}> '
bash
