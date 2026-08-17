/*
 * port/via_hid.c  —  VIA 명령을 우리 raw HID 채널에 잇는다.
 *
 * 방향이 둘이다.
 *
 *   호스트 -> 장치   hid_if.c 가 모르는 명령을 여기로 넘긴다 -> raw_hid_receive()
 *   장치 -> 호스트   via.c 가 raw_hid_send() 를 부른다 -> hid_if.c 의 IN 전송
 *
 * ★ 명령 ID 대역이 갈려 있다.
 *
 *   VIA 는 0x01~0x15 를 쓰고 우리 확장은 0xC0 대에 있다. 처음에는 우리 것이
 *   0x01/0x02/0x03 이라 정면으로 겹쳤는데, QMK 를 얹기 전에 옮겼다.
 *   부트로더 점프만 VIA 자리(0x0B)를 그대로 쓴다 — 표준 VIA 도구로도 넣을 수 있다.
 */

#include "via_hid.h"
#include "raw_hid.h"
#include "usb/cherryusb/hid_if.h"


void via_hid_init(void)
{
  hidIfSetRawReceiveFunc(raw_hid_receive);
}

void raw_hid_send(uint8_t *data, uint8_t length)
{
  hidIfSendRaw(data, length);
}
