rm -rf build-qnx
source ~/qnx-rpi5/repo/qnx800/qnxsdp-env.sh
echo "$QNX_TARGET"      # should now point into ~/qnx-rpi5/repo/qnx800/target/qnx
which qcc               # should agree with $QNX_HOST

~/qt6-qnx/bin/qt-cmake -S . -B build-qnx -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DQNX_TARGET_ARCH=gcc_ntoaarch64le
cmake --build build-qnx
