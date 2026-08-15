cmake_minimum_required(VERSION 3.13)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON CACHE INTERNAL "")
set(CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/tools")
include(hpmicro-riscv-gcc)


# 프로젝트 이름은 최종 출력 파일 이름으로 사용된다.
#
set(PRJ_NAME wish60-he)


project(${PRJ_NAME}
  LANGUAGES ASM C CXX
)


set(EXECUTABLE ${PRJ_NAME}.elf)


# USB 스택 선택 : 0 = CherryUSB, 1 = TinyUSB
#   cmake -DHW_USB_STACK=1 -S . -B build  으로 전환
#
if(NOT DEFINED HW_USB_STACK)
  set(HW_USB_STACK 0)
endif()



# 지정한 폴더에 있는 파일만 포함한다.
#
file(GLOB SRC_FILES CONFIGURE_DEPENDS
  src/*.cpp
  src/*.c
  src/ap/*.cpp
  src/ap/*.c
  src/bsp/*.c
  src/bsp/device/*.c
)

# 하위폴더에 있는 파일까지 포함한다.
#
file(GLOB_RECURSE SRC_FILES_RECURSE CONFIGURE_DEPENDS
  src/ap/*.cpp
  src/ap/*.c
  src/common/*.c
  src/hw/*.c
  )


# 특정 폴더를 빌드에서 제외한다.
#
# USB 스택별 글루는 재귀 glob 에서 빼고, 선택된 스택일 때만 USB_SRC_FILES 로 명시 추가한다.
#
set(EXCLUDE_PATHS
)

if(HW_USB_STACK EQUAL 0)
  list(APPEND EXCLUDE_PATHS src/hw/driver/usb/tinyusb)
else()
  list(APPEND EXCLUDE_PATHS src/hw/driver/usb/cherryusb)
endif()

foreach(TMP_PATH ${SRC_FILES_RECURSE})
  foreach(EXCLUDE_PATH ${EXCLUDE_PATHS})
    string(FIND ${TMP_PATH} ${EXCLUDE_PATH} RESULT)
    if(NOT ${RESULT} EQUAL -1)
      list(REMOVE_ITEM SRC_FILES_RECURSE ${TMP_PATH})
    endif()
  endforeach(EXCLUDE_PATH)
endforeach(TMP_PATH)


# HPM SDK 는 필요한 파일만 명시적으로 나열한다. (글로브 금지)
#
set(HPM_SDK_DIR src/lib/hpm_sdk)

set(SRC_FILES_SDK
  ${HPM_SDK_DIR}/soc/HPM5300/HPM5361/toolchains/gcc/start.S       # ASM
  ${HPM_SDK_DIR}/soc/HPM5300/HPM5361/toolchains/gcc/initfini.c    # __libc_init_array
  ${HPM_SDK_DIR}/soc/HPM5300/HPM5361/toolchains/reset.c
  ${HPM_SDK_DIR}/soc/HPM5300/HPM5361/toolchains/trap.c
  ${HPM_SDK_DIR}/soc/HPM5300/HPM5361/system.c
  ${HPM_SDK_DIR}/soc/HPM5300/HPM5361/hpm_sysctl_drv.c
  ${HPM_SDK_DIR}/soc/HPM5300/HPM5361/hpm_clock_drv.c
  ${HPM_SDK_DIR}/soc/HPM5300/HPM5361/hpm_otp_drv.c
  # hpm_bootheader.c 제외 — IAP 가 부트 헤더를 갖고 있다. __app_offset__ 를 참조하므로
  # 링커 스크립트에서 그 심볼을 없앤 이상 함께 빼야 한다.
  ${HPM_SDK_DIR}/arch/riscv/l1c/hpm_l1c_drv.c                     # start.S 가 직접 호출
  ${HPM_SDK_DIR}/utils/hpm_swap.c
  ${HPM_SDK_DIR}/utils/hpm_ffssi.c
  ${HPM_SDK_DIR}/utils/hpm_crc32.c

  ${HPM_SDK_DIR}/drivers/src/hpm_uart_drv.c
  ${HPM_SDK_DIR}/drivers/src/hpm_gpio_drv.c
  ${HPM_SDK_DIR}/drivers/src/hpm_spi_drv.c
  ${HPM_SDK_DIR}/drivers/src/hpm_mchtmr_drv.c
  ${HPM_SDK_DIR}/drivers/src/hpm_pllctlv2_drv.c
  ${HPM_SDK_DIR}/drivers/src/hpm_pcfg_drv.c
  ${HPM_SDK_DIR}/drivers/src/hpm_pmp_drv.c
  ${HPM_SDK_DIR}/drivers/src/hpm_dmav2_drv.c
  ${HPM_SDK_DIR}/drivers/src/hpm_usb_drv.c
  )


# USB 스택별 소스 / 인클루드
#
if(HW_USB_STACK EQUAL 0)
  set(USB_SRC_FILES
    ${HPM_SDK_DIR}/middleware/cherryusb/core/usbd_core.c
    ${HPM_SDK_DIR}/middleware/cherryusb/class/cdc/usbd_cdc_acm.c
    ${HPM_SDK_DIR}/middleware/cherryusb/class/hid/usbd_hid.c
    ${HPM_SDK_DIR}/middleware/cherryusb/port/hpmicro/usb_dc_hpm.c
    ${HPM_SDK_DIR}/middleware/cherryusb/port/hpmicro/usb_glue_hpm.c
    ${HPM_SDK_DIR}/components/usb/device/hpm_usb_device.c
  )
  set(USB_INC_DIRS
    src/hw/driver/usb
    src/hw/driver/usb/cherryusb
    ${HPM_SDK_DIR}/middleware/cherryusb/common
    ${HPM_SDK_DIR}/middleware/cherryusb/core
    ${HPM_SDK_DIR}/middleware/cherryusb/class/cdc
    ${HPM_SDK_DIR}/middleware/cherryusb/class/hid
    ${HPM_SDK_DIR}/middleware/cherryusb/port/hpmicro
    ${HPM_SDK_DIR}/components/usb/device
  )
else()
  set(USB_SRC_FILES)
  set(USB_INC_DIRS
    src/hw/driver/usb
    src/hw/driver/usb/tinyusb
  )
endif()


add_executable(${EXECUTABLE}
  ${SRC_FILES}
  ${SRC_FILES_RECURSE}
  ${SRC_FILES_SDK}
  ${USB_SRC_FILES}
)



target_include_directories(${EXECUTABLE} PRIVATE
  src
  src/ap
  src/ap/modules
  src/ap/modules/common
  src/bsp
  src/bsp/device
  src/common
  src/common/core
  src/common/hw/include
  src/hw
  src/hw/driver
  src/lib

  # HPM SDK
  ${HPM_SDK_DIR}/arch
  ${HPM_SDK_DIR}/arch/riscv/intc
  ${HPM_SDK_DIR}/arch/riscv/l1c
  ${HPM_SDK_DIR}/soc/HPM5300/HPM5361
  ${HPM_SDK_DIR}/soc/HPM5300/HPM5361/toolchains
  ${HPM_SDK_DIR}/soc/HPM5300/HPM5361/boot
  ${HPM_SDK_DIR}/soc/HPM5300/ip
  ${HPM_SDK_DIR}/drivers/inc
  ${HPM_SDK_DIR}/utils
  ${HPM_SDK_DIR}/generated/include

  # USB (스택별 — 위 HW_USB_STACK 분기에서 설정)
  ${USB_INC_DIRS}
)


# HPMSOC_HAS_HPMSDK_* 는 SDK 의 soc_modules.list 에서 온다.
# SDK cmake 가 -D 로 넘기던 것을 그대로 재현한다.
#
target_compile_definitions(${EXECUTABLE} PRIVATE
  -DFLASH_XIP=1

  -DHW_USB_STACK=${HW_USB_STACK}
  # QHD/QTD 디스크립터 배열 크기. 기본 8 이면 DLM 을 10KB 먹는다. CDC 에는 2 로 충분(각 QTD 가 16KB 담당).
  -DUSB_SOC_DCD_QTD_COUNT_EACH_ENDPOINT=2

  -DHPMSOC_HAS_HPMSDK_GPIO=y
  -DHPMSOC_HAS_HPMSDK_PLIC=y
  -DHPMSOC_HAS_HPMSDK_MCHTMR=y
  -DHPMSOC_HAS_HPMSDK_PLICSW=y
  -DHPMSOC_HAS_HPMSDK_GPTMR=y
  -DHPMSOC_HAS_HPMSDK_UART=y
  -DHPMSOC_HAS_HPMSDK_I2C=y
  -DHPMSOC_HAS_HPMSDK_SPI=y
  -DHPMSOC_HAS_HPMSDK_CRC=y
  -DHPMSOC_HAS_HPMSDK_TSNS=y
  -DHPMSOC_HAS_HPMSDK_MBX=y
  -DHPMSOC_HAS_HPMSDK_EWDG=y
  -DHPMSOC_HAS_HPMSDK_DMAMUX=y
  -DHPMSOC_HAS_HPMSDK_DMAV2=y
  -DHPMSOC_HAS_HPMSDK_GPIOM=y
  -DHPMSOC_HAS_HPMSDK_MCAN=y
  -DHPMSOC_HAS_HPMSDK_PTPC=y
  -DHPMSOC_HAS_HPMSDK_QEIV2=y
  -DHPMSOC_HAS_HPMSDK_QEO=y
  -DHPMSOC_HAS_HPMSDK_MMC=y
  -DHPMSOC_HAS_HPMSDK_PWM=y
  -DHPMSOC_HAS_HPMSDK_RDC=y
  -DHPMSOC_HAS_HPMSDK_PLB=y
  -DHPMSOC_HAS_HPMSDK_SYNT=y
  -DHPMSOC_HAS_HPMSDK_SEI=y
  -DHPMSOC_HAS_HPMSDK_TRGM=y
  -DHPMSOC_HAS_HPMSDK_USB=y
  -DHPMSOC_HAS_HPMSDK_SDP=y
  -DHPMSOC_HAS_HPMSDK_SEC=y
  -DHPMSOC_HAS_HPMSDK_MON=y
  -DHPMSOC_HAS_HPMSDK_RNG=y
  -DHPMSOC_HAS_HPMSDK_OTP=y
  -DHPMSOC_HAS_HPMSDK_KEYM=y
  -DHPMSOC_HAS_HPMSDK_ADC16=y
  -DHPMSOC_HAS_HPMSDK_DAC=y
  -DHPMSOC_HAS_HPMSDK_OPAMP=y
  -DHPMSOC_HAS_HPMSDK_ACMP=y
  -DHPMSOC_HAS_HPMSDK_SYSCTL=y
  -DHPMSOC_HAS_HPMSDK_IOC=y
  -DHPMSOC_HAS_HPMSDK_PLLCTLV2=y
  -DHPMSOC_HAS_HPMSDK_PPOR=y
  -DHPMSOC_HAS_HPMSDK_PCFG=y
  -DHPMSOC_HAS_HPMSDK_PGPR=y
  -DHPMSOC_HAS_HPMSDK_PDGO=y
  -DHPMSOC_HAS_HPMSDK_PMP=y
  )


# -march/-mabi 는 .S 에도 적용되어야 하므로 제너레이터 표현식을 쓰지 않는다.
# _zicsr_zifencei 는 필수다 (GCC 12+ 기본 isa-spec 이 20191213 이라 I 에서 분리되어 있다).
#
target_compile_options(${EXECUTABLE} PRIVATE
  -march=rv32imac_zicsr_zifencei_zba_zbb_zbc_zbs
  -mabi=ilp32

  -fdata-sections
  -ffunction-sections
  -fomit-frame-pointer
  -fno-builtin

  -fstack-usage
  -Wall
  -Wundef
  -Wno-format
  -Wmissing-field-initializers
  -Wuninitialized

  -g3
  -O0
  )


target_link_options(${EXECUTABLE} PRIVATE
  -T${CMAKE_CURRENT_SOURCE_DIR}/src/bsp/ldscript/HPM5361_IAP_APP.ld

  -march=rv32imac_zicsr_zifencei_zba_zbb_zbc_zbs
  -mabi=ilp32

  -nostartfiles
  -static
  --specs=nano.specs
  -Wl,-u,_printf_float
  -Wl,-u,_scanf_float

  -Wl,-Map=${PRJ_NAME}.map,--cref
  -Wl,--gc-sections
  -Wl,--print-memory-usage
  )

set (CMAKE_C_FLAGS "-Wenum-conversion")
set (CMAKE_CXX_FLAGS "-fno-rtti")


add_custom_command(TARGET ${EXECUTABLE}
  POST_BUILD
  COMMAND ${CMAKE_OBJCOPY} ARGS -O binary -S ${EXECUTABLE} ${PROJECT_NAME}.bin
  COMMENT "Invoking: Make Bin"
  )

add_custom_command(TARGET ${EXECUTABLE}
  POST_BUILD
  COMMAND ${CMAKE_OBJDUMP} ARGS -S -d ${EXECUTABLE} > ${PROJECT_NAME}.asm
  COMMENT "Invoking: Make Asm"
  )
