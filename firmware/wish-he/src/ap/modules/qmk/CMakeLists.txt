cmake_minimum_required(VERSION 3.13)

#
# QMK 통합 — 키맵 · 레이어 · 매크로 · VIA 만 맡는다.
#
# 스캔과 눌림 판정, 그리고 나중의 래피드 트리거는 keys.c 가 자기 주기로 돌린다.
# QMK 는 port/matrix.c 를 거쳐 그 결과 비트마스크만 본다.
#
# quantum 트리는 통째로 들어와 있지만 **여기 적힌 것만 컴파일한다.** 상위
# CMakeLists 의 재귀 glob 에서도 이 폴더를 빼두었다 — 안 그러면 안 쓰는 기능까지
# 전부 딸려 들어온다.
#

set(QMK_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/src/ap/modules/qmk")
set(QMK_BOARD "${CMAKE_CURRENT_SOURCE_DIR}/keyboards/${HW_KEYBOARD}")

if (NOT EXISTS "${QMK_BOARD}/config.h")
  message(FATAL_ERROR "QMK 보드 설정이 없다: ${QMK_BOARD}/config.h")
endif()

set(QMK_SRC_FILES
  ${QMK_ROOT}/qmk.c

  # 포트 계층 — 이 보드가 QMK 에 채워 넣는 것들
  ${QMK_ROOT}/port/matrix.c
  ${QMK_ROOT}/port/via_hid.c
  ${QMK_ROOT}/port/driver_usb.c
  ${QMK_ROOT}/port/protocol/host.c
  ${QMK_ROOT}/port/protocol/report.c
  ${QMK_ROOT}/port/platforms/eeprom.c
  ${QMK_ROOT}/port/platforms/timer.c
  ${QMK_ROOT}/port/platforms/wait.c
  ${QMK_ROOT}/port/platforms/bootloader.c
  ${QMK_ROOT}/port/platforms/suspend.c

  # quantum — 최소 집합
  ${QMK_ROOT}/quantum/quantum.c
  ${QMK_ROOT}/quantum/via.c
  ${QMK_ROOT}/quantum/keyboard.c
  ${QMK_ROOT}/quantum/action.c
  ${QMK_ROOT}/quantum/action_tapping.c
  ${QMK_ROOT}/quantum/action_util.c
  ${QMK_ROOT}/quantum/action_layer.c
  ${QMK_ROOT}/quantum/keycode_config.c
  ${QMK_ROOT}/quantum/led.c
  ${QMK_ROOT}/quantum/keymap_common.c
  ${QMK_ROOT}/quantum/dynamic_keymap.c
  ${QMK_ROOT}/quantum/eeconfig.c
  ${QMK_ROOT}/quantum/keymap_introspection.c   # keymap.c 를 #include 한다
  ${QMK_ROOT}/quantum/color.c
  ${QMK_ROOT}/quantum/process_keycode/process_grave_esc.c
  ${QMK_ROOT}/quantum/process_keycode/process_magic.c   # NK_TOGG
)

# 보드별 VIA 커스텀 메뉴 처리 (있으면)
if (EXISTS "${QMK_BOARD}/via_port.c")
  list(APPEND QMK_SRC_FILES ${QMK_BOARD}/via_port.c)
endif()

file(GLOB QMK_SRC_GLOB CONFIGURE_DEPENDS
  ${QMK_ROOT}/quantum/logging/*.c
  ${QMK_ROOT}/quantum/send_string/*.c
)
list(APPEND QMK_SRC_FILES ${QMK_SRC_GLOB})

set(QMK_INC_DIR
  ${QMK_ROOT}
  ${QMK_ROOT}/port
  ${QMK_ROOT}/port/platforms
  ${QMK_ROOT}/port/protocol
  ${QMK_ROOT}/quantum
  ${QMK_ROOT}/quantum/logging
  ${QMK_ROOT}/quantum/keymap_extras
  ${QMK_ROOT}/quantum/send_string
  ${QMK_ROOT}/quantum/process_keycode
  ${QMK_ROOT}/quantum/sequencer
  ${QMK_BOARD}
)

# QMK 가 보드를 찾는 세 진입점
add_compile_definitions(QMK_KEYBOARD_H="quantum.h")
add_compile_definitions(QMK_KEYMAP_CONFIG_H="${QMK_BOARD}/config.h")
add_compile_definitions(KEYMAP_C="${QMK_BOARD}/keymap.c")

add_compile_definitions(VIA_ENABLE)
add_compile_definitions(RAW_ENABLE)
add_compile_definitions(DYNAMIC_KEYMAP_ENABLE)
add_compile_definitions(GRAVE_ESC_ENABLE)

# NKRO 와 확장키는 IF1 (hid_exk_if.c) 로 나간다
add_compile_definitions(NKRO_ENABLE)
add_compile_definitions(EXTRAKEY_ENABLE)

# Hold On Other Key Press 를 VIA 에서 켜고 끈다 -> per-key 콜백 경로가 필요하다
add_compile_definitions(HOLD_ON_OTHER_KEY_PRESS)
add_compile_definitions(HOLD_ON_OTHER_KEY_PRESS_PER_KEY)
