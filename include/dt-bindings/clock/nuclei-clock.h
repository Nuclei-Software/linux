// SPDX-License-Identifier: GPL-2.0+ OR BSD-3-Clause
/*
 * Copyright (C) 2026 Nucleisys.
 *
 */

#ifndef _DT_BINDINGS_CLOCK_NUCLEI_H
#define _DT_BINDINGS_CLOCK_NUCLEI_H

#define CLK_CORE1_PLL                0
#define CLK_XUC_PLL                  1

#define CLK_MUX_CORE1_PLL_IN         2
#define CLK_MUX_XUC_PLL_IN           3
#define CLK_MUX_CLUSTER1             4
#define CLK_MUX_CLUSTER2             5
#define CLK_MUX_NACC0                6
#define CLK_MUX_DISP_PIXEL           7
#define CLK_MUX_DCMI_PIXEL           8
#define CLK_MUX_XEC_SYS              9
#define CLK_MUX_XEC_RMII             10
#define CLK_MUX_XUC                  11
#define CLK_MUX_SAI_S0               12
#define CLK_MUX_SAI_S1               13
#define CLK_MUX_SAI_S2               14
#define CLK_MUX_SAI_S3               15
#define CLK_MUX_SPDIFRX0             16
#define CLK_MUX_EFUSE                17
#define CLK_MUX_CTC                  18

#define CLK_USART0                   19
#define CLK_USART0_INTF              20
#define CLK_USART1                   21
#define CLK_USART1_INTF              22
#define CLK_USART2                   23
#define CLK_USART2_INTF              24
#define CLK_USART3                   25
#define CLK_USART3_INTF              26
#define CLK_USART4                   27
#define CLK_USART4_INTF              28
#define CLK_USART5                   29
#define CLK_USART5_INTF              30
#define CLK_USART6                   31
#define CLK_USART6_INTF              32
#define CLK_USART7                   33
#define CLK_USART7_INTF              34
#define CLK_USART8                   35
#define CLK_USART8_INTF              36
#define CLK_USART9                   37
#define CLK_USART9_INTF              38
#define CLK_USART10                  39
#define CLK_USART10_INTF             40
#define CLK_USART11                  41
#define CLK_USART11_INTF             42
#define CLK_USART12                  43
#define CLK_USART12_INTF             44

#define CLK_I2C0                     45
#define CLK_I2C0_INTF                46
#define CLK_I2C1                     47
#define CLK_I2C1_INTF                48
#define CLK_I2C2                     49
#define CLK_I2C2_INTF                50
#define CLK_I2C3                     51
#define CLK_I2C3_INTF                52

#define CLK_QSPI_XIP0                53
#define CLK_QSPI_XIP0_INTF           54
#define CLK_QSPI_XIP1                55
#define CLK_QSPI_XIP1_INTF           56
#define CLK_QSPI_XIP2                57
#define CLK_QSPI_XIP2_INTF           58
#define CLK_QSPI3                     59
#define CLK_QSPI3_INTF                60
#define CLK_QSPI4                     61
#define CLK_QSPI4_INTF                62
#define CLK_QSPI5                     63
#define CLK_QSPI5_INTF                64
#define CLK_QSPI6                     65
#define CLK_QSPI6_INTF                66

#define CLK_XKAN0                     67
#define CLK_XKAN0_INTF                68
#define CLK_XKAN1                     69
#define CLK_XKAN1_INTF                70
#define CLK_XKAN2                     71
#define CLK_XKAN2_INTF                72

#define CLK_SAI0                      73
#define CLK_SAI0_S0_INTF              74
#define CLK_SAI0_S1_INTF              75
#define CLK_SAI0_S2_INTF              76

#define CLK_DISP0                     77
#define CLK_DISP0_INTF                78
#define CLK_DISP0_AON                 79
#define CLK_DCMI0                     80
#define CLK_DCMI0_INTF                81
#define CLK_DCMI0_AON                 82

#define CLK_XEC_GEN20_SYS             83
#define CLK_XEC_GEN21_SYS             84
#define CLK_XEC_GEN20_RMII_REF        85
#define CLK_XEC_GEN20_PTP_REF         86

#define CLK_XUC0                      87
#define CLK_XUC_PHY                   88

#define CLK_USB_TOP0                  89
#define CLK_USB_TOP_CORECORE          90

#define CLK_SDIO0                     91
#define CLK_SDIO0_INTF                92
#define CLK_HS_SDIO0                  93
#define CLK_HS_SDIO0_INTF             94

#define CLK_CEC0                      95
#define CLK_CEC0_INTF                 96
#define CLK_DFSDM0                    97
#define CLK_DFSDM0_INTF               98
#define CLK_CAPC0                     99
#define CLK_CAPC0_INTF                100
#define CLK_SWPMI0                    101
#define CLK_SWPMI0_INTF               102
#define CLK_I3C0                      103
#define CLK_I3C0_INTF                 104
#define CLK_LGPIO0                    105
#define CLK_IOMUX                     106
#define CLK_NACC0                     107
#define CLK_G2D0                      108
#define CLK_JPEG0                     109
#define CLK_FILTER0                   110
#define CLK_FLT_MACC0                 111
#define CLK_FFT0                      112
#define CLK_GMC0                      113
#define CLK_PID0                      114
#define CLK_IDU                       115
#define CLK_SOC_GLUE                  116
#define CLK_PCRC0                     117
#define CLK_SYS_UDMA                  118
#define CLK_ACC_UDMA0                 119
#define CLK_ACC_UDMA1                 120
#define CLK_FLT_MACC_UDMA             121
#define CLK_ATB2AXI                   122
#define CLK_MDIOS0                    123
#define CLK_CORDIC0                   124
#define CLK_SPDIFRX0                  125
#define CLK_SPDIFRX0_INTF             126

#define CLK_ADV_TIMER0                127
#define CLK_ADV_TIMER1                128
#define CLK_ADV_TIMER2                129
#define CLK_ADV_TIMER3                130
#define CLK_ADV_TIMER4                131
#define CLK_ADV_TIMER5                132
#define CLK_ADV_TIMER6                133
#define CLK_ADV_TIMER7                134
#define CLK_ADV_TIMER8                135
#define CLK_ADV_TIMER9                136
#define CLK_ADV_TIMER10               137
#define CLK_ADV_TIMER11               138
#define CLK_ADV_TIMER12               139
#define CLK_ADV_TIMER13               140
#define CLK_ADV_TIMER14               141
#define CLK_ADV_TIMER15               142

#define CLK_BASIC_TIMER0              143
#define CLK_ACRYP0                    144
#define CLK_CRYP0                     145
#define CLK_HASH0                     146
#define CLK_TRNG0                     147
#define CLK_TRNG0_SMP                 148
#define CLK_WWDG0                     149
#define CLK_WWDG1                     150
#define CLK_BROM                      151
#define CLK_TSENS_INTF                152
#define CLK_ADC                       153
#define CLK_DAC                       154
#define CLK_VREF                      155
#define CLK_TSENS                     156
#define CLK_PVD                       157
#define CLK_COMP                      158
#define CLK_OPAMP                     159
#define CLK_CTC0                      160
#define CLK_CTC0_REF                  161

#define CLK_XEC_GEN21_RMII_REF        162
#define CLK_XEC_GEN21_PTP_REF         163

#define CLK_MAX                       164

#endif /* _DT_BINDINGS_CLOCK_NUCLEI_H */
