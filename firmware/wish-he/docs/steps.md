# WISH60-HE 구현 단계

기능을 단계별로 쌓아 올리는 순서와 현재 위치를 적는다.
각 단계는 **무엇을 만드는지 / 어떻게 검증했는지**를 같이 남긴다.

> **원칙** — 먼저 **완전히 동작하는 키보드**를 만들고, 고속화와 HE 고유 기능(래피드
> 트리거 등)은 그 뒤에 얹는다. 1단계에서는 HE 스위치를 **임계값 하나로 on/off** 판정해
> 일반 기계식 스위치처럼 다룬다.

상세는 [README.md](README.md), IAP 인터페이스는
`../hpm5361-fw/docs/board-iap.md` 를 본다.

| | 단계 | 상태 |
|---|---|---|
| 1 | [IAP 위에서 부팅](steps/01-boot-on-iap.md) | ✅ |
| 2 | [콘솔 확보 (RAM 로그 · USB CDC · CLI)](steps/02-console.md) | ✅ |
| 3 | [리셋 / 부트로더 진입](steps/03-reset-boot.md) | ✅ |
| 4 | [WS2812 (SPI + DMA)](steps/04-ws2812.md) | ✅ |
| 5 | ADC + MUX 원시값 스캔 | ⬜ **다음** |
| 6 | 키 판정 — 임계값 on/off | ⬜ |
| 7 | USB HID 키보드 + 키맵 | ⬜ |
| 8 | VIA 지원 | ⬜ |
| 9 | [파이썬 IAP 업데이터](steps/09-iap-updater.md) | ✅ |
| 10 | 고속화 — 8kHz 리포트 | ⬜ |
| 11 | HE 고유 기능 — 래피드 트리거 | ⬜ |
| 12 | LED 효과 · 전류 리미터 | ⬜ |

---

## 완료된 단계

각 단계의 상세 — 무엇을 만들었고 어떻게 검증했으며 어떤 함정이 있었는지 — 는
개별 문서에 있다.

- **[1. IAP 위에서 부팅](steps/01-boot-on-iap.md)** — 링커 · 외부 전원 DCDC 함정 · IAP 인계 정리
- **[2. 콘솔 확보](steps/02-console.md)** — USB CDC + CLI · JTAG 로 읽는 RAM 링버퍼
- **[3. 리셋 / 부트로더 진입](steps/03-reset-boot.md)** — `reset boot` · ROM API 플래시 · 캐시 함정
- **[4. WS2812](steps/04-ws2812.md)** — SPI1 + HDMA 논블로킹 · DMA 채널 배분 · 전류 예산
- **[9. 파이썬 IAP 업데이터](steps/09-iap-updater.md)** — JTAG 없는 USB 업데이트 (84KB / 1.2초)

---

## 5. ADC + MUX 원시값 스캔 ⬜ ← 다음

키 판정 이전에 **센서가 실제로 어떤 값을 내는지부터 본다.**

핀맵은 덤프 분석으로 확보돼 있다 (`../hpm5361-fw/docs/flash_dump.md` 3절):

```
PB00 ~ PB03  ADC0 시퀀스 4채널
PB04 ~ PB07  ADC1 시퀀스 4채널      -> 동시 8채널
PY00 ~ PY02  3비트 MUX 주소         -> 8스텝
                8채널 x 8스텝 = 64키
```

- MUX 주소는 `DO[PY].VALUE` 에 포트 전체를 한 번에 쓴다
- 상용은 그레이 코드 `[6,7,5,4,0,1,3,2]` 를 쓴다 (스텝당 1비트만 변해 크로스토크가 적다)
- DMA 는 `HW_DMA_CH_ADC = 3~`, **우선순위 HIGH**
- ADC 버퍼는 읽기이므로 `l1c_dc_invalidate()` 쪽 캐시 관리가 필요하다

**할 일**
- [ ] ADC0/ADC1 초기화, PB00~PB07 아날로그 먹싱
- [ ] PY00~PY02 MUX 주소 출력
- [ ] 스텝 순회 + 변환 완료 대기
- [ ] CLI `keys raw` — 8×8 원시값 표로 덤프

**검증 기준** — 키를 누를 때 해당 셀 값이 단조롭게 변하는지, 무자석 상태의
기준값이 채널마다 얼마나 흩어지는지 확인.

## 6. 키 판정 — 임계값 on/off ⬜

`m483-fw` 의 `keys` 인터페이스를 그대로 쓴다. `keysGetRow()` 가 QMK 의
`matrix_row_t` 비트마스크와 호환되므로 **상위 계층이 HE 인지 매트릭스인지 몰라도 된다.**

```c
bool     keysInit(...);
bool     keysUpdate(void);
bool     keysGetPressed(uint16_t row, uint16_t col);
uint16_t keysGetRow(uint16_t row);      /* row = MUX 스텝, col = ADC 채널 */
```

- [ ] 부팅 시 기준값(무압) 캘리브레이션
- [ ] 임계값 + 히스테리시스로 on/off
- [ ] CLI `keys` — 눌린 키 표시

## 7. USB HID 키보드 + 키맵 ⬜

- [ ] **IF0 = 부트 키보드** (일부 BIOS 가 첫 인터페이스만 본다. 상용도 IF0)
- [ ] CDC 는 IAD 로 묶어 공존 (디바이스 클래스가 이미 `0xEF/0x02/0x01`)
- [ ] 키맵 테이블 + 스캔코드 변환

**여기까지가 "완전히 동작하는 키보드"** 목표 지점이다.

## 8. VIA 지원 ⬜

- [ ] IF1 = raw HID, usage page `0xFF60`, 32B IN/OUT
- [ ] VIA JSON (VID/PID 일치 필요)

## 9. 파이썬 IAP 업데이터 ✅

완료 — [steps/09-iap-updater.md](steps/09-iap-updater.md) 참조.
84,840 B 기록에 1.2초. `reset boot` → IAP → `tools/iap_update.py` → 앱 재부팅.

## 10. 고속화 — 8kHz 리포트 ⬜

- [ ] 스캔 주기 측정 후 예산 배분
- [ ] bInterval=1 (HS 125us)
- [ ] 필요하면 스캔을 타이머 트리거 + DMA 로

## 11. HE 고유 기능 — 래피드 트리거 ⬜

- [ ] 거리 변환 (ADC 값 → mm)
- [ ] 액추에이션 포인트 조정
- [ ] 래피드 트리거 / 연속 리셋

## 12. LED 효과 · 전류 리미터 ⬜

- [ ] **프레임 전류 합산 리미터** — 83개 보드에서는 사실상 필수
- [ ] 키 반응 효과 등
