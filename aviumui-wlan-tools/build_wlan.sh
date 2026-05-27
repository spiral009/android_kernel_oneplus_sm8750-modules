#!/bin/bash
# Rebuild qca_cld3_peach_v2.ko against the running kernel (g24f4b1767592)
# Mirrors the Soong legacy-make invocation captured from build_20260513_213448.log.
set -e
ROOT=/home/spiral/AviumUI
KERNEL_SRC=$ROOT/kernel/oneplus/sm8750
KERNEL_OBJ=$ROOT/out/target/product/dodge/obj/KERNEL_OBJ
WLAN=$ROOT/kernel/oneplus/sm8750-modules/qcom/opensource/wlan/qcacld-3.0
OBJBASE=$ROOT/out/target/product/dodge/obj/sm8750-modules/qcom/opensource

CLANG=$ROOT/prebuilts/clang/host/linux-x86/clang-r563880c/bin
BUILDTOOLS=$ROOT/prebuilts/build-tools/linux-x86/bin
KBUILDTOOLS=$ROOT/prebuilts/kernel-build-tools/linux-x86/bin
export PATH=$CLANG:$BUILDTOOLS:$KBUILDTOOLS:$PATH

DATAIPA_SYMS=$OBJBASE/dataipa/drivers/platform/msm/Module.symvers
PLATFORM_SYMS=$OBJBASE/wlan/platform/Module.symvers

cd "$KERNEL_SRC"
make -C "$KERNEL_SRC" O="$KERNEL_OBJ" \
  M=../sm8750-modules/qcom/opensource/wlan/qcacld-3.0 \
  ARCH=arm64 LLVM=1 LLVM_IAS=1 \
  modules \
  WLAN_ROOT="$WLAN" \
  KBUILD_EXTRA_SYMBOLS="$DATAIPA_SYMS $PLATFORM_SYMS" \
  CONFIG_QCA_WIFI_ISOC=0 CONFIG_QCA_WIFI_2_0=1 CONFIG_QCA_CLD_WLAN=m \
  CONFIG_IPA_OUT_OF_TREE=y \
  WLAN_PROFILE=sun_gki_peach-v2 MODNAME=qca_cld3_peach_v2 \
  -j"$(nproc)"

echo "=== BUILD DONE ==="
KO=$OBJBASE/wlan/qcacld-3.0/qca_cld3_peach_v2.ko
STRIPPED=/tmp/qca_cld3_peach_v2.ko
"$CLANG/llvm-strip" --strip-debug "$KO" -o "$STRIPPED"
ls -la "$KO" "$STRIPPED"
echo "vermagic: $(strings "$STRIPPED" | grep -E '^6\.6\.129' | head -1)"
echo "stripped module ready for deploy: $STRIPPED"
