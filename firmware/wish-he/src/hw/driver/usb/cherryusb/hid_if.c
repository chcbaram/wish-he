/*
 * hid_if.c  —  raw HID 설정 채널 (CherryUSB, HPM5361)
 *
 * 웹페이지에서 HID 명령으로 부트로더에 진입시킨다. IAP 가 기대하는 방식이다.
 * CDC 콘솔에 사람이 `reset boot` 를 치지 않아도 호스트 도구가 알아서 넘길 수 있다.
 *
 *   호스트 -> 장치 (OUT, 32 B)      장치 -> 호스트 (IN, 32 B)
 *     [0]    명령                     [0]    명령 에코
 *     [1..]  인자                     [1..]  인자 에코
 *                                     [..]   그 뒤부터 응답
 *
 * ★ 인자를 그대로 되돌려줘야 한다.
 *
 *   VIA 클라이언트는 응답이 `[명령, ...인자]` 로 시작하는지 검사하고, 아니면
 *   "Receiving incorrect response for command" 로 버린다. 처음에는 [1] 에 상태
 *   바이트를 넣었다가 그 검사에 걸렸다. 상태는 뺐다 — 실패는 무응답으로 드러난다.
 *
 * ★ 리셋은 콜백(ISR) 안에서 하지 않는다. 응답 IN 전송이 호스트에게 실제로 나가기 전에
 *   리셋해버리면 도구 쪽에서는 그냥 장치가 사라진 것으로 보인다. 그리고 부트 플래그
 *   기록은 ROM API 로 XIP 플래시를 건드리는 일이라 인터럽트 컨텍스트에서 할 일이 아니다.
 *   그래서 콜백은 예약만 하고 hidIfUpdate() 가 메인 루프에서 실행한다.
 *
 * ★ 라이브 트래킹은 여기서 내보내지 않는다.
 *
 *   전용 인터페이스(hid_trk_if.c, IF3)가 맡는다. 이 채널로 같이 내보내면 VIA 의
 *   요청-응답 짝이 어긋나기 때문이다 — VIA 는 명령 다음 IN 리포트를 응답이라고
 *   가정한다. 여기서는 0xC3 으로 켜고 끄기만 한다.
 */

#include "usb/cherryusb/hid_if.h"


#ifdef _USE_HW_USB
#if HW_USB_STACK == HW_USB_STACK_CHERRYUSB

#include "usbd_core.h"
#include "usbd_hid.h"
#include "usb/cherryusb/usb_desc.h"
#include "reset.h"
#include "keys.h"
#include "qmk.h"
#include "usb/cherryusb/hid_trk_if.h"


#define HID_BUSID           0

/* 응답이 호스트로 빠져나갈 시간을 주고 리셋한다. */
#define HID_ACTION_DELAY_MS 50


/* VIA 계열과 같은 벤더 정의 페이지. 호스트는 이 값으로 장치를 찾는다. */
static const uint8_t hid_report_desc[] = {
  0x06, 0x60, 0xFF,         /* Usage Page (Vendor Defined 0xFF60)  */
  0x09, 0x61,               /* Usage (0x61)                        */
  0xA1, 0x01,               /* Collection (Application)            */
  0x09, 0x62,               /*   Usage (0x62)                      */
  0x15, 0x00,               /*   Logical Minimum (0)               */
  0x26, 0xFF, 0x00,         /*   Logical Maximum (255)             */
  0x95, HID_EP_MPS,         /*   Report Count (32)                 */
  0x75, 0x08,               /*   Report Size (8)                   */
  0x81, 0x02,               /*   Input (Data, Var, Abs)            */
  0x09, 0x63,               /*   Usage (0x63)                      */
  0x15, 0x00,               /*   Logical Minimum (0)               */
  0x26, 0xFF, 0x00,         /*   Logical Maximum (255)             */
  0x95, HID_EP_MPS,         /*   Report Count (32)                 */
  0x75, 0x08,               /*   Report Size (8)                   */
  0x91, 0x02,               /*   Output (Data, Var, Abs)           */
  0xC0                      /* End Collection                      */
};

_Static_assert(sizeof(hid_report_desc) == HID_REPORT_DESC_SIZE,
               "HID_REPORT_DESC_SIZE 가 리포트 기술자 실제 크기와 다르다");


/* DMA 가 접근하는 스테이징 버퍼 */
USB_NOCACHE_RAM_SECTION USB_MEM_ALIGNX static uint8_t rx_report[HID_EP_MPS];
USB_NOCACHE_RAM_SECTION USB_MEM_ALIGNX static uint8_t tx_report[HID_EP_MPS];

enum
{
  ACTION_NONE = 0,
  ACTION_BOOT,
  ACTION_RESET,
};

static volatile bool     is_configured  = false;
static volatile bool     is_tx_busy     = false;
static volatile uint8_t  pending_action = ACTION_NONE;
static volatile uint32_t action_time    = 0;
static volatile uint32_t rx_count       = 0;

/*
 * 우리가 모르는 명령은 QMK/VIA 로 넘긴다.
 *
 * OUT 콜백(ISR)에서 바로 부르면 안 된다 — VIA 명령은 EEPROM 을 건드린다.
 * 그래서 복사만 해두고 hidIfUpdate() 가 메인 루프에서 실행한다.
 */
static hid_raw_recv_t    raw_recv    = NULL;
static uint8_t           raw_pending_buf[HID_EP_MPS];

/*
 * 보정 저장 요청.
 *
 * ★ ISR 에서 플래시를 쓰면 안 된다. keysCalSave() 는 2~3ms 걸린다. 요청만 세워
 *   두고 메인 루프(hidUpdate)가 처리한다 — 03편에서 부팅 경로에 플래시 작업을
 *   넣었다가 벽돌을 만든 뒤로 이 경계를 지킨다.
 */
static volatile bool     cal_save_req  = false;
static volatile uint8_t  cal_save_ok   = 0;
static volatile uint8_t  cal_save_skip = 0;
static volatile bool     raw_pending = false;

/*
 * 프로파일 전환도 같은 경계를 지킨다.
 *
 * 전환은 플래시에 "지금 몇 번" 을 남겨야 하므로 보정 저장과 똑같이 2~3ms 짜리
 * 작업이다. 125us 폴링에 그걸 물리면 키 입력이 밀린다.
 *
 * ★ 대신 응답에는 **바뀔 번호**를 바로 실어 준다.
 *
 *   메모리의 active 는 ISR 안에서 바꿔도 싸다. 느린 것은 플래시뿐이다. 그래서
 *   전환 자체는 즉시 일어나고 미루는 것은 "남기기" 뿐이다 — 도구가 다시 물을 일이
 *   없다.
 */
static volatile bool     prof_save_req = false;

/*
 * 보낼 자리가 없어 못 보낸 응답.
 *
 * ★ 조용히 버리면 호스트가 영영 기다린다.
 *
 *   응답은 IN 엔드포인트가 비어 있을 때만 보낼 수 있다. 그런데 플래시를 쓰는
 *   동안(6ms) 인터럽트가 꺼져 있으면 그 사이 전송 완료 인터럽트를 놓칠 수 있고,
 *   그러면 is_tx_busy 가 참인 채로 남는다. 그 상태에서 다음 명령이 오면 응답을
 *   만들어 놓고 **보내지 않고 버렸다** — 요청 하나에 응답 하나인 통로에서 하나가
 *   사라지니 호스트는 그대로 멈춘다.
 *
 *   실험으로 잡았다. 전환 없이 400번은 멀쩡한데, 저장이 도는 전환 뒤에는 일곱 번째
 *   왕복에서 응답이 사라졌다.
 *
 *   버리지 말고 들고 있다가 자리가 나면 보낸다.
 *
 * ★ 그런데 **너무 늦었으면 보내면 안 된다.** 늦게 가는 것이 안 가는 것보다 나쁘다.
 *
 *   이 통로에는 순번이 없다. 요청 하나에 응답 하나로만 짝을 맞춘다. 그래서 호스트가
 *   기다리다 포기하고 다음 명령을 보낸 뒤에 묵은 응답이 나가면, 호스트는 그것을
 *   **다음 명령의 응답으로 읽는다.** 그때부터 모든 응답이 한 칸씩 밀리고 재시도로도
 *   못 푼다 — 매번 "직전 것"이 오기 때문이다.
 *
 *   실제로 그렇게 됐다. 펌웨어를 굽고 나면 (a) 새 펌웨어가 부팅하며 플래시를 쓰고
 *   (b) 도구는 그 순간 값들을 몰아서 묻는다. 도구 로그에 이렇게 남았다 —
 *
 *     보냄 14 37 244 28   받음 1 0 12 …         <- 훨씬 앞선 명령의 응답
 *     보냄 14 38  16 28   받음 14 37 244 28 …   <- 직전 명령의 응답
 *
 *   그래서 자리가 났을 때 **묵었으면 버린다.** 응답이 사라지면 호스트는 그 한 번만
 *   실패하고 재시도로 회복하지만, 어긋나면 회복할 길이 없다.
 */
#define HID_TX_PENDING_MAX_MS   20      /* 정상은 1ms 안에 나간다 */

static volatile bool     tx_pending    = false;
static volatile uint32_t tx_pending_ms = 0;
static volatile uint32_t tx_busy_ms    = 0;

/*
 * 응답을 만드는 자리와 실제로 나가는 자리를 갈라 둔다.
 *
 * ★ 예전에는 둘 다 tx_report 였다. 나가 있는 중에 다음 응답을 만들면 **DMA 가
 *   읽는 중인 버퍼를 덮어쓴다** — 호스트가 반쯤 섞인 프레임을 받는다. 창이 좁아
 *   안 걸렸을 뿐이다.
 *
 *   tx_stage 에 만들고, 자리가 비어 있으면 tx_report 로 옮겨 싣는다. 차 있으면
 *   tx_pend_buf 에 들고 있다가 완료 콜백에서 옮긴다.
 */
static uint8_t           tx_stage[HID_EP_MPS];
static uint8_t           tx_pend_buf[HID_EP_MPS];

/*
 * 들고 있던 응답을 지금 보낼지 정한다. 묵었으면 버린다.
 * 어느 쪽이든 tx_pending 은 내려간다.
 */
static bool hidTxPendingTake(void)
{
  if (tx_pending == false)
  {
    return false;
  }
  tx_pending = false;
  if ((millis() - tx_pending_ms) > HID_TX_PENDING_MAX_MS) return false;

  memcpy(tx_report, tx_pend_buf, HID_EP_MPS);
  return true;
}


/*
 * 응답 하나를 내보낸다. 자리가 비었으면 지금, 차 있으면 들고 있다가 완료 콜백에서.
 *
 * ★ 인터럽트가 막힌 상태이거나 ISR 안에서 불러야 한다.
 */
static void hidTxSubmit(const uint8_t *p_data, uint32_t length)
{
  if (length > HID_EP_MPS) length = HID_EP_MPS;

  if (is_tx_busy)
  {
    memset(tx_pend_buf, 0, HID_EP_MPS);
    memcpy(tx_pend_buf, p_data, length);
    tx_pending    = true;
    tx_pending_ms = millis();
    return;
  }

  memset(tx_report, 0, HID_EP_MPS);
  memcpy(tx_report, p_data, length);
  is_tx_busy = true;
  tx_busy_ms = millis();
  if (usbd_ep_start_write(HID_BUSID, HID_IN_EP, tx_report, HID_EP_MPS) != 0)
  {
    is_tx_busy = false;      /* 못 실었다 — 되살리기가 아니라 그냥 잃는다 */
  }
}

static struct usbd_interface hid_intf;




/*---------------------------------------------------------------------------
 *  명령 처리
 *---------------------------------------------------------------------------*/

/*
 * 명령별 인자 길이 (명령 바이트 제외).
 *
 * 응답에서 인자 뒤가 곧 페이로드 자리다. 실패할 수 있는 명령이 생기면 그 첫
 * 바이트를 상태로 쓰면 된다 — 지금 명령들은 실패할 여지가 없어 안 쓴다.
 */
/*
 * 되돌려 줄 인자 길이.
 *
 * ★ 보낸 만큼 그대로 돌려줘야 한다.
 *
 *   VIA 클라이언트는 응답이 [명령, 보낸 인자...] 로 시작하는지 확인하고, 안 맞으면
 *   오류로 버린다. 키별 설정 쓰기는 인자가 16바이트인데 2바이트만 돌려주다가
 *   전부 오류가 났다. 읽기는 인자가 2바이트뿐이라 통과해서, 읽기만 되고 쓰기가
 *   안 되는 모양으로 나타났다.
 *
 *   그래서 명령만 보고는 정할 수 없다 — 같은 명령이라도 하위 명령에 따라 다르다.
 */
static uint32_t hidCmdArgLen(const uint8_t *p_rx)
{
  switch (p_rx[0])
  {
    case HID_CMD_LAYOUT: return 1;   /* 시작 인덱스 */
    case HID_CMD_TRACK:  return 1;   /* on/off */
    case HID_CMD_SWITCH: return 1;   /* 인덱스 */
    /* 상태만 물으면 하위 하나, 바꾸는 것은 인덱스가 더 붙는다 */
    case HID_CMD_PROF:   return (p_rx[1] == HID_PROF_STATUS) ? 1 : 2;
    /* 둘 다 [하위, 페이지] 를 받는다 — 한 프레임에 다 안 들어가서 나눈다 */
    case HID_CMD_HWINFO: return 2;
    case HID_CMD_STAT:   return 2;

    /* 하위 명령. 행정 읽기만 시작 인덱스가 더 붙는다 */
    case HID_CMD_CAL:    return (p_rx[1] == HID_CAL_STROKE) ? 2 : 1;

    case HID_CMD_KEYCFG:
      /* get: [하위, idx]   set: [하위, idx] + 값 */
      return (p_rx[1] == HID_KEYCFG_SET) ? (2 + HID_KEYCFG_LEN) : 2;

    /*
     * 커스텀 스위치. 상태만 물으면 하위 하나, 슬롯을 다루면 번호가 더 붙는다.
     *
     * ★ 쓰기 에코에 값을 다 싣지 않는다. 이름까지 되돌리면 19바이트가 에코라
     *   32바이트에서 남는 자리가 없다. 하위와 슬롯만 맞으면 짝은 지켜진다.
     */
    case HID_CMD_SWCUST:
      return (p_rx[1] == HID_SWCUST_INFO) ? 1 : 2;

    default:             return 0;
  }
}

/*
 * ISR 컨텍스트. 플래시나 긴 작업은 여기서 하지 않는다.
 *
 * 우리가 아는 명령이면 응답을 채우고 true 를 준다. 모르면 false — 부른 쪽이
 * QMK/VIA 로 넘긴다.
 */
static bool hidCmdHandler(const uint8_t *p_rx, uint8_t *p_tx)
{
  uint8_t cmd = p_rx[0];

  /*
   * 인자를 되돌리고 그 뒤는 지운다.
   *
   * 통째로 memcpy 하면 호스트가 보낸 쓰레기가 응답 꼬리에 섞여 나와 디버깅할 때
   * 헷갈린다. 명령마다 인자 길이가 정해져 있으므로 딱 그만큼만 에코한다.
   */
  memset(p_tx, 0, HID_EP_MPS);
  memcpy(p_tx, p_rx, hidCmdArgLen(p_rx) + 1);

  switch (cmd)
  {
    /*
     * 보드 정보.
     *
     *   [1..]                보드 이름 (NUL 종료)
     *   [HID_INFO_VER_OFF..] 펌웨어 버전 (NUL 종료)
     *
     * ★ 버전을 **고정 자리**에 둔다. 이름 뒤에 이어 붙이면 이름 길이가 바뀔 때마다
     *   버전 자리가 밀려, 이름이 다른 보드에서 도구가 엉뚱한 바이트를 읽는다.
     */
    case HID_CMD_INFO:
    {
      const char *p_name = _DEF_BOARD_NAME;
      const char *p_ver  = _DEF_FIRMWATRE_VERSION;

      for (uint32_t i = 0; i < (HID_INFO_VER_OFF - 1) && p_name[i]; i++)
      {
        p_tx[1 + i] = (uint8_t)p_name[i];
      }
      for (uint32_t i = 0; i < (HID_EP_MPS - HID_INFO_VER_OFF - 1) && p_ver[i]; i++)
      {
        p_tx[HID_INFO_VER_OFF + i] = (uint8_t)p_ver[i];
      }
      break;
    }

    case HID_CMD_BOOT:
      pending_action = ACTION_BOOT;
      action_time    = millis();
      break;

    case HID_CMD_RESET:
      pending_action = ACTION_RESET;
      action_time    = millis();
      break;

    /*
     * 물리 배치 읽기 — 페이지 방식.
     *
     *   OUT [1] = 시작 인덱스
     *   IN  [1] = 시작 인덱스(에코)   [2] = 이 응답의 개수
     *       [3..] = {x, y, w, h, row, col} x 개수
     *
     * 응답에 전체 개수를 안 싣는 대신, 끝을 넘겨 물으면 개수 0 이 온다. 호스트는
     * 0 이 올 때까지 인덱스를 늘리면 된다.
     */
    case HID_CMD_KEYCFG:
    {
      uint32_t idx = p_rx[2];

      if (p_rx[1] == HID_KEYCFG_SET)
      {
        /*
         * ISR 이라 플래시는 건드리지 않는다. RAM 설정만 바꾸고 즉시 판정에 반영된다 —
         * 저장은 VIA 의 save 명령이 따로 한다.
         */
        keysSetKeyCfg(idx, &p_rx[HID_KEYCFG_OFF], HID_EP_MPS - HID_KEYCFG_OFF);
      }
      else
      {
        keysGetKeyCfg(idx, &p_tx[HID_KEYCFG_OFF], HID_EP_MPS - HID_KEYCFG_OFF);
      }
      return true;
    }

    case HID_CMD_LAYOUT:
    {
      uint32_t start = p_rx[1];
      uint32_t total = keysGetLayoutCount();
      uint32_t n     = 0;

      while (n < HID_LAYOUT_PER_FRAME && (start + n) < total)
      {
        const uint8_t *p_geo = keysGetLayoutEntry(start + n);
        uint32_t       o     = HID_LAYOUT_OFF + n * HID_LAYOUT_ENTRY;

        for (uint32_t k = 0; k < HID_LAYOUT_ENTRY; k++) p_tx[o + k] = p_geo[k];
        n++;
      }

      p_tx[2] = (uint8_t)n;
      break;
    }

    /*
     * 보정 — 시작·상태·저장·취소.
     *
     * 표본 수집은 keysUpdate() 안에서 돈다. 여기서는 켜고 끄고 진행 상황만 준다 —
     * ISR 컨텍스트라 오래 걸리는 일을 하면 안 된다.
     *
     * ★ 저장은 플래시를 쓴다. keysCalSave() 가 2~3ms 걸리므로 ISR 에서 부르면
     *   안 된다. 그래서 요청만 세워 두고 메인 루프가 처리한다.
     */
    /*
     * 하드웨어·펌웨어 제원. 전부 상수라 ISR 에서 읽어도 된다.
     */
    case HID_CMD_HWINFO:
    {
      extern uint32_t _fw_flash_begin;
      extern uint32_t __etext;

      uint32_t fw_begin = (uint32_t)&_fw_flash_begin;
      uint32_t page     = p_rx[2];
      uint32_t v[HID_HW_CNT];

      v[0]  = clock_get_frequency(clock_cpu0);
      v[1]  = (uint32_t)&__etext - fw_begin;   /* 펌웨어 크기 (코드+상수) */
      v[2]  = fw_begin;
      v[3]  = HW_FLASH_APP_SIZE;
      v[4]  = TOTAL_EEPROM_BYTE_COUNT;
      v[5]  = HW_FLASH_CAL_A;
      v[6]  = HW_FLASH_SET_A;
      v[7]  = keysGetKeyCount();
      v[8]  = MATRIX_ROWS;
      v[9]  = MATRIX_COLS;
      v[10] = DYNAMIC_KEYMAP_LAYER_COUNT;
      v[11] = RGB_MATRIX_LED_COUNT;

      if (page == HID_HW_PAGE_MCU || page == HID_HW_PAGE_AUTHOR)
      {
        const char *p_str = (page == HID_HW_PAGE_MCU) ? _DEF_MCU_NAME
                                                      : _DEF_AUTHOR_NAME;

        for (uint32_t i = 0; i < (HID_EP_MPS - HID_HW_OFF - 1) && p_str[i]; i++)
        {
          p_tx[HID_HW_OFF + i] = (uint8_t)p_str[i];
        }
        break;
      }

      for (uint32_t i = 0; i < HID_HW_PER_PAGE; i++)
      {
        uint32_t k = page * HID_HW_PER_PAGE + i;
        uint32_t o = HID_HW_OFF + i * 4;

        if (k >= HID_HW_CNT) break;

        p_tx[o + 0] = (uint8_t)(v[k] >>  0);
        p_tx[o + 1] = (uint8_t)(v[k] >>  8);
        p_tx[o + 2] = (uint8_t)(v[k] >> 16);
        p_tx[o + 3] = (uint8_t)(v[k] >> 24);
      }
      break;
    }

    /*
     * 진단 통계. 읽기만 하므로 ISR 에서 해도 된다 — 플래시도 긴 계산도 없다.
     */
    case HID_CMD_STAT:
    {
      keys_stat_t k;
      qmk_stat_t  q;
      uint32_t    v[HID_STAT_CNT];

      /*
       * ★ 지우고 나서 읽는다.
       *
       *   지운 뒤의 값을 그대로 돌려주면 화면이 한 번 더 물을 필요가 없다. 따로
       *   물으면 그 사이에 몇 회가 더 쌓여 "지웠는데 0 이 아닌" 화면이 나온다.
       */
      if (p_rx[1] == HID_STAT_CLEAR)
      {
        keysClearStat();
        qmkClearStat();
      }

      keysGetStat(&k);
      qmkGetStat(&q);

      v[0]  = k.scan_us;       v[1]  = k.scan_us_max;
      v[2]  = k.scan_over;     v[3]  = k.scan_cnt;
      v[4]  = k.timeout;       v[5]  = k.cal_ms;
      v[6]  = k.calibrated;
      v[7]  = q.task_us;       v[8]  = q.task_us_max;
      v[9]  = q.task_us_avg;   v[10] = q.task_over;
      v[11] = q.task_cnt;
      v[12] = q.rgb_us_max;    v[13] = q.rgb_us_avg;

      for (uint32_t i = 0; i < HID_STAT_PER_PAGE; i++)
      {
        uint32_t k = (uint32_t)p_rx[2] * HID_STAT_PER_PAGE + i;
        uint32_t o = HID_STAT_OFF + i * 4;

        if (k >= HID_STAT_CNT) break;

        p_tx[o + 0] = (uint8_t)(v[k] >>  0);
        p_tx[o + 1] = (uint8_t)(v[k] >>  8);
        p_tx[o + 2] = (uint8_t)(v[k] >> 16);
        p_tx[o + 3] = (uint8_t)(v[k] >> 24);
      }
      break;
    }

    case HID_CMD_PROF:
    {
      switch (p_rx[1])
      {
        /* 갈아 끼우기는 메모리에서 즉시, 플래시에 남기는 것만 미룬다 */
        case HID_PROF_SET:
          if (keysProfSelect(p_rx[2])) prof_save_req = true;
          break;

        case HID_PROF_COPY:
          if (keysProfCopy(p_rx[2])) prof_save_req = true;
          break;

        default: break;                       /* 상태만 */
      }

      p_tx[2] = keysProfGet();
      p_tx[3] = keysProfCount();
      break;
    }

    case HID_CMD_CAL:
    {
      switch (p_rx[1])
      {
        /*
         * ★ 시작·취소는 지난 저장 결과를 지운다.
         *
         *   안 지우면 한 번 저장한 뒤로 `cal_save_ok` 가 계속 1 이라, 취소해도
         *   도구가 "저장됨" 으로 표시한다. 이 값은 **이번 회차의 결과**여야 한다.
         */
        case HID_CAL_START:
          keysCalStart();
          cal_save_ok = cal_save_skip = 0;
          break;

        case HID_CAL_CANCEL:
          keysCalCancel();
          cal_save_ok = cal_save_skip = 0;
          break;

        case HID_CAL_SAVE:   cal_save_req = true; break;

        /*
         * 진행 중인 행정. 상태 응답과 자리가 겹치므로 여기서 끝낸다.
         *
         *   IN [2] = 시작 인덱스   [3] = 개수   [4..] = LE16 값들
         */
        case HID_CAL_STROKE:
        {
          uint16_t v[HID_CAL_STROKE_MAX];
          uint32_t n = keysCalStrokes(p_rx[2], v, HID_CAL_STROKE_MAX);

          p_tx[2] = p_rx[2];
          p_tx[3] = (uint8_t)n;
          for (uint32_t i = 0; i < n; i++)
          {
            p_tx[HID_CAL_STROKE_OFF + i * 2 + 0] = (uint8_t)(v[i] & 0xFF);
            p_tx[HID_CAL_STROKE_OFF + i * 2 + 1] = (uint8_t)(v[i] >> 8);
          }
          return true;
        }

        default: break;                       /* 상태만 */
      }

      p_tx[2] = keysCalIsActive() ? 1 : 0;
      p_tx[3] = (uint8_t)keysCalDone();
      p_tx[4] = (uint8_t)keysCalTotal();
      p_tx[5] = cal_save_ok;
      p_tx[6] = cal_save_skip;
      keysCalBitmap(&p_tx[HID_CAL_MAP_OFF], HID_EP_MPS - HID_CAL_MAP_OFF);
      break;
    }

    /*
     * 스위치 종류표 — 한 번에 하나씩 준다.
     *
     * 항목이 열 개도 안 되므로 페이지를 나눌 것 없이 인덱스로 하나씩 묻는다.
     * 이름이 문자열이라 한 프레임에 여러 개를 넣으면 자리 계산이 지저분해진다.
     */
    case HID_CMD_SWITCH:
    {
      uint32_t    idx   = p_rx[1];
      uint32_t    total = keysGetSwitchCount();
      const char *p_nm;
      uint16_t    um;

      p_tx[2] = (uint8_t)total;
      p_tx[3] = (uint8_t)keysGetSwitchGenericCount();

      if (idx >= total) break;          /* 범위 밖 — 개수만 알려주고 끝 */

      um      = keysGetSwitchTravelUm(idx);
      p_tx[4] = (uint8_t)(um & 0xFF);
      p_tx[5] = (uint8_t)(um >> 8);

      {
        uint16_t r = keysGetSwitchFluxRest(idx);
        uint16_t b = keysGetSwitchFluxBottom(idx);

        p_tx[HID_SWITCH_FLUX_OFF + 0] = (uint8_t)(r & 0xFF);
        p_tx[HID_SWITCH_FLUX_OFF + 1] = (uint8_t)(r >> 8);
        p_tx[HID_SWITCH_FLUX_OFF + 2] = (uint8_t)(b & 0xFF);
        p_tx[HID_SWITCH_FLUX_OFF + 3] = (uint8_t)(b >> 8);
      }

      p_nm = keysGetSwitchName(idx);
      for (uint32_t i = 0; i < (HID_EP_MPS - HID_SWITCH_NAME_OFF - 1) && p_nm[i]; i++)
      {
        p_tx[HID_SWITCH_NAME_OFF + i] = (uint8_t)p_nm[i];
      }
      break;
    }

    /*
     * 커스텀 스위치 슬롯 — 표에 없는 스위치를 사용자가 정의한다.
     *
     * ★ 여기서 플래시를 쓰지 않는다. 이 함수는 USB OUT ISR 이라 6ms 를 잡아먹으면
     *   리포트가 빠진다. keysSwCustomSet 은 RAM 만 고치고 표시만 남기며, 굽는 것은
     *   메인 루프의 keysSwUpdate 가 한다 — 0xC7·0xC8 과 같은 방식이다.
     */
    case HID_CMD_SWCUST:
    {
      uint32_t sub  = p_rx[1];
      uint32_t slot = p_rx[2];

      if (sub == HID_SWCUST_INFO)
      {
        p_tx[2] = (uint8_t)keysSwCustomCount();

        /* 이름이 붙은 칸 = 사용자가 실제로 고친 칸 */
        p_tx[3] = 0;
        for (uint32_t i = 0; i < keysSwCustomCount() && i < 8; i++)
        {
          keys_sw_info_t info;

          if (keysSwCustomGet(i, &info) && info.name[0] != 0) p_tx[3] |= (uint8_t)(1U << i);
        }
        break;
      }

      if (sub == HID_SWCUST_SET)
      {
        keys_sw_info_t info;

        memset(&info, 0, sizeof(info));
        info.travel_um      = (uint16_t)(p_rx[HID_SWCUST_TRAVEL_OFF]
                                       | (p_rx[HID_SWCUST_TRAVEL_OFF + 1] << 8));
        info.flux_rest_gs   = (uint16_t)(p_rx[HID_SWCUST_REST_OFF]
                                       | (p_rx[HID_SWCUST_REST_OFF + 1] << 8));
        info.flux_bottom_gs = (uint16_t)(p_rx[HID_SWCUST_BOTTOM_OFF]
                                       | (p_rx[HID_SWCUST_BOTTOM_OFF + 1] << 8));
        info.kind           = p_rx[HID_SWCUST_KIND_OFF];

        for (uint32_t i = 0; i < sizeof(info.name) - 1; i++)
        {
          info.name[i] = (char)p_rx[HID_SWCUST_NAME_OFF + i];
          if (info.name[i] == 0) break;
        }
        keysSwCustomSet(slot, &info);
        /* 되읽어 실제로 들어간 값을 응답에 싣는다 — 잘렸는지 화면이 안다 */
      }

      {
        keys_sw_info_t info;

        if (keysSwCustomGet(slot, &info) == false) break;

        p_tx[HID_SWCUST_TRAVEL_OFF + 0] = (uint8_t)(info.travel_um & 0xFF);
        p_tx[HID_SWCUST_TRAVEL_OFF + 1] = (uint8_t)(info.travel_um >> 8);
        p_tx[HID_SWCUST_REST_OFF + 0]   = (uint8_t)(info.flux_rest_gs & 0xFF);
        p_tx[HID_SWCUST_REST_OFF + 1]   = (uint8_t)(info.flux_rest_gs >> 8);
        p_tx[HID_SWCUST_BOTTOM_OFF + 0] = (uint8_t)(info.flux_bottom_gs & 0xFF);
        p_tx[HID_SWCUST_BOTTOM_OFF + 1] = (uint8_t)(info.flux_bottom_gs >> 8);
        p_tx[HID_SWCUST_KIND_OFF]       = info.kind;

        for (uint32_t i = 0; i < sizeof(info.name) && info.name[i]; i++)
        {
          p_tx[HID_SWCUST_NAME_OFF + i] = (uint8_t)info.name[i];
        }
      }
      break;
    }

    /*
     * 라이브 트래킹 on/off.
     *
     *   OUT [1] = 1 시작 / 0 정지
     *   IN  [1] = 에코   [2] = 전체 키 수   [3] = 프레임당 키 수
     *       [4..5] = 전 행정 (LE16, 0.01mm)
     *
     * 전 행정을 같이 주는 것은 호스트가 막대를 몇 mm 짜리로 그릴지 정하기 위해서다.
     * 스위치 종류가 바뀌면 따라가야 하므로 도구에 상수로 박으면 안 된다.
     */
    case HID_CMD_TRACK:
    {
      uint16_t travel = keysGetTravelUm(0, 0);

      hidTrkSetEnable(p_rx[1] != 0);
      p_tx[2] = KEYS_MAX;
      p_tx[3] = TRK_PER_FRAME;
      p_tx[4] = (uint8_t)(travel & 0xFF);
      p_tx[5] = (uint8_t)(travel >> 8);
      break;
    }

    /* 우리 명령이 아니다 — VIA 일 수 있으니 넘긴다 */
    default:
      return false;
  }

  return true;
}

/*
 * VIA 응답을 보낸다. raw_hid_send() 가 부른다 — 메인 루프 컨텍스트다.
 *
 * ★ 여기서 기다리면 안 된다.
 *
 *   예전에는 자리가 빌 때까지 최대 100ms 를 돌았다. 그동안 메인 루프가 멈추므로
 *   keysUpdate() 도 qmkUpdate() 도 안 돈다 — **설정 도구를 열어 둔 채 타건하면
 *   그 순간 키가 씹힌다.** 스캔이 26us 마다 도는 루프에서 100ms 는 4000 바퀴다.
 *
 *   그리고 기다려서 늦게 보내는 것 자체가 틀렸다. 이 통로에는 순번이 없어 요청
 *   하나에 응답 하나로만 짝을 맞추므로, 호스트가 포기한 뒤에 나간 응답은 **다음
 *   명령의 응답으로 읽힌다** — 그때부터 모든 응답이 한 칸씩 밀린다 (16편).
 *   같은 이유로 OUT 쪽은 이미 "묵으면 버린다"로 가 있었는데 이쪽만 안 맞았다.
 *
 *   그래서 자리가 차 있으면 들고만 있는다. 20ms 안에 자리가 나면 나가고, 아니면
 *   버린다. 버려진 응답은 호스트가 그 한 번만 실패하고 재시도로 회복한다.
 */
void hidIfSendRaw(const uint8_t *p_data, uint8_t length)
{
  uint32_t mask;

  if (is_configured == false || p_data == NULL) return;

  mask = disable_global_irq(CSR_MSTATUS_MIE_MASK);
  hidTxSubmit(p_data, length);
  restore_global_irq(mask);
}

void hidIfSetRawReceiveFunc(hid_raw_recv_t func)
{
  raw_recv = func;
}

/* 메인 루프에서 부른다. 미뤄둔 VIA 명령과 예약된 리셋을 실제로 수행한다. */
void hidIfUpdate(void)
{
  /*
   * 보정 저장 — ISR 이 아니라 여기서 한다.
   *
   * keysCalSave() 는 플래시를 쓰고 2~3ms 걸린다. ISR 에서 부르면 USB 가 밀린다.
   * 03편에서 부팅 경로에 플래시 작업을 넣었다가 벽돌을 만든 뒤로 이 경계를 지킨다.
   */
  if (cal_save_req)
  {
    uint32_t done = 0, skip = 0;

    cal_save_req  = false;
    cal_save_ok   = keysCalSave(&done, &skip) ? 1 : 0;
    cal_save_skip = (uint8_t)skip;
    logPrintf("[  ] 보정 저장 %d 키, %d 건너뜀, %s\n",
              (int)done, (int)skip, cal_save_ok ? "OK" : "실패");
  }

  /*
   * ★ 놓친 전송 완료를 되살린다.
   *
   *   플래시를 쓰는 동안 인터럽트가 꺼져 있으면 전송 완료 인터럽트를 놓칠 수 있다.
   *   그러면 is_tx_busy 가 참인 채로 굳고, 그 뒤로는 응답을 한 번도 못 보낸다 —
   *   호스트에서는 키보드가 죽은 것처럼 보인다.
   *
   *   자리가 났는지 아닌지는 여기서 알 길이 없으므로 **시간으로 푼다.** 한 번의
   *   전송은 1ms 안에 끝난다. 50ms 가 지나도 안 끝났으면 인터럽트를 놓친 것이다.
   */
  if (is_tx_busy && (millis() - tx_busy_ms) > 50)
  {
    logPrintf("[  ] HID 전송 완료를 놓쳤다 — 되살린다\n");
    is_tx_busy = false;

    if (hidTxPendingTake())
    {
      is_tx_busy = true;
      tx_busy_ms = millis();
      usbd_ep_start_write(HID_BUSID, HID_IN_EP, tx_report, HID_EP_MPS);
    }
  }

  if (prof_save_req)
  {
    prof_save_req = false;
    logPrintf("[  ] 프로파일 %d 저장 %s\n", (int)keysProfGet() + 1,
              keysProfSave() ? "OK" : "실패");
  }

  if (raw_pending)
  {
    raw_pending = false;
    if (raw_recv != NULL) raw_recv(raw_pending_buf, HID_EP_MPS);

    /* 다 처리했으니 이제 다음 명령을 받는다 (위 OUT 콜백의 흐름제어) */
    usbd_ep_start_read(HID_BUSID, HID_OUT_EP, rx_report, HID_EP_MPS);
  }

  if (pending_action == ACTION_NONE) return;

  if (millis() - action_time < HID_ACTION_DELAY_MS) return;

  switch (pending_action)
  {
    case ACTION_BOOT:
      logPrintf("[  ] HID 부트로더 진입 요청\n");
      pending_action = ACTION_NONE;
      resetToBoot();              /* 플래그 기록에 실패하면 그냥 돌아온다 */
      logPrintf("[E_] 부트 플래그 기록 실패\n");
      break;

    case ACTION_RESET:
      pending_action = ACTION_NONE;
      resetToReset();             /* 돌아오지 않는다 */
      break;

    default:
      pending_action = ACTION_NONE;
      break;
  }
}




/*---------------------------------------------------------------------------
 *  엔드포인트 콜백 (ISR)
 *---------------------------------------------------------------------------*/
static void hidOutCallback(uint8_t busid, uint8_t ep, uint32_t nbytes)
{
  (void)ep;

  if (nbytes > 0)
  {
    rx_count++;

    if (hidCmdHandler(rx_report, tx_stage))
    {
      /* 자리가 차 있으면 들고 있다가 보낸다 — 단 20ms 안에 (hidTxPendingTake) */
      hidTxSubmit(tx_stage, HID_EP_MPS);
    }
    else if (raw_recv != NULL)
    {
      /* 모르는 명령 — 메인 루프로 미룬다. VIA 는 EEPROM 을 건드린다 */
      memcpy(raw_pending_buf, rx_report, HID_EP_MPS);
      raw_pending = true;

      /*
       * ★ 여기서는 **재무장하지 않는다.** 흐름제어가 필요하다.
       *
       *   예전에는 `raw_pending` 이 이미 참이면 새 명령을 조용히 버리고 곧바로
       *   재무장했다. 요청 하나에 응답 하나인 통로에서 요청이 사라지면, 그 뒤로
       *   **모든 응답이 한 칸씩 밀린다** — 호스트는 A 의 응답을 B 의 것으로 읽고,
       *   재시도로도 못 푼다. 실제로 굽고 난 뒤 "Receiving incorrect response" 가
       *   끝없이 나고 화면이 로딩에서 멈췄다.
       *
       *   OUT 을 재무장하지 않으면 컨트롤러가 다음 OUT 을 NAK 한다. 호스트는
       *   그냥 기다렸다 다시 보낸다 — USB 가 원래 그러라고 만든 장치다.
       *   재무장은 hidIfUpdate() 가 그 명령을 처리한 뒤에 한다.
       */
      return;
    }
  }

  usbd_ep_start_read(busid, HID_OUT_EP, rx_report, HID_EP_MPS);
}

static void hidInCallback(uint8_t busid, uint8_t ep, uint32_t nbytes)
{
  (void)ep;
  (void)nbytes;

  is_tx_busy = false;

  /* 자리가 났다 — 들고 있던 응답이 아직 쓸 만하면 지금 보낸다 */
  if (hidTxPendingTake())
  {
    is_tx_busy = true;
    tx_busy_ms = millis();
    usbd_ep_start_write(busid, HID_IN_EP, tx_report, HID_EP_MPS);
  }
}

static struct usbd_endpoint hid_out_ep = { .ep_addr = HID_OUT_EP, .ep_cb = hidOutCallback };
static struct usbd_endpoint hid_in_ep  = { .ep_addr = HID_IN_EP,  .ep_cb = hidInCallback  };




/*---------------------------------------------------------------------------
 *  스택 이벤트 / 공개 API
 *---------------------------------------------------------------------------*/
void hidIfEventHandler(uint8_t busid, uint8_t event)
{
  switch (event)
  {
    case USBD_EVENT_RESET:
    case USBD_EVENT_DISCONNECTED:
      is_configured = false;
      is_tx_busy    = false;
      break;

    case USBD_EVENT_CONFIGURED:
      is_configured = true;
      is_tx_busy    = false;
      usbd_ep_start_read(busid, HID_OUT_EP, rx_report, HID_EP_MPS);
      break;

    default:
      break;
  }
}

bool hidIfInit(void)
{
  is_configured  = false;
  is_tx_busy     = false;
  pending_action = ACTION_NONE;
  rx_count       = 0;

  usbd_add_interface(HID_BUSID,
                     usbd_hid_init_intf(HID_BUSID, &hid_intf,
                                        hid_report_desc, sizeof(hid_report_desc)));
  usbd_add_endpoint(HID_BUSID, &hid_out_ep);
  usbd_add_endpoint(HID_BUSID, &hid_in_ep);

  return true;
}

bool hidIfIsConfigured(void)
{
  return is_configured;
}

bool hidIfIsTracking(void)
{
  return hidTrkIsEnabled();
}

uint32_t hidIfGetRxCount(void)
{
  return rx_count;
}

uint32_t hidIfGetTrackCount(void)
{
  return hidTrkGetFrameCount();
}

#endif
#endif
