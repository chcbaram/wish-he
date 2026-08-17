cmake_minimum_required(VERSION 3.13)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON CACHE INTERNAL "")
set(CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/tools")
include(hpmicro-riscv-gcc)


# 프로젝트 이름은 최종 출력 파일 이름으로 사용된다.
#
set(PRJ_NAME wish-he)


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
  # QMK 는 트리 전체가 들어와 있지만 쓰는 파일만 골라 쓴다.
  # 재귀 glob 에 걸리면 안 쓰는 기능까지 전부 딸려온다.
  src/ap/modules/qmk
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
# 최적화 수준.
#
#   이식 중에는 추적이 쉬운 -O0 를 기본으로 두었다. USB 폴링만 보면 차이가 없어서
#   (-O0 / -Og 모두 약 5900/s) 그대로 두어도 되는 줄 알았는데, 스캔은 얘기가 달랐다.
#
#     스캔 1회 (8 step x 8 ch)      -O0        -O2
#       mux + 세틀링              5.998 us   1.262 us
#       + ADC 변환 대기          23.732     19.528      (하드웨어 변환 시간)
#       + 데드밴드 필터          13.915      2.619
#       + 기준값 추적·판정       18.395      5.067
#       ─────────────────────────────────────────────
#       합계                     62.040     28.476
#
#   후처리가 5배 가까이 줄었다. 12편의 목표(스캔 25us 대)를 구조를 바꾸지 않고
#   여기서 거의 다 받았다. 디버깅 때 변수가 사라지는 값은 치를 만하다.
#     cmake -DHW_OPT=-O0 -S . -B build   으로 되돌릴 수 있다.
#
if(NOT DEFINED HW_OPT)
  set(HW_OPT -O2)
endif()
message(STATUS "Optimize: ${HW_OPT}")

# 키보드 모델 선택. keyboards/<모델>/ 에 레이아웃 원본과 생성물이 들어 있다.
#   cmake -DHW_KEYBOARD=<모델> -S . -B build  으로 전환
if(NOT DEFINED HW_KEYBOARD)
  set(HW_KEYBOARD wish60-he-7u)
endif()
message(STATUS "Keyboard: ${HW_KEYBOARD}")

# QMK — 소스 목록(QMK_SRC_FILES)과 인클루드 경로(QMK_INC_DIR)를 채워준다
include(src/ap/modules/qmk/CMakeLists.txt)

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
  ${HPM_SDK_DIR}/drivers/src/hpm_adc16_drv.c
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
  ${QMK_SRC_FILES}
)


# ★ 보드 config.h 를 모든 C 파일 맨 앞에 강제로 넣는다.
#
#   QMK 상류 빌드가 하는 일(-include config.h)을 우리가 빼먹고 있었다. 그 결과
#   config.h 는 host.h 를 타고 **늦게** 들어왔고, 그전에 eeconfig.h 가 먼저 처리된
#   번역 단위에서는
#
#     #ifndef EECONFIG_USER_DATA_SIZE
#     #    define EECONFIG_USER_DATA_SIZE 0
#
#   가 0 으로 확정돼 버렸다. 같은 매크로가 파일마다 0 이기도 512 이기도 했다는 뜻이고,
#   EECONFIG_SIZE 와 사용자 영역 주소가 번역 단위마다 어긋난다. -Werror 를 켜고서야
#   드러났다 (config.h 가 재정의로 보였다).
#
#   ASM 에는 넣지 않는다.
#
target_compile_options(${EXECUTABLE} PRIVATE
  $<$<COMPILE_LANGUAGE:C>:-include$<SEMICOLON>config.h>
)

set(HW_OUR_SRC ${SRC_FILES} ${SRC_FILES_RECURSE} ${USB_SRC_FILES})

# 보드별 파일도 우리 것이다. QMK 소스 목록에 얹혀 있을 뿐이다.
if (EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/keyboards/${HW_KEYBOARD}/via_port.c")
  list(APPEND HW_OUR_SRC
       ${CMAKE_CURRENT_SOURCE_DIR}/keyboards/${HW_KEYBOARD}/via_port.c)
endif()

# 우리가 쓴 파일만 경고를 오류로 올린다.
#
#   왜 필요한가. 이런 줄을 하루 종일 쫓은 적이 있다.
#
#     void eeconfig_update_keymap(uint16_t val);   /* 값을 받는다 */
#     eeconfig_update_keymap(&keymap_config);      /* 포인터를 넘겼다 */
#
#   포인터가 16비트로 잘려 설정 전체로 저장됐고 하필 QMK 매직 스왑 비트에 걸렸다.
#   매트릭스도 키맵도 정의 JSON 도 전부 정상인데 나가는 키코드만 달랐다 — 눈으로
#   볼 수 있는 곳에는 단서가 없었다. **컴파일러는 이미 경고하고 있었다**
#   (-Wint-conversion, 기본 켜짐). QMK 와 SDK 가 쏟아내는 경고에 묻혔을 뿐이다.
#
#   그래서 범위를 우리 것으로 좁힌다. QMK 와 HPM SDK 는 우리가 고칠 물건이 아니고,
#   거기까지 올리면 다시 무시하게 된다 — 무시되는 경고는 없는 것과 같다.
#
#   -Wextra 는 넣지 않았다. src/common 은 다른 프로젝트와 공유하는 틀이라
#   sign-compare 만 여덟 군데 나오는데, 그걸 고치면 원본과 갈라진다. 잡으려던
#   종류(포인터를 정수 자리에 넘기기)는 -Wall 로 이미 잡힌다.
#
#   ★ 새 경고가 나면 끄지 말고 고칠 것. 못 고칠 사정이 있으면 그 파일만
#     빼고 이유를 여기 남긴다.
#
set_source_files_properties(${HW_OUR_SRC} PROPERTIES COMPILE_OPTIONS
  "-Wall;-Werror"
)



target_include_directories(${EXECUTABLE} PRIVATE
  keyboards/${HW_KEYBOARD}
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

  # QMK (src/ap/modules/qmk/CMakeLists.txt 에서 설정)
  ${QMK_INC_DIR}
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
  ${HW_OPT}
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

#
# 태그를 심은 사본을 하나 더 만든다 — <이름>-tag.bin
#
# ★ 원본(.bin)은 그대로 둔다.
#
#   그쪽은 태그가 0 이라 부팅 검사를 건너뛴다. 개발 중 JTAG 나 iap_update.py 로
#   바로 굽는 흐름이 그대로 살아 있어야 하기 때문이다.
#
#   -tag.bin 은 웹 도구의 "파일에서 굽기" 로 올릴 때 쓴다. 그때는 앱이 자기 CRC 를
#   대조해서 반쯤 써진 이미지를 잡아낸다 (src/hw/hw.c 의 hwVerifyFirm).
#
# 태그를 심는 규칙은 tools/fw_tag.py 한 곳에만 둔다 — make_release.py 도 같은 것을
# 부른다. 두 군데 적으면 반드시 갈라진다.
#
add_custom_command(TARGET ${EXECUTABLE}
  POST_BUILD
  COMMAND python3 ARGS ${CMAKE_CURRENT_SOURCE_DIR}/tools/fw_tag.py
                       ${PROJECT_NAME}.bin ${PROJECT_NAME}-tag.bin
  COMMENT "Invoking: Make Tagged Bin"
  )
