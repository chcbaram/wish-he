# 9. 파이썬 IAP 업데이터

> 전체 목차는 [README.md](README.md) 를 본다.

**JTAG 없이 USB 만으로 펌웨어를 교체한다.** 실측 84,712 B 기록에 **약 1초**.

```
CLI 에서  reset boot
  -> IAP 업데이트 모드로 재부팅 (USB 제품 문자열이 바뀐다)
  -> python3 tools/iap_update.py build/wish60-he.bin
  -> 앱으로 재부팅. 부트 플래그는 resetInit() 이 자동 소거
```

---

## 도구

`tools/iap_update.py` — **의존성 없음.** brew 의 `libhidapi` 를 `ctypes` 로 직접 부른다.

```sh
python3 tools/iap_update.py --list          # HID 장치 전부
python3 tools/iap_update.py --info          # 벤더 usage 후보만
python3 tools/iap_update.py fw.bin          # 기록
python3 tools/iap_update.py --path <경로> fw.bin
```

이미지 선두가 `"HPM\n"` 이 아니면 거부한다 — IAP 가 인식하지 못하는 이미지다.

## 업데이트 채널

IAP 모드에서 HID 인터페이스가 4개 뜬다. 그중 **IF3 / usage page `0xFF53`** 이
업데이트 채널이다(64바이트 리포트, EP `0x05` OUT / `0x85` IN).

| IF | usage page | 용도 |
|---|---|---|
| 0 | `0x0001/0x06` | 부트 키보드 |
| 1 | **`0xFF60`** | VIA 계열 페이지 |
| 2 | `0x0001`, `0x000C` | 마우스 · 컨슈머 · 시스템 |
| **3** | **`0xFF53`** | **업데이트 (64B)** |

## 프로토콜

요청·응답 모두 64바이트 고정. `buf[0]` 이 명령, `buf[1]` 이 길이, `buf[4..]` 가 데이터.
응답은 `[0]=0x85`, `[1]`=1(성공)/0(실패).

| cmd | 동작 |
|---|---|
| `0x81` | 시작. 기록 주소를 `0x80020000` 으로 고정, `[4..7]`=총 길이(LE32) |
| `0x80` | 데이터를 4KB 페이지 버퍼에 누적 |
| `0x82` | **현재 버퍼를 기록하고 나서** 이 리포트의 데이터를 붙인다 |
| `0x83` | 종료 — 앱으로 점프 |

---

## ★ 클라이언트에서 틀리기 쉬운 두 가지

### ① `0x82` 에 "페이지를 채우는 마지막 청크" 를 실으면 안 된다

`0x82` 는 **먼저 flush 하고 나서** 붙인다. 페이지를 채우는 청크를 여기 실으면
덜 찬 페이지가 `0xFF` 로 패딩돼 기록되고 데이터가 밀린다.

```python
# 옳은 순서 — 0x80 으로 정확히 4096 을 채운 뒤, 빈 0x82 로 flush
n = min(60, total - sent, 4096 - in_page)
iap.data(chunk)                 # 언제나 0x80
if in_page == 4096:
    iap.flush()                 # 0x82, 페이로드 없음
```

`pagelen == 0` 이면 `0x82` 는 아무것도 하지 않으므로 빈 flush 는 안전하다.

### ② `0x83` 은 status 를 1 로 세우지 않는다

IAP 의 `0x83` 분기는 코드상 `nop` 뿐이라 **status 0 이 정상**이다. 실패로 처리하면
멀쩡한 업데이트를 오류로 본다.

---

## 안전장치

`0x81` 이 기록 주소를 `0x80020000` 으로 **하드코딩**한다. 호스트가 임의 주소를 줄 수
없으므로 **IAP 자신을 덮어쓸 수 없다.** 업데이트가 깨져도 IAP 는 살아남는다.
