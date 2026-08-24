// SPDX-License-Identifier: GPL-2.0+ OR BSD-3-Clause
/*
 * Copyright (C) 2026 Nucleisys.
 *
 */

#ifndef _DT_BINDINGS_RESET_NUCLEI_H
#define _DT_BINDINGS_RESET_NUCLEI_H

/* ============================================================
 * RESET ID Defination
 * SUBM_RESET_CTRL0: ID 0~31
 * SUBM_RESET_CTRL1: ID 32~63
 * SUBM_RESET_CTRL2: ID 64~95
 * SUBM_RESET_CTRL3: ID 96~127
 * ============================================================ */

/* SUBM_RESET_CTRL0 - offset 0x20, ID 0~31 */
#define RST_PID0               0
#define RST_ADC                1
#define RST_TSENS              2
#define RST_DAC                3
#define RST_VREF               4
#define RST_COMP               5
#define RST_OPAMP              6
/* 7~8: Reserved */
#define RST_USART0             9
#define RST_USART1             10
#define RST_USART2             11
#define RST_USART3             12
#define RST_USART4             13
#define RST_USART5             14
#define RST_USART6             15
#define RST_USART7             16
#define RST_USART8             17
#define RST_USART9             18
#define RST_USART10            19
#define RST_USART11            20
#define RST_USART12            21
#define RST_I2C0               22
#define RST_I2C1               23
#define RST_I2C2               24
#define RST_I2C3               25
/* 26~27: Reserved */
#define RST_QSPI_XIP0          28
#define RST_QSPI_XIP1          29
#define RST_QSPI_XIP2          30
#define RST_QSPI3              31

/* SUBM_RESET_CTRL1 - offset 0x24, ID 32~63 */
#define RST_QSPI4              32
#define RST_QSPI5              33
#define RST_QSPI6              34
#define RST_XKAN0              35
#define RST_XKAN1              36
#define RST_XKAN2              37
#define RST_SAI0               38
#define RST_ADV_TIMER0         39
#define RST_ADV_TIMER1         40
#define RST_ADV_TIMER2         41
#define RST_ADV_TIMER3         42
#define RST_ADV_TIMER4         43
#define RST_ADV_TIMER5         44
#define RST_ADV_TIMER6         45
#define RST_ADV_TIMER7         46
#define RST_ADV_TIMER8         47
#define RST_ADV_TIMER9         48
#define RST_ADV_TIMER10        49
#define RST_ADV_TIMER11        50
#define RST_ADV_TIMER12        51
#define RST_ADV_TIMER13        52
#define RST_ADV_TIMER14        53
#define RST_ADV_TIMER15        54
#define RST_LGPIO0             55
#define RST_I2C2ICB0           56
#define RST_DISP0              57
#define RST_DCMI0              58
#define RST_G2D0               59
#define RST_JPEG0              60
#define RST_NACC0              61
#define RST_FILTER0            62
#define RST_FLT_MACC0          63

/* SUBM_RESET_CTRL2 - offset 0x28, ID 64~95 */
#define RST_FFT0               64
#define RST_GMC0               65
#define RST_XEC_GEN20          66
#define RST_XEC_GEN21          67
#define RST_XUC0               68
#define RST_USB_TOP0           69
/* 70~71: Reserved */
#define RST_IDU                72
#define RST_SOC_GLUE           73
#define RST_PCRC0              74
/* 75~82: Reserved */
#define RST_SYS_UDMA           83
#define RST_IOMUX              84
#define RST_SDIO0              85
#define RST_HS_SDIO0           86
#define RST_I3C0               87
#define RST_MDIOS0             88
#define RST_CORDIC0            89
#define RST_SPDIFRX0           90
#define RST_SWPMI0             91
#define RST_CEC0               92
#define RST_DFSDM0             93
#define RST_CAPC0              94
#define RST_CTC0               95

/* SUBM_RESET_CTRL3 - offset 0x2C, ID 96~127 */
#define RST_ACC_UDMA0          96
#define RST_FLT_MACC_UDMA      97
#define RST_ACC_UDMA1          98
#define RST_SYS_CACHE          99
#define RST_ATB2AXI0           100
/* 101~127: Reserved */

#endif /* _DT_BINDINGS_RESET_NUCLEI_H */
