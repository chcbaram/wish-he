# 5. 센서가 내는 값 들여다보기

> 전체 목차는 [README.md](README.md) 를 본다.

키를 눌렸다/말았다로 판정하기 전에, **센서가 실제로 어떤 숫자를 내는지부터** 본다.
핀맵과 ADC 배정은 [00-hardware.md](00-hardware.md) 4절에 있다.

## 한 것

- ADC0/ADC1 시퀀스 모드 + 내장 DMA, 각 4채널 = 동시 8채널
- `PY00~PY03` MUX 주소를 그레이 코드로 8스텝
- CLI `keys dump` / `keys raw` / `keys time` / `keys adc`

```
cli# keys dump
       ch0  ch1  ch2  ch3  ch4  ch5  ch6  ch7
  s0   42280 44741 42964 43579 43536 43809 45003 45823
  s1   42827 44741 42690 45763 45140 43604 43055 43262
  ...
  s7   44741 42964 43715 44741 44456 45243 42918 44593
  scan : 52 us
```

## 스캔 한 바퀴 38us — 8kHz 예산의 30%

```
cli# keys time
scan   : 25796 회 / 초
주기   : 38 us
8kHz 예산 125us 대비 : 30 %
timeout: 0
```

**이 숫자가 이후 설계를 상당히 단순하게 만든다.** 64키 전체를 38us 에 읽으므로
8kHz(125us)를 맞추는 데 타이머 트리거도, HDMA 도, 정교한 파이프라이닝도 필요 없다.
스캔을 그냥 자유 구동으로 돌리고 USB 완료 콜백에서 최신값을 실어보내면 된다
(12편에서 그렇게 간다).

시퀀스 DMA 는 **HDMA 채널을 쓰지 않는다.** ADC 가 자체 AHB 라이터로 직접 메모리에
쓰므로 `hw_def.h` 의 채널 배분과 무관하다. 계획 단계에서 `HW_DMA_CH_ADC` 를 잡아두려
했는데 필요가 없었다.

## 스캔 구조

```c
for (step = 0; step < 8; step++)
{
  gpio_write_port(HPM_GPIO0, GPIO_DO_GPIOY, mux_addr[step]);   /* 주소 */
  keysSettle();                                                /* 16 사이클 = 40ns */

  adc0_done = adc1_done = false;
  adc16_trigger_seq_by_sw(HPM_ADC0);
  adc16_trigger_seq_by_sw(HPM_ADC1);
  keysWaitDone();                                              /* ISR 플래그 스핀 */

  gpio_write_port(HPM_GPIO0, GPIO_DO_GPIOY, mux_addr[step + 1]);  /* ★ 다음 주소 먼저 */
  /* 그 다음에 결과를 읽는다 — 다음 스텝의 세틀링이 이 처리와 겹친다 */
}
```

마지막 두 줄의 순서가 핵심이다. 그래서 주소 테이블에 되돌이용 원소가 하나 더 붙는다.

```c
static const uint8_t mux_addr[8 + 1] = { 6, 7, 5, 4, 0, 1, 3, 2,  6 };
```

---

## 겪은 함정

### ① `SEQ_INT_EN` 은 시퀀스가 아니라 **큐 원소별** 비트다

완료 인터럽트가 영영 오지 않아 스캔이 통째로 타임아웃했다.

```
cli# keys time
scan   : 174 회 / 초
주기   : 5747 us
timeout: 174        <- 174번 스캔해서 174번 다 타임아웃
```

`adc16_seq_config_t.queue[i].seq_int_en` 을 "시퀀스 완료 인터럽트를 쓸 것인가"로 읽고
전부 `false` 로 뒀는데, 실제로는 **"이 큐 원소가 끝나면 알려라"** 다. 하나도 안 켜면
`INT_STS` 의 `SEQ_CMPT` 가 서지 않는다.

```c
/* 마지막 원소에만 켠다 — 시퀀스 끝에 한 번만 받는다 */
seq_cfg.queue[i].seq_int_en = (i == (KEYS_SEQ_LEN - 1));
```

### ② `CONT_EN` 은 "연속 반복"이 아니라 "큐 끝까지 진행"이다

①을 고쳤는데도 타임아웃이 그대로였다. 진단 명령을 넣어 보니 **변환이 딱 하나만**
일어나고 멈춰 있었다.

```
cli# keys adc
ADC0
  트리거 후 INT_STS : 0x00000000 (spin 200000)
  ISR done flag     : 0
  DMA 버퍼          : 0x82F0A4E6 0xDEADBEEF 0xDEADBEEF 0xDEADBEEF
                       ^^^^^^^^^^ 첫 채널만 쓰였다
```

레지스터 설명이 답이었다.

> **CONT_EN** — *if set, HW will continue process the queue **till end(seq_len)** after trigger once*
> **RESTART_EN** — *if set **together with cont_en**, HW will continue process the whole queue after trigger once*

이름 때문에 `CONT_EN` 을 "끝나면 계속 반복"으로 읽고 껐는데, 그러면 **트리거 1회에
변환 1개**만 하고 선다. 반복은 `RESTART_EN` 쪽이다.

```c
seq_cfg.cont_en    = true;    /* 한 번 트리거 -> 큐 끝까지 */
seq_cfg.restart_en = false;   /* 끝나면 멈춘다 */
```

고친 뒤:

```
  트리거 후 INT_STS : 0x01000000 (spin 8)
  ISR done flag     : 1
  DMA 버퍼          : 0x82F0A528 0x86E1AEC5 0x8AC2A818 0x8E83AAA2
```

패킹된 워드를 풀어보면 `adc_ch` 가 15, 14, 12, 8 로 등록 순서 그대로고 `seq_num` 도
0,1,2,3 이다.

### ③ ADC 채널 번호는 패드 번호와 다르다

`PB00~PB07 → ch8~ch15`, `PB08~PB15 → ch0~ch7` 로 8만큼 돌아가 있다. 그래서 이 보드가
쓰는 8개는 오름차순도 아니고 `PB03` 을 건너뛴다.

```c
static const uint8_t adc0_seq_ch[4] = { 15, 14, 12,  8 };  /* PB07 PB06 PB04 PB00 */
static const uint8_t adc1_seq_ch[4] = {  0, 13,  9, 10 };  /* PB08 PB05 PB01 PB02 */
```

### ④ `PY` 패드는 IOC 만으로 연결되지 않는다

`IOC` 를 ALT0(GPIO)으로 잡아도 패드가 SoC 에 붙지 않는다. **`PIOC` 도 ALT3 으로** 함께
넘겨야 한다.

```c
HPM_IOC->PAD[IOC_PAD_PY00 + i].FUNC_CTL  = IOC_PY00_FUNC_CTL_GPIO_Y_00;
HPM_PIOC->PAD[IOC_PAD_PY00 + i].FUNC_CTL = IOC_PAD_FUNC_CTL_ALT_SELECT_SET(3);
```

---

## 남은 것

- [ ] `adc16_res_12_bits` 로 설정했는데 결과가 16비트 전 범위(약 42000~46000)로 나온다.
      상대 변화만 쓰는 6편에는 지장이 없지만, 분해능 설정이 실제로 무엇을 바꾸는지 확인
- [ ] 64셀 ↔ 실제 키 위치 매핑 (6편에서 키를 하나씩 눌러가며 표를 만든다)
