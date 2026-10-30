# rk3506-mini cross toolchain environment
# source from anywhere:  . ~/rk3506-dev/env.sh
export RK3506_TC=~/toolchains/arm-buildroot-linux-gnueabihf_sdk-buildroot
export RK3506_SYSROOT=$RK3506_TC/arm-buildroot-linux-gnueabihf/sysroot
export RK3506_BOARD=rk3506-buildroot   # ssh alias, see ~/.ssh/config
export PATH=$RK3506_TC/bin:$PATH
echo "rk3506 toolchain ready: $RK3506_TC"
