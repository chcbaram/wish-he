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
  ${QMK_ROOT}/quantum/mousekey.c
  ${QMK_ROOT}/quantum/process_keycode/process_grave_esc.c
  ${QMK_ROOT}/quantum/process_keycode/process_magic.c   # NK_TOGG

  # RGB 매트릭스. 드라이버는 우리 것을 쓰므로 quantum 쪽 드라이버는 안 넣는다
  # (rgb_matrix_drivers.c 는 IS31 계열용이다). port/rgb_matrix_port.c 가
  # rgb_matrix_driver 를 채운다.
  # lib8tion — FastLED 수학. rgb_matrix 효과와 via.c 가 scale8/sin8 로 쓴다.
  #
  # ★ QMK 코드가 아니라 **들여온 남의 라이브러리**라 src/lib 에 둔다 (hpm_sdk 옆).
  #   상류는 qmk/lib/ 에 두지만, 이 트리에서 들여온 것들이 한자리에 있는 편이 낫다.
  #
  #   경로가 바뀌어도 quantum 쪽 `#include <lib/lib8tion/lib8tion.h>` 는 그대로다 —
  #   src 가 인클루드 경로에 있어서 lib/... 가 그대로 풀린다. 상류 파일을 안 고쳤다.
  #
  #   빌드에 넣는 것은 여기다. src/lib 은 재귀 glob 대상이 아니라(hpm_sdk 가 통째로
  #   딸려오면 안 되므로) 쓰는 쪽에서 명시해야 한다.
  ${CMAKE_CURRENT_SOURCE_DIR}/src/lib/lib8tion/lib8tion.c

  ${QMK_ROOT}/quantum/rgb_matrix/rgb_matrix.c
  ${QMK_ROOT}/quantum/process_keycode/process_rgb.c
  ${QMK_ROOT}/port/rgb_matrix_port.c
)

# 보드 배치에서 생성한 g_led_config
if (EXISTS "${QMK_BOARD}/rgb_config.c")
  list(APPEND QMK_SRC_FILES ${QMK_BOARD}/rgb_config.c)
endif()

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
  ${QMK_ROOT}/quantum/rgb_matrix
  ${QMK_ROOT}/quantum/rgb_matrix/animations
  ${QMK_ROOT}/quantum/rgb_matrix/animations/runners
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
add_compile_definitions(RGB_MATRIX_ENABLE)

# NKRO 와 확장키, 마우스키는 IF1 (hid_exk_if.c) 로 리포트 ID 를 나눠 쓴다
add_compile_definitions(NKRO_ENABLE)
add_compile_definitions(EXTRAKEY_ENABLE)
add_compile_definitions(MOUSEKEY_ENABLE)
add_compile_definitions(MOUSE_ENABLE)
add_compile_definitions(MOUSE_SHARED_EP)
# MOUSE_EXTENDED_REPORT 는 켜지 않는다 — 기술자가 X/Y 를 int8 로 잡고 있어 어차피 잘린다

# Hold On Other Key Press 를 VIA 에서 켜고 끈다 -> per-key 콜백 경로가 필요하다
add_compile_definitions(HOLD_ON_OTHER_KEY_PRESS)
add_compile_definitions(HOLD_ON_OTHER_KEY_PRESS_PER_KEY)
