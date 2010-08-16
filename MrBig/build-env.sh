#! /bin/bash
W32_TOOLS_DIR=/cygdrive/m/toolchain/mingw-w32-1.0-bin_i686-mingw_20100702/bin
W64_TOOLS_DIR=/cygdrive/m/toolchain/mingw-w64-1.0-bin_i686-mingw_20100702/bin
export PATH=${W32_TOOLS_DIR}:${W64_TOOLS_DIR}:/bin
echo "Type make now."
export PS1='build-env:${PWD##*/}> '
bash
