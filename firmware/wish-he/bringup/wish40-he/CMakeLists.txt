#
# wish40-he 브링업 시험 이미지 — 빈 칩에 JTAG 으로 굽는다.
#
#   cmake -S bringup/wish40-he -B build/bringup-wish40-he
#   cmake --build build/bringup-wish40-he
#
# ★ 본 펌웨어와 따로 둔다. 부트로더가 아직 없어서 0x80000000 부터 부트 헤더를 단
#   단독 XIP 이미지여야 하고, 회로 시험에는 QMK · USB 가 필요 없다. 섞으면 회로가
#   잘못된 건지 포팅이 잘못된 건지 못 가린다.
#
cmake_minimum_required(VERSION 3.13)
set(FW_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../..)
set(CMAKE_MODULE_PATH "${FW_DIR}/tools")
include(hpmicro-riscv-gcc)

project(wish40-he-bringup LANGUAGES ASM C)
set(EXECUTABLE ${PROJECT_NAME}.elf)

set(SDK ${FW_DIR}/src/lib/hpm_sdk)
set(SOC ${SDK}/soc/HPM5300/HPM5361)

#
# -DBRINGUP_AS_APP=ON 이면 부트로더 위에 얹는 앱으로 링크한다 (0x80020000, "HPM\n").
#
#   부트로더를 끝까지 시험할 앱이 필요한데 wish40 의 본 펌웨어는 아직 없다. 이 이미지는
#   돌면 LED 주기(274ms)로 바로 보이고 JTAG 으로 probe 를 읽을 수 있어 그 자리에 맞다.
#
option(BRINGUP_AS_APP "부트로더 위의 앱으로 링크" OFF)
if(BRINGUP_AS_APP)
  set(BRINGUP_LD ${FW_DIR}/src/bsp/ldscript/HPM5361_IAP_APP.ld)
  set(BRINGUP_BOOTHDR)                            # 부트 헤더는 부트로더가 갖고 있다
else()
  set(BRINGUP_LD ${FW_DIR}/src/bsp/ldscript/HPM5361_FLASH_XIP.ld)
  set(BRINGUP_BOOTHDR ${SOC}/boot/hpm_bootheader.c)
endif()

add_executable(${EXECUTABLE}
  main.c
  ${FW_DIR}/src/bsp/device/board.c

  ${SOC}/toolchains/gcc/start.S
  ${SOC}/toolchains/gcc/initfini.c
  ${SOC}/toolchains/reset.c
  ${SOC}/toolchains/trap.c
  ${SOC}/system.c
  ${SOC}/hpm_sysctl_drv.c
  ${SOC}/hpm_clock_drv.c
  ${SOC}/hpm_otp_drv.c
  ${BRINGUP_BOOTHDR}                    # 단독 이미지일 때만 — ROM 이 직접 띄운다
  ${SDK}/arch/riscv/l1c/hpm_l1c_drv.c
  ${SDK}/drivers/src/hpm_gpio_drv.c
  ${SDK}/drivers/src/hpm_adc16_drv.c
  ${SDK}/drivers/src/hpm_pllctlv2_drv.c
  ${SDK}/drivers/src/hpm_pcfg_drv.c
  ${SDK}/drivers/src/hpm_usb_drv.c      # board.c 가 PHY 풀다운 해제에 쓴다
)

target_include_directories(${EXECUTABLE} PRIVATE
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
)

target_compile_definitions(${EXECUTABLE} PRIVATE
  -DFLASH_XIP=1
  -DHPMSOC_HAS_HPMSDK_GPIO=y   -DHPMSOC_HAS_HPMSDK_PLIC=y    -DHPMSOC_HAS_HPMSDK_MCHTMR=y
  -DHPMSOC_HAS_HPMSDK_PLICSW=y -DHPMSOC_HAS_HPMSDK_UART=y    -DHPMSOC_HAS_HPMSDK_DMAV2=y
  -DHPMSOC_HAS_HPMSDK_USB=y    -DHPMSOC_HAS_HPMSDK_OTP=y     -DHPMSOC_HAS_HPMSDK_ADC16=y
  -DHPMSOC_HAS_HPMSDK_SYSCTL=y -DHPMSOC_HAS_HPMSDK_IOC=y     -DHPMSOC_HAS_HPMSDK_PLLCTLV2=y
  -DHPMSOC_HAS_HPMSDK_PPOR=y   -DHPMSOC_HAS_HPMSDK_PCFG=y    -DHPMSOC_HAS_HPMSDK_PGPR=y
  -DHPMSOC_HAS_HPMSDK_PDGO=y   -DHPMSOC_HAS_HPMSDK_PMP=y     -DHPMSOC_HAS_HPMSDK_DMAMUX=y
)

set(ARCH_FLAGS -march=rv32imac_zicsr_zifencei_zba_zbb_zbc_zbs -mabi=ilp32)

target_compile_options(${EXECUTABLE} PRIVATE
  ${ARCH_FLAGS} -O2 -g3 -ffunction-sections -fdata-sections -fno-builtin -Wall
)

target_link_options(${EXECUTABLE} PRIVATE
  -T${BRINGUP_LD}
  ${ARCH_FLAGS}
  -nostartfiles -static --specs=nano.specs
  -Wl,-Map=${PROJECT_NAME}.map -Wl,--gc-sections -Wl,--print-memory-usage
)

add_custom_command(TARGET ${EXECUTABLE} POST_BUILD
  COMMAND ${CMAKE_OBJCOPY} ARGS -O binary -S ${EXECUTABLE} ${PROJECT_NAME}.bin
)
