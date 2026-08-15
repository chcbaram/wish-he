# 2. 콘솔 확보

> 전체 로드맵은 [../steps.md](../steps.md) 를 본다.

이 보드에는 **UART 헤더도 디버그 LED 도 없다.** 출력 수단부터 만들어야 그 뒤가 편하다.

---

## USB CDC + CLI

| 항목 | 값 |
|---|---|
| VID / PID | `0x0483` / `0x5304` |
| 제품 문자열 | `WISH60-HE` |
| 속도 | High Speed (480 Mb/s) |
| 클래스 | `0xEF / 0x02 / 0x01` (Misc / Common / **IAD**) |

**macOS 에서는 반드시 `/dev/cu.*` 를 쓴다.** `/dev/tty.*` 는 캐리어를 기다려 블록된다 —
이것 때문에 CLI 가 죽은 줄 알고 한참 봤다.

```sh
python3 - <<'PY'
import serial, time
p = serial.Serial("/dev/cu.usbmodem00011", 115200, timeout=0.3)
p.dtr = True; p.rts = True
time.sleep(0.3); p.reset_input_buffer()
p.write(b"help\r\n"); time.sleep(1)
print(p.read(8192).decode())
PY
```

---

## RAM 링버퍼 콘솔 (JTAG)

USB 가 올라오기 전이나 죽은 뒤에도 로그를 읽는다. `log.c` 가 모든 `logPrintf()` 출력을
`.noinit` 링버퍼에도 흘린다. **`.noinit` 이라 리셋해도 내용이 남아** 크래시 직전 로그도 본다.

주소는 빌드마다 바뀌므로 ELF 에서 뽑는다.

```sh
A=$(riscv-none-elf-objdump -t build/wish60-he.elf | grep -w log_ram | awk '{print "0x"$1}')
openocd ... -c "dump_image log.bin $A 0x810" -c "exit"

python3 -c "
import struct; d=open('log.bin','rb').read()
m,i,w,s=struct.unpack('<IIII',d[:16])
print((d[16+i:16+s]+d[16:16+i]).decode('utf-8','replace') if w
      else d[16:16+i].decode('utf-8','replace'))"
```

헤더는 `magic('RLOG') / index / wrapped / size` 4 워드다.

> `.noinit` 에 다른 변수를 추가하면 `log_ram` 주소가 밀린다. 고정 주소를 적어두지 말고
> 항상 ELF 에서 뽑을 것.
