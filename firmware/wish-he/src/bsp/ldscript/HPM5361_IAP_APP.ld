/*
 * Copyright (c) 2023,2025,2026 HPMicro
 * SPDX-License-Identifier: BSD-3-Clause
 */

/*
 * HPM5361_FLASH_XIP.ld 의 IAP 앱 판.
 *
 * 보드의 IAP 부트로더 위에 얹는 앱이므로 ROM 부팅 구조를 우리가 만들지 않는다.
 *   - FLASH ORIGIN 을 0x80020000 으로, LENGTH 를 384KB 로 묶는다
 *     (0x80080000 부터의 EEPROM 에뮬레이션 영역을 침범하지 않기 위함)
 *   - .nor_cfg_option / .boot_header 는 IAP 가 이미 갖고 있으므로 버린다
 *   - 대신 0x80020000 에 매직 "HPM\n", 0x80020004 에 진입점을 둔다
 * 자세한 인터페이스는 docs/board-iap.md 참조.
 *
 * 본 프로젝트 전용 섹션 3개는 원본과 동일하다.
 *   - .version           : firm_ver 블롭 (hw.c)          + _fw_flash_begin
 *   - _smodule/_emodule  : MODULE_DEF 테이블 (module.c)
 *   - .noinit            : 리셋 후 보존 영역 (hpm5361_it.c 의 fault 로그)
 * 그 외 섹션 순서 / KEEP() / ASSERT 는 원본 그대로 유지한다.
 */

ENTRY(_start)

/* SDK cmake 의 --defsym 대체 */
_flash_size     = 1M;        /* 내장 NOR 전체 (참고용) */
_app_flash_size = 0x60000;   /* 0x80020000 ~ 0x80080000 = 384KB */
_heap_size  = 0x4000;
_stack_size = 0x4000;

STACK_SIZE = _stack_size;
HEAP_SIZE = _heap_size;

MEMORY
{
    FLASH (rx) : ORIGIN = 0x80020000, LENGTH = _app_flash_size
    ILM (wx) : ORIGIN = 0x00000000, LENGTH = 128K
    DLM (w) : ORIGIN = 0x00080300, LENGTH = 128K - 768
    AHB_SRAM (w) : ORIGIN = 0xf0400000, LENGTH = 32K
}

/* IAP 는 0x80020000 의 매직만 확인하고 0x80020004 로 점프한다 (docs/board-iap.md 2절) */
__iap_magic_load_addr__ = ORIGIN(FLASH);
__app_load_addr__       = ORIGIN(FLASH) + 4;


SECTIONS
{
    /* ROM 부팅용 구조는 IAP 가 갖고 있다. 우리가 만들면 중복이므로 버린다. */
    /DISCARD/ : {
        *(.nor_cfg_option)
        *(.boot_header)
        *(.fw_info_table)
        *(.dc_info)
    }

    /* IAP 가 검사하는 매직 — 리틀엔디안 48 50 4D 0A = "HPM\n" */
    .iap_magic __iap_magic_load_addr__ : {
        LONG(0x0A4D5048)
    } > FLASH

    _fw_flash_begin = ORIGIN(FLASH);

    /*
     * 진입점은 반드시 ORIGIN+4 다. 원본에 있던 선두 . = ALIGN(8) 을 그대로 두면
     * +8 로 밀려 IAP 의 점프가 빗나간다.
     */
    .start __app_load_addr__ : {
        KEEP(*(.start))

        /*
         * 이미지 태그 (src/hw/hw.c 의 firm_tag) — **앞쪽에 둔다.**
         *
         * ★ 여기 있어야 하는 이유가 둘이다.
         *
         *   1. 검사 범위가 "태그 뒤 전부" 라, 태그가 앞에 있어야 이미지 거의 전부를
         *      덮는다. 끝에 두었더니 뒤따르는 .data 초기값 2.8KB 만 검사했다.
         *   2. 태그가 **첫 섹터에 함께 써진다.** 굽다 만 이미지도 태그는 남아 있어야
         *      검사가 돌아간다.
         *
         * ★ .start 안에 넣는 이유 — 뒤 섹션 주소가 SIZEOF(.start) 로 줄줄이 엮여
         *   있어서, 별도 출력 섹션을 끼우면 그 식을 전부 다시 짜야 한다. 여기 넣으면
         *   길이만 늘고 나머지는 알아서 따라온다.
         */
        . = ALIGN(4);
        KEEP(*(.tag))

        . = ALIGN(8);
    } > FLASH

    __vector_load_addr__ = ADDR(.start) + SIZEOF(.start);
    .vectors ORIGIN(ILM) : AT(__vector_load_addr__) ALIGN(8) {
        __vector_ram_start__ = .;
        KEEP(*(.vector_table))
        KEEP(*(.isr_vector))
        KEEP(*(.isr_vector.*))
        . = ALIGN(8);
        __vector_ram_end__ = .;
    } > ILM

    /*
     * ★ QMK 키 처리 경로는 ILM 으로 뺀다 (아래 .fast).
     *
     *   여기서 걸러 두지 않으면 .text 가 먼저 집어간다 — ld 는 스크립트 순서대로
     *   **처음 맞는 출력 섹션**에 넣는다.
     */
    .text (__vector_load_addr__ + SIZEOF(.vectors)) : {
        . = ALIGN(8);
        *(EXCLUDE_FILE(*/qmk/*.obj */keyboards/*.obj */src/hw/*.obj) .text)
        *(EXCLUDE_FILE(*/qmk/*.obj */keyboards/*.obj */src/hw/*.obj) .text*)
        *(.rodata)
        *(.rodata*)
        *(.srodata)
        *(.srodata*)

        *(.hash)
        *(.dyn*)
        *(.gnu*)
        *(.pl*)

        KEEP (*(.init))
        KEEP (*(.fini))

        /* section information for usbh class */
        . = ALIGN(8);
        __usbh_class_info_start__ = .;
        KEEP(*(.usbh_class_info))
        __usbh_class_info_end__ = .;

        /* RT-Thread related sections - Start */
        /* section information for finsh shell */
        . = ALIGN(4);
        __fsymtab_start = .;
        KEEP(*(FSymTab))
        __fsymtab_end = .;
        . = ALIGN(4);
        __vsymtab_start = .;
        KEEP(*(VSymTab))
        __vsymtab_end = .;
        . = ALIGN(4);

        . = ALIGN(4);
        __rt_init_start = .;
        KEEP(*(SORT(.rti_fn*)))
        __rt_init_end = .;
        . = ALIGN(4);

        /* section information for modules */
        . = ALIGN(4);
        __rtmsymtab_start = .;
        KEEP(*(RTMSymTab))
        __rtmsymtab_end = .;

        /* RT-Thread related sections - end */
        . = ALIGN(8);
    } > FLASH

    .eh_frame :
    {
        __eh_frame_start = .;
        KEEP(*(.eh_frame))
        __eh_frame_end = .;
    }  > FLASH

    .eh_frame_hdr :
    {
        KEEP(*(.eh_frame_hdr))
    }  > FLASH
    __eh_frame_hdr_start = SIZEOF(.eh_frame_hdr) > 0 ? ADDR(.eh_frame_hdr) : 0;
    __eh_frame_hdr_end = SIZEOF(.eh_frame_hdr) > 0 ? . : 0;

    .rel : {
        KEEP(*(.rel*))
    } > FLASH

    /* 프로젝트 추가: 펌웨어 버전 블롭 (src/hw/hw.c 의 firm_ver).
       심볼로만 접근하므로 고정 주소가 필요 없어 플래시 끝으로 옮겼다. */
    .version : {
        . = ALIGN(4);
        KEEP(*(.version))
        . = ALIGN(4);
    } > FLASH


    . = ALIGN(8);
    PROVIDE (__etext = .);
    PROVIDE (_etext = .);
    PROVIDE (etext = .);

    __data_load_addr__ = etext;
    .data : AT(__data_load_addr__) ALIGN(8) {
        __data_start__ = .;
        __global_pointer$ = . + 0x800;
        *(.data)
        *(.data*)
        *(.sdata)
        *(.sdata*)

        KEEP(*(.jcr))
        KEEP(*(.dynamic))
        KEEP(*(.got*))
        KEEP(*(.got))
        KEEP(*(.gcc_except_table))
        KEEP(*(.gcc_except_table.*))

        . = ALIGN(8);
        PROVIDE(__preinit_array_start = .);
        KEEP(*(.preinit_array))
        PROVIDE(__preinit_array_end = .);

        . = ALIGN(8);
        PROVIDE(__init_array_start = .);
        KEEP(*(SORT_BY_INIT_PRIORITY(.init_array.*)))
        KEEP(*(.init_array))
        PROVIDE(__init_array_end = .);

        . = ALIGN(8);
        PROVIDE(__fini_array_start = .);
        KEEP(*(SORT_BY_INIT_PRIORITY(.fini_array.*)))
        KEEP(*(.fini_array))
        PROVIDE(__fini_array_end = .);

        . = ALIGN(8);
        KEEP(*crtbegin*.o(.ctors))
        KEEP(*(EXCLUDE_FILE (*crtend*.o) .ctors))
        KEEP(*(SORT(.ctors.*)))
        KEEP(*(.ctors))

        . = ALIGN(8);
        KEEP(*crtbegin*.o(.dtors))
        KEEP(*(EXCLUDE_FILE (*crtend*.o) .dtors))
        KEEP(*(SORT(.dtors.*)))
        KEEP(*(.dtors))

        /* 프로젝트 추가: MODULE_DEF 테이블 (src/ap/modules/module.c 가 포인터 산술로 개수를 센다) */
        . = ALIGN(8);
        _smodule = .;
        KEEP(*(.module))
        KEEP(*(.module*))
        _emodule = .;

        . = ALIGN(8);
        __data_end__ = .;
        PROVIDE (__edata = .);
        PROVIDE (_edata = .);
        PROVIDE (edata = .);
    } > DLM

    __fast_load_addr__ = etext + SIZEOF(.data);
    .fast : AT(__fast_load_addr__) ALIGN(8) {
        PROVIDE(__ramfunc_start__ = .);
        *(.fast)
        *(.fast.*)

        /*
         * ★ QMK 키 처리 경로를 통째로 ILM 에 둔다.
         *
         *   키를 누른 뒤 리포트가 나가기까지 걸리는 시간을 재 보니, 그 안에서
         *   **키 처리(action 분해 -> 훅 -> 리포트 생성)가 108us** 로 절반이었다.
         *   400MHz 에서 108us 는 43,000 사이클이다 — 160MHz 짜리가 4,000 사이클에
         *   하는 일이라, 클럭도 알고리즘도 아니고 **명령어를 가져오는 시간**이다.
         *
         *   가려냈다. 같은 키를 1ms 간격으로 이어 두드리면 캐시에 남아 있어서
         *   min 이 절반 아래로 떨어진다 —
         *
         *     간격 500ms   키코드까지 30us   훅+리포트 54us
         *     간격   1ms   키코드까지 15us   훅+리포트 20us
         *
         *   키 처리 경로는 **드물게 돈다.** 스캔은 26us 마다 돌아 캐시에 눌러앉지만,
         *   키를 누르는 것은 수십 ms 에 한 번이라 그때마다 캐시 밖이다. 매번 XIP
         *   QSPI 에서 수백 줄을 다시 읽는다.
         *
         *   ILM 은 코어에 붙어 있어 캐시도 XIP 도 안 탄다. 128KB 중 3KB 만 쓰고
         *   있었고 QMK 전체가 48KB, 보드 훅이 4KB 라 통째로 들어간다.
         *
         * ★ 함수마다 ATTR_RAMFUNC 을 붙이지 않는다. 상류 QMK 소스를 고치는 것이라
         *   갱신할 때마다 다시 붙여야 하고, 어느 함수가 경로에 있는지도 늘 바뀐다.
         *   **오브젝트 단위로 옮기면 소스를 한 줄도 안 건드린다.**
         */
        */qmk/*.obj(.text .text*)
        */keyboards/*.obj(.text .text*)

        /*
         * 스캔·USB 도 통째로 올린다.
         *
         * ★ keys.c 는 뜨거운 곳만 ATTR_RAMFUNC 으로 올려 두었는데(12편), 그 바깥이
         *   플래시에 남아 있으면 **캐시를 두고 다툰다.** QMK 를 올리자마자 실제
         *   루프의 스캔이 31us -> 26us 로 떨어진 것이 그 증거다 — 남을 밀어내던
         *   코드가 사라지자 스캔이 빨라졌다.
         *
         * ★ USB 드라이버를 올려 두면 나중에 **플래시를 쓰는 동안에도 인터럽트를
         *   열어 둘 수 있다.** XIP 가 멎어도 ISR 이 ILM 에 있으면 돈다 (C9).
         *
         * ★ 부팅 코드는 여기 넣으면 안 된다. .fast 를 복사하는 쪽이 .fast 안에
         *   있으면 자기를 복사하기 전에 실행된다. src/bsp 는 그대로 둔다.
         */
        */src/hw/*.obj(.text .text*)
        . = ALIGN(8);
        PROVIDE(__ramfunc_end__ = .);
    } > ILM

    .bss (NOLOAD) : {
        . = ALIGN(8);
        __bss_start__ = .;
        *(.bss)
        *(.bss*)
        *(.sbss*)
        *(.scommon)
        *(.scommon*)
        *(.dynsbss*)
        *(COMMON)
        . = ALIGN(8);
        _end = .;
        __bss_end__ = .;
    } > DLM

    /*
     * 프로젝트 추가: 리셋 후 보존 영역.
     * start.S 의 c_startup 은 __bss_start__ .. __bss_end__ 만 0 으로 밀기 때문에
     * 반드시 .bss 바깥에 두어야 내용이 유지된다.
     */
    .noinit (NOLOAD) : {
        . = ALIGN(8);
        __noinit_start__ = .;
        KEEP(*(.noinit))
        KEEP(*(.noinit.*))
        . = ALIGN(8);
        __noinit_end__ = .;
    } > DLM

    __tdata_load_addr__ = etext + SIZEOF(.data) + SIZEOF(.fast);
    .tdata : AT(__tdata_load_addr__) ALIGN(8) {
        PROVIDE(__tdata_start__ = .);
        *(.tdata)
        *(.tdata.*)
        *(.gnu.linkonce.td.*)
        . = ALIGN(8);
        PROVIDE(__tdata_end__ = .);
    } > DLM

    .tbss (NOLOAD) : {
        . = ALIGN(8);
        PROVIDE(__tbss_start__ = .);
        __thread_pointer$ = .;
        *(.tbss)
        *(.tbss.*)
        *(.gnu.linkonce.tb.*)
        *(.tcommon)
        . = ALIGN(8);
        PROVIDE(__tbss_end__ = .);
    } > DLM

    .framebuffer (NOLOAD) : {
        . = ALIGN(8);
        KEEP(*(.framebuffer))
        KEEP(*(.framebuffer.*))
        . = ALIGN(8);
    } > DLM

    __noncacheable_init_load_addr__ = etext + SIZEOF(.data) + SIZEOF(.fast) + SIZEOF(.tdata);
    .noncacheable.init : AT(__noncacheable_init_load_addr__) ALIGN(8) {
        __noncacheable_init_start__ = .;
        KEEP(*(.noncacheable.init))
        KEEP(*(.noncacheable.init.*))
        . = ALIGN(8);
        __noncacheable_init_end__ = .;
    } > DLM

    .noncacheable (NOLOAD) : {
        . = ALIGN(8);
        KEEP(*(.noncacheable))  /* Legacy sections, kept for backwards compatibility */
        KEEP(*(.noncacheable.non_init))
        KEEP(*(.noncacheable.non_init.*))
        . = ALIGN(8);
        __noncacheable_bss_start__ = .;
        KEEP(*(.noncacheable.bss))
        KEEP(*(.noncacheable.bss.*))
        . = ALIGN(8);
        __noncacheable_bss_end__ = .;
    } > DLM

    __fast_ram_init_load_addr__ = etext + SIZEOF(.data) + SIZEOF(.fast) + SIZEOF(.tdata) + SIZEOF(.noncacheable.init);
    .fast_ram.init : AT(__fast_ram_init_load_addr__) ALIGN(8) {
        __fast_ram_init_start__ = .;
        KEEP(*(.fast_ram.init))
        KEEP(*(.fast_ram.init.*))
        . = ALIGN(8);
        __fast_ram_init_end__ = .;
    } > DLM

    .ahb_sram (NOLOAD) : {
        KEEP(*(.ahb_sram))
        KEEP(*(.ahb_sram.*))
    } > AHB_SRAM

    .fast_ram (NOLOAD) : {
        . = ALIGN(8);
        KEEP(*(.fast_ram))  /* Legacy sections, kept for backwards compatibility */
        KEEP(*(.fast_ram.non_init))
        KEEP(*(.fast_ram.non_init.*))
        . = ALIGN(8);
        __fast_ram_bss_start__ = .;
        KEEP(*(.fast_ram.bss))
        KEEP(*(.fast_ram.bss.*))
        . = ALIGN(8);
        __fast_ram_bss_end__ = .;
    } > DLM

    .heap (NOLOAD) : {
        . = ALIGN(8);
        __heap_start__ = .;
        . += HEAP_SIZE;
        . = ALIGN(8);
        __heap_end__ = .;
    } > DLM

    .stack (NOLOAD) : {
        . = ALIGN(16);
        __stack_base__ = .;
        . += STACK_SIZE;
        . = ALIGN(16);
        PROVIDE (_stack = .);
        PROVIDE (_stack_safe = .);
    } > DLM

    __fw_size__ = SIZEOF(.start) + SIZEOF(.vectors) + SIZEOF(.rel) + SIZEOF(.text) + SIZEOF(.data) + SIZEOF(.fast) + SIZEOF(.tdata) + SIZEOF(.noncacheable.init) + SIZEOF(.fast_ram.init) + SIZEOF(.eh_frame) + SIZEOF(.eh_frame_hdr);
    __last_addr__ = __fast_ram_init_load_addr__ + SIZEOF(.fast_ram.init);
    ASSERT(((__fw_size__ <= LENGTH(FLASH)) && (__last_addr__ <= (ORIGIN(FLASH) + LENGTH(FLASH)))), "******  FAILED! FLASH does not have enough space!  ******")
}
