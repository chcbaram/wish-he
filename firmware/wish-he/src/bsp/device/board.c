/*
 * board.c
 *
 * hpm_sdk boards/hpm5300evk/board.c 에서 본 프로젝트에 필요한 부분만 추린 것이다.
 * 콘솔 초기화(board_init_console)는 제거했다 — UART0 은 src/hw/driver/uart.c 가 단독으로 소유한다.
 *
 * Copyright (c) 2023-2026 HPMicro
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "board.h"

#include "hpm_clock_drv.h"
#include "hpm_sysctl_drv.h"
#include "hpm_pllctlv2_drv.h"
#include "hpm_pcfg_drv.h"
#include "hpm_usb_drv.h"


/**
 * @brief FLASH configuration option definitions:
 * option[0]:
 *    [31:16] 0xfcf9 - FLASH configuration option tag
 *    [15:4]  0 - Reserved
 *    [3:0]   option words (exclude option[0])
 * option[1]:
 *    [31:28] Flash probe type
 *    [27:24] Command Pads after Power-on Reset
 *    [23:20] Command Pads after Configuring FLASH
 *    [19:16] Quad Enable Sequence
 *    [15:8]  Dummy cycles
 *    [7:4]   Misc.
 *    [3:0]   Frequency option
 * option[2]:
 *    [19:16] IO voltage
 *    [15:12] Pin group
 *    [11:8]  Connection selection
 *    [7:0]   Drive Strength
 *
 * ROM 부트로더가 0x80000400 에서 이 값을 읽어 XPI0 를 설정한다. 없으면 부팅하지 못한다.
 */
#if defined(FLASH_XIP) && FLASH_XIP
/*
 * option[1] 하위 4비트 = 주파수 옵션
 *   1:30MHz  2:50MHz  3:66MHz  4:80MHz  5:100MHz  6:120MHz  7:133MHz  8:166MHz
 * 5(100MHz) -> 6(120MHz).
 * 부팅 실패 시 BOOT0 스트랩으로 USB ISP 복구.
 */
__attribute__ ((section(".nor_cfg_option"), used)) const uint32_t option[4] = {0xfcf90002, 0x00000006, 0x1000, 0x0};
#endif




/*
 * PY 패드는 IOC 뿐 아니라 PIOC 도 함께 설정해야 SoC 기능이 연결된다.
 * 특정 주변장치에 속하지 않는 칩 레벨 설정이라 보드 계층에 둔다.
 */
static void board_init_py_pins(void)
{
    HPM_PIOC->PAD[IOC_PAD_PY00].FUNC_CTL = PIOC_PY00_FUNC_CTL_PGPIO_Y_00;
    HPM_PIOC->PAD[IOC_PAD_PY01].FUNC_CTL = PIOC_PY01_FUNC_CTL_PGPIO_Y_01;
    HPM_PIOC->PAD[IOC_PAD_PY02].FUNC_CTL = PIOC_PY02_FUNC_CTL_PGPIO_Y_02;
    HPM_PIOC->PAD[IOC_PAD_PY03].FUNC_CTL = PIOC_PY03_FUNC_CTL_PGPIO_Y_03;
    HPM_PIOC->PAD[IOC_PAD_PY04].FUNC_CTL = PIOC_PY04_FUNC_CTL_PGPIO_Y_04;
    HPM_PIOC->PAD[IOC_PAD_PY05].FUNC_CTL = PIOC_PY05_FUNC_CTL_PGPIO_Y_05;
}

/* 클럭 그룹0 에 항상 필요한 것만 등록한다. 주변장치별 클럭은 각 드라이버가 직접 켠다. */
static void board_init_clock_group(void)
{
    clock_add_to_group(clock_cpu0,    0);
    clock_add_to_group(clock_ahb,     0);
    clock_add_to_group(clock_lmm0,    0);
    clock_add_to_group(clock_mchtmr0, 0);
    clock_add_to_group(clock_rom,     0);
    clock_add_to_group(clock_gpio,    0);
    clock_add_to_group(clock_hdma,    0);
    clock_add_to_group(clock_xpi0,    0);
}

static void board_init_clock_source(void)
{
    /* PLL0 960MHz */
    pllctlv2_init_pll_with_freq(HPM_PLLCTLV2, PLLCTLV2_PLL_PLL0, 960000000);

    pllctlv2_set_postdiv(HPM_PLLCTLV2, PLLCTLV2_PLL_PLL0, pllctlv2_clk0, pllctlv2_div_1p0);  /* 960MHz */
    pllctlv2_set_postdiv(HPM_PLLCTLV2, PLLCTLV2_PLL_PLL0, pllctlv2_clk1, pllctlv2_div_1p6);  /* 600MHz */
    pllctlv2_set_postdiv(HPM_PLLCTLV2, PLLCTLV2_PLL_PLL0, pllctlv2_clk2, pllctlv2_div_2p4);  /* 400MHz */

    /* mchtmr = 24MHz */
    clock_set_source_divider(clock_mchtmr0, clk_src_osc24m, 1);
}


/*
 * 리셋 직후 PHY 에는 호스트측 DP/DM 풀다운(45ohm)이 걸려 있다.
 * 이걸 해제하지 않으면 호스트가 connect/idle 시그널링을 제대로 읽지 못해 열거에 실패한다.
 * PHY_CTRL0 를 건드리려면 USB 클럭이 살아 있어야 해서, XTAL 상태를 보고 임시로
 * 클럭을 붙였다 떼는 절차가 필요하다. (hpm_sdk boards/hpm5300evk/board.c 원본 그대로)
 *
 * 클럭 재설정(board_init_clock) 보다 반드시 먼저 호출해야 한다.
 */
void board_init_usb_dp_dm_pins(void)
{
    while (sysctl_resource_any_is_busy(HPM_SYSCTL)) {
        ;
    }
    if (pllctlv2_xtal_is_stable(HPM_PLLCTLV2) && pllctlv2_xtal_is_enabled(HPM_PLLCTLV2)) {
        if (clock_check_in_group(clock_usb0, 0)) {
            usb_phy_disable_dp_dm_pulldown(HPM_USB0);
        } else {
            clock_add_to_group(clock_usb0, 0);
            usb_phy_disable_dp_dm_pulldown(HPM_USB0);
            clock_remove_from_group(clock_usb0, 0);
        }
    } else {
        uint8_t tmp;
        tmp = sysctl_resource_target_get_mode(HPM_SYSCTL, sysctl_resource_xtal);
        sysctl_resource_target_set_mode(HPM_SYSCTL, sysctl_resource_xtal, 0x03);    /* NOLINT */
        clock_add_to_group(clock_usb0, 0);
        usb_phy_disable_dp_dm_pulldown(HPM_USB0);
        clock_remove_from_group(clock_usb0, 0);
        while (sysctl_resource_target_is_busy(HPM_SYSCTL, sysctl_resource_usb0)) {
            ;
        }
        sysctl_resource_target_set_mode(HPM_SYSCTL, sysctl_resource_xtal, tmp);
    }
}

/*
 * 디바이스 전용 초기화. SDK 원본의 usb_hcd_set_power_ctrl_polarity() + 100ms 지연은
 * 호스트 모드용이라 뺐다. PY00/PY01/PY02(ID/OC/PWR) 핀먹스도 디바이스에는 불필요하다.
 */
void board_init_usb(USB_Type *ptr)
{
    if (ptr == HPM_USB0) {
        clock_add_to_group(clock_usb0, 0);
    }
}


void board_init(void)
{
    board_init_py_pins();
    board_init_usb_dp_dm_pins();    /* 클럭 재설정 전에 와야 한다 */

    board_init_clock();
    board_init_pmp();
}

void board_init_clock(void)
{
    uint32_t cpu0_freq = clock_get_frequency(clock_cpu0);

    if (cpu0_freq == PLLCTL_SOC_PLL_REFCLK_FREQ) {
        /* Configure the External OSC ramp-up time: ~9ms */
        pllctlv2_xtal_set_rampup_time(HPM_PLLCTLV2, 32UL * 1000UL * 9U);

        /* Select clock setting preset1 */
        sysctl_clock_set_preset(HPM_SYSCTL, 2);
    }

    /* group0[0] */
    board_init_clock_group();

    /* Connect Group0 to CPU0 */
    clock_connect_group_to_cpu(0, 0);

    /*
     * PLL0    : 960MHz
     * PLL0CLK0: 960MHz / PLL0CLK1: 600MHz / PLL0CLK2: 400MHz
     * mchtmr  : 24MHz
     *
     * 원본(EVK)은 CPU 도메인을 먼저 붙이고 PLL 을 나중에 설정했다. 여기서는 순서를 뒤집는다.
     * 이 시점의 CPU 는 아직 24MHz(OSC) 라 PLL 을 먼저 확정하는 편이 안전하고,
     * PLL0CLK2(400MHz) 를 분주 없이 그대로 쓰려면 postdiv 가 먼저 정해져 있어야 한다.
     */
    board_init_clock_source();

    /*
     * VDD_SOC 는 건드리지 않는다 — 이 보드는 내장 DCDC 로 코어를 만들지 않는다.
     *
     * 실측: pcfg_dcdc_set_voltage(HPM_PCFG, 1175) 를 호출하면 VOLT 필드에 0x497(1175)
     * 이 들어가고 MODE 는 001(basic) 인데도 DCDC_MODE.READY(bit28) 가 영영 서지 않아
     * pcfg_dcdc_is_stable() 에서 무한 대기한다(PC 가 0x800259BC 에 고정).
     * 내부 DCDC 가 실제로 레귤레이션하지 못한다는 뜻이다.
     *
     * SDK 원본 주석이 경고하던 그대로다 —
     *   "When using an external DCDC, don't set the internal DCDC voltage.
     *    The following call of pcfg_dcdc_set_voltage() should be commented out."
     *
     * 값 자체는 1175 로 같지만 구버전 SDK 는 READY 를 기다리지 않아
     * 그냥 넘어갔던 것으로 보인다.
     *
     * 보드가 공급하는 전압 그대로 400MHz 로 돈다. 이 전압에서 400MHz 는 규격 안이므로
     * 하드웨어가 지원하는 동작점이다. 480MHz 로 올리려면 VDD_SOC 실측이 먼저다.
     */

    /* CPU 400MHz, AXI/AHB 133MHz */
    sysctl_config_cpu0_domain_clock(HPM_SYSCTL, clock_source_pll0_clk2, 1, 3);

    clock_update_core_clock();
}

/*
 * ── IAP 인계 정리 ──────────────────────────────────────────────────────────
 *
 * IAP 부트로더는 주변장치를 켜둔 채 앱으로 점프한다. 앱 진입 시점에 PLIC 에
 * GPTMR1(IRQ 6) 이 pending 으로 남아 있다 — IAP 가 리포트 주기를 GPTMR1 로 돌리다가
 * 끄지 않고 넘기기 때문이다.
 *
 * SDK 의 system_init() 은 이걸 청소하지 않고 곧바로 mstatus.MIE 를 열기 때문에,
 * 그대로 두면 등록되지 않은 벡터로 점프해 default_irq_handler(무한루프)에 빠진다.
 * ROM 이 직접 앱을 띄우는 EVK 에서는 겪지 않는 문제다.
 *
 * ★ enable 을 내리는 것만으로는 부족하다. 실측: enable word0 을 0 으로 만든 뒤에도
 *   claim 레지스터가 6(GPTMR1) 을 계속 내줬다. pending 자체를 claim/complete 로
 *   비워야 한다. 아래 세 가지를 모두 한다 — mie 클리어 / enable 클리어 / pending 배수.
 *
 * system_init() 이 weak 이므로 여기서 덮어쓴다.
 */
extern void enable_plic_feature(void);   /* hpm_sdk soc/.../system.c (static 아님) */


/* HPM5361 의 IRQ 는 71(DEBUG0)까지다. 여유를 두고 훑는다 — 없는 소스를 꺼도 무해하다. */
#define BOARD_PLIC_IRQ_MAX  128

void system_init(void)
{
#ifndef CONFIG_NOT_ENALBE_ACCESS_TO_CYCLE_CSR
    uint32_t mcounteren = read_csr(CSR_MCOUNTEREN);
    write_csr(CSR_MCOUNTEREN, mcounteren | 1);   /* MCYCLE 접근 허용 */
#endif

    disable_global_irq(CSR_MSTATUS_MIE_MASK);

    /*
     * ★ mie 를 통째로 지운다.
     *
     * start.S 는 mstatus 만 0 으로 만들고 mie 는 그대로 둔다. ROM 이 깨끗한 상태로
     * 넘겨주는 EVK 에서는 문제가 없지만, IAP 위에 얹으면 IAP 가 켜둔 비트(머신 타이머
     * MTIE 등)가 살아서 넘어온다. SDK 의 system_init() 은 bit11(MEIE)만 건드리므로
     * 그 잔재가 남고, mstatus.MIE 를 여는 순간 등록되지 않은 인터럽트가 곧장
     * default_irq_handler(무한루프)로 떨어진다.
     */
    write_csr(CSR_MIE, 0);

    /*
     * PLIC enable 도 전부 내린다. pending 은 남아 있어도 enable 이 없으면 CPU 로
     * 올라오지 않는다. 필요한 소스는 각 드라이버가 초기화할 때 다시 켠다.
     */
    for (uint32_t irq = 1; irq < BOARD_PLIC_IRQ_MAX; irq++)
    {
        intc_m_disable_irq(irq);
    }

    /*
     * ★ 남아 있는 pending 을 claim/complete 로 비운다.
     *
     * enable 만 내려서는 부족하다 — 실측하면 enable word0 이 0 인데도 claim 레지스터가
     * 6(GPTMR1) 을 내준다. IAP 가 남긴 pending 이 그대로 살아 있기 때문이다.
     * 이 상태로 mstatus.MIE 를 열면 등록되지 않은 벡터로 점프해 죽는다.
     */
    {
        volatile uint32_t *claim =
            (volatile uint32_t *)(HPM_PLIC_BASE + HPM_PLIC_CLAIM_OFFSET);

        for (uint32_t i = 0; i < BOARD_PLIC_IRQ_MAX; i++)
        {
            uint32_t id = *claim;

            if (id == 0)
            {
                break;
            }
            *claim = id;        /* complete */
        }
    }

    enable_plic_feature();
    enable_irq_from_intc();

    /*
     * 위에서 mie / PLIC enable / pending 을 모두 정리했으므로 SDK 원본과 같은
     * 시점에 전역 인터럽트를 연다. 덕분에 hw.c 는 EVK 원본과 동일하게 유지된다.
     */
    enable_global_irq(CSR_MSTATUS_MIE_MASK);
}


void board_init_pmp(void)
{
}

void board_ungate_mchtmr_at_lp_mode(void)
{
    /* Keep cpu clock on wfi, so that mchtmr irq can still work after wfi */
    sysctl_set_cpu_lp_mode(HPM_SYSCTL, BOARD_RUNNING_CORE, cpu_lp_mode_ungate_cpu_clock);
}

void board_delay_us(uint32_t us)
{
    clock_cpu_delay_us(us);
}

void board_delay_ms(uint32_t ms)
{
    clock_cpu_delay_ms(ms);
}
