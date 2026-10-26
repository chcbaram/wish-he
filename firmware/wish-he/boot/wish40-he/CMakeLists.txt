#
# wish40-he 부트로더 — 0x80000000 부터 굽는 단독 이미지.
#
#   cmake -S boot/wish40-he -B build/boot-wish40-he
#   cmake --build build/boot-wish40-he
#
# 계약은 wish60-he 의 부트로더와 같다 (docs/00-hardware.md 3절, 09편).
#
#   0x80003000  부트로더          ~ 0x8001D000 앞까지 (링커가 넘치면 멈춘다)
#   0x8001D000  업데이트 플래그   0xFFFFFFFF 가 아니면 부트로더에 남는다
#   0x80020000  앱                "HPM\n" + 진입점 0x80020004
#
# USB 만 다르다 — 0483:5308 (wish60 은 534B:4102). docs/wish40-he.md
#
cmake_minimum_required(VERSION 3.13)
set(FW_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../..)
set(CMAKE_MODULE_PATH "${FW_DIR}/tools")
include(hpmicro-riscv-gcc)

project(wish40-he-boot LANGUAGES ASM C)
set(EXECUTABLE ${PROJECT_NAME}.elf)

set(SDK ${FW_DIR}/src/lib/hpm_sdk)
set(SOC ${SDK}/soc/HPM5300/HPM5361)
set(USB ${SDK}/middleware/cherryusb)

#
# 링커 스크립트는 XIP 원본을 그대로 쓰고 크기만 줄인다.
#
# ★ 부트로더가 0x1D000(플래그)을 넘으면 안 된다. 넘으면 앱이 플래그를 쓰는 순간
#   부트로더 끝자락이 지워진다. 크기를 묶어 두면 링커가 먼저 멈춘다.
#   사본을 두지 않는 것은 두 벌이 반드시 갈라지기 때문이다.
#
file(READ ${FW_DIR}/src/bsp/ldscript/HPM5361_FLASH_XIP.ld LD_SRC)
string(REPLACE "_flash_size = 1M;" "_flash_size = 0x1D000;" LD_BOOT "${LD_SRC}")
if(LD_SRC STREQUAL LD_BOOT)
  message(FATAL_ERROR "XIP 링커 스크립트에서 _flash_size 줄을 못 찾았다 — 크기를 못 묶는다")
endif()
file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/boot.ld "${LD_BOOT}")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
             ${FW_DIR}/src/bsp/ldscript/HPM5361_FLASH_XIP.ld)

add_executable(${EXECUTABLE}
  main.c
  iap.c
  usb.c
  ${FW_DIR}/src/bsp/device/board.c

  ${SOC}/toolchains/gcc/start.S
  ${SOC}/toolchains/gcc/initfini.c
  ${SOC}/toolchains/reset.c
  ${SOC}/toolchains/trap.c
  ${SOC}/system.c
  ${SOC}/hpm_sysctl_drv.c
  ${SOC}/hpm_clock_drv.c
  ${SOC}/hpm_otp_drv.c
  ${SOC}/boot/hpm_bootheader.c
  ${SDK}/arch/riscv/l1c/hpm_l1c_drv.c
  ${SDK}/drivers/src/hpm_gpio_drv.c
  ${SDK}/drivers/src/hpm_adc16_drv.c
  ${SDK}/drivers/src/hpm_pllctlv2_drv.c
  ${SDK}/drivers/src/hpm_pcfg_drv.c
  ${SDK}/drivers/src/hpm_usb_drv.c

  ${USB}/core/usbd_core.c
  ${USB}/class/hid/usbd_hid.c
  ${USB}/port/hpmicro/usb_dc_hpm.c
  ${USB}/port/hpmicro/usb_glue_hpm.c
  ${SDK}/components/usb/device/hpm_usb_device.c
)

target_include_directories(${EXECUTABLE} PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}             # usb_config.h — 본 펌웨어 것보다 먼저
  ${FW_DIR}/src/bsp/device
  ${SDK}/arch
  ${SDK}/arch/riscv/intc
  ${SDK}/arch/riscv/l1c
  ${SOC}
  ${SOC}/toolchains
  ${SOC}/boot
  ${SDK}/soc/HPM5300/ip
  ${SDK}/drivers/inc
  ${SDK}/utils
  ${SDK}/generated/include
  ${USB}/common
  ${USB}/core
  ${USB}/class/hid
  ${USB}/port/hpmicro
  ${SDK}/components/usb/device
)

target_compile_definitions(${EXECUTABLE} PRIVATE
  -DFLASH_XIP=1
  -DUSB_SOC_DCD_QTD_COUNT_EACH_ENDPOINT=2
  -DHPMSOC_HAS_HPMSDK_GPIO=y   -DHPMSOC_HAS_HPMSDK_PLIC=y    -DHPMSOC_HAS_HPMSDK_MCHTMR=y
  -DHPMSOC_HAS_HPMSDK_PLICSW=y -DHPMSOC_HAS_HPMSDK_UART=y    -DHPMSOC_HAS_HPMSDK_DMAV2=y
  -DHPMSOC_HAS_HPMSDK_USB=y    -DHPMSOC_HAS_HPMSDK_OTP=y     -DHPMSOC_HAS_HPMSDK_ADC16=y
  -DHPMSOC_HAS_HPMSDK_SYSCTL=y -DHPMSOC_HAS_HPMSDK_IOC=y     -DHPMSOC_HAS_HPMSDK_PLLCTLV2=y
  -DHPMSOC_HAS_HPMSDK_PPOR=y   -DHPMSOC_HAS_HPMSDK_PCFG=y    -DHPMSOC_HAS_HPMSDK_PGPR=y
  -DHPMSOC_HAS_HPMSDK_PDGO=y   -DHPMSOC_HAS_HPMSDK_PMP=y     -DHPMSOC_HAS_HPMSDK_DMAMUX=y
)

set(ARCH_FLAGS -march=rv32imac_zicsr_zifencei_zba_zbb_zbc_zbs -mabi=ilp32)

target_compile_options(${EXECUTABLE} PRIVATE
  ${ARCH_FLAGS} -Os -g3 -ffunction-sections -fdata-sections -fno-builtin -Wall
)

# 우리가 쓴 파일만 경고를 오류로 (본 펌웨어와 같은 규칙)
set_source_files_properties(main.c iap.c usb.c PROPERTIES COMPILE_OPTIONS "-Werror")

target_link_options(${EXECUTABLE} PRIVATE
  -T${CMAKE_CURRENT_BINARY_DIR}/boot.ld
  ${ARCH_FLAGS}
  -nostartfiles -static --specs=nano.specs
  -Wl,-Map=${PROJECT_NAME}.map -Wl,--gc-sections -Wl,--print-memory-usage
)

#
# 산출물은 0x80000000 부터 쓰는 완성 이미지다 (tools/boot_image.py).
# objcopy -O binary 는 쓰지 않는다 — 그 까닭은 boot_image.py 머리에 있다.
#
# 저장소의 boot/wish40-he/bin/ 은 이걸 **손으로** 옮긴 것이다 — VSCode 의
# boot-wish40-release 태스크. 빌드할 때마다 바뀌면 시험한 판이 어느 것인지 모른다.
#
add_custom_command(TARGET ${EXECUTABLE} POST_BUILD
  COMMAND python3 ARGS ${FW_DIR}/tools/boot_image.py ${EXECUTABLE} ${PROJECT_NAME}.bin
)
