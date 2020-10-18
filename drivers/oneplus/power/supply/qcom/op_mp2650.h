/************************************************************************************
** --------------------------- Revision History: ------------------------------------------------------------
* <version>       <date>        <author>                          <desc>
* Revision 1.0    2015-06-22    Fanhong.Kong@ProDrv.CHG           Created for new architecture
************************************************************************************************************/


#ifndef __OP_MP2650_H__

#define __OP_MP2650_H__

#define OP_MP2650

#include <linux/power_supply.h>
#include <linux/regmap.h>

#define chg_debug(fmt, ...)                                                    \
    printk(KERN_NOTICE "[SWARPCHG][%s]" fmt, __func__, ##__VA_ARGS__)

#define chg_err(fmt, ...)                                                      \
    printk(KERN_ERR "[SWARPCHG][%s]" fmt, __func__, ##__VA_ARGS__)

#define chg_info(fmt, ...)                                                     \
    printk(KERN_INFO "[SWARPCHG][%s]" fmt, __func__, ##__VA_ARGS__)


#define WPC_PRECHARGE_CURRENT                                   480

#define MP2650_FIRST_REG                                        0x00
#define MP2650_DUMP_MAX_REG                                            0x22
#define MP2650_LAST_REG                                            0x48
#define MP2650_REG_NUMBER                                        0x30


/* Address:00h */
#define REG00_MP2650_ADDRESS                                   0x00

#define REG00_MP2650_1ST_CURRENT_LIMIT_MASK                    (BIT(6) | BIT(5) | BIT(4) | BIT(3) | BIT(2) | BIT(1) | BIT(0))
#define REG00_MP2650_1ST_CURRENT_LIMIT_SHIFT                   0
#define REG00_MP2650_1ST_CURRENT_LIMIT_OFFSET                  0
#define REG00_MP2650_1ST_CURRENT_LIMIT_STEP                    50    //default 3A
#define REG00_MP2650_1ST_CURRENT_LIMIT_500MA                  (BIT(3) | BIT(1))
#define REG00_MP2650_1ST_CURRENT_LIMIT_900MA                  (BIT(4) | BIT(1))
#define REG00_MP2650_1ST_CURRENT_LIMIT_1100MA                  (BIT(4) | BIT(2) | BIT(1))
#define REG00_MP2650_1ST_CURRENT_LIMIT_1200MA                  (BIT(4) | BIT(3))
#define REG00_MP2650_1ST_CURRENT_LIMIT_1350MA                  (BIT(4) | BIT(3) | BIT(1) | BIT(0))
#define REG00_MP2650_1ST_CURRENT_LIMIT_1400MA                  (BIT(4) |BIT(3) |BIT(2))
#define REG00_MP2650_1ST_CURRENT_LIMIT_1500MA                  (BIT(4) |BIT(3) |BIT(2) |BIT(1))
#define REG00_MP2650_1ST_CURRENT_LIMIT_1600MA                  (BIT(5))
#define REG00_MP2650_1ST_CURRENT_LIMIT_1700MA                  (BIT(5) | BIT(1))
#define REG00_MP2650_1ST_CURRENT_LIMIT_1900MA                  (BIT(5) | BIT(2) | BIT(1))
#define REG00_MP2650_1ST_CURRENT_LIMIT_2000MA                  (BIT(5) | BIT(3))
#define REG00_MP2650_1ST_CURRENT_LIMIT_2400MA                  (BIT(5) | BIT(4))
#define REG00_MP2650_1ST_CURRENT_LIMIT_3600MA                  (BIT(6) | BIT(3))

/* Address:01h */
#define REG01_MP2650_ADDRESS                                   0x01

#define REG01_MP2650_VINDPM_THRESHOLD_MASK                     (BIT(7) | BIT(6) | BIT(5) | BIT(4) | BIT(3) | BIT(2) | BIT(1) | BIT(0))
#define REG01_MP2650_VINDPM_THRESHOLD_SHIFT                    0
#define REG01_MP2650_VINDPM_THRESHOLD_OFFSET                   0
#define REG01_MP2650_VINDPM_THRESHOLD_STEP                     100    //default 4.5V


/* Address:02h */
#define REG02_MP2650_ADDRESS                                   0x02

#define REG02_MP2650_CHARGE_CURRENT_SETTING_MASK               (BIT(7) | BIT(6) | BIT(5) | BIT(4) | BIT(3) | BIT(2) | BIT(1) | BIT(0))
#define REG02_MP2650_CHARGE_CURRENT_SETTING_SHIFT              0
#define REG02_MP2650_CHARGE_CURRENT_SETTING_OFFSET             0
#define REG02_MP2650_CHARGE_CURRENT_SETTING_STEP               50    //default 3A


/* Address:03h */
#define REG03_MP2650_ADDRESS                                   0x03

#define REG03_MP2650_TERMINATION_CURRENT_LIMIT_MASK            (BIT(3) | BIT(2) | BIT(1) | BIT(0))
#define REG03_MP2650_TERMINATION_CURRENT_LIMIT_SHIFT           0
#define REG03_MP2650_TERMINATION_CURRENT_LIMIT_OFFSET          0
#define REG03_MP2650_TERMINATION_CURRENT_LIMIT_STEP            100    //default 200mA
#define REG03_MP2650_TERMINATION_CURRENT_600MA                 (BIT(2) | BIT(1))
#define REG03_MP2650_TERMINATION_CURRENT_200MA                 BIT(1)
#define REG03_MP2650_TERMINATION_CURRENT_100MA                 BIT(0)

#define REG03_MP2650_PRECHARGE_CURRENT_LIMIT_MASK              (BIT(7) | BIT(6) | BIT(5) | BIT(4))
#define REG03_MP2650_PRECHARGE_CURRENT_LIMIT_SHIFT             4
#define REG03_MP2650_PRECHARGE_CURRENT_LIMIT_OFFSET            0
#define REG03_MP2650_PRECHARGE_CURRENT_LIMIT_STEP              60    //default 400mA


/* Address:04h */
#define REG04_MP2650_ADDRESS                                   0x04

#define REG04_MP2650_BAT_RECHARGE_THRESHOLD_MASK               BIT(0)
#define REG04_MP2650_BAT_RECHARGE_THRESHOLD_100MV              0//default
#define REG04_MP2650_BAT_RECHARGE_THRESHOLD_200MV              BIT(0)
#define REG04_MP2650_BAT_RECHARGE_THRESHOLD_SHIFT              0//default
#define REG04_MP2650_BAT_RECHARGE_THRESHOLD_OFFSET             100
#define REG04_MP2650_BAT_RECHARGE_THRESHOLD_STEP               100   

#define REG04_MP2650_CHARGE_FULL_VOL_MASK                      (BIT(6) | BIT(5) | BIT(4) | BIT(3) | BIT(2) | BIT(1))
#define REG04_MP2650_CHARGE_FULL_VOL_SHIFT                     1
#define REG04_MP2650_CHARGE_FULL_VOL_OFFSET                    37125
#define REG04_MP2650_CHARGE_FULL_VOL_STEP                      125   //12.5mV,  default 4.2V


/* Address:05h */
#define REG05_MP2650_ADDRESS                                   0x05

#define REG05_MP2650_THERMAL_REGULATION_THRESHOLD_MASK         (BIT(1) | BIT(0))
#define REG05_MP2650_THERMAL_REGULATION_THRESHOLD_SHIFT        0
#define REG05_MP2650_THERMAL_REGULATION_THRESHOLD_OFFSET       60
#define REG05_MP2650_THERMAL_REGULATION_THRESHOLD_STEP         20    //default 120

#define REG05_MP2650_IR_COMPENSATION_RESISTOR_CLAMP_MASK       (BIT(4) | BIT(3) | BIT(2))
#define REG05_MP2650_IR_COMPENSATION_RESISTOR_CLAMP_SHIFT      2
#define REG05_MP2650_IR_COMPENSATION_RESISTOR_CLAMP_OFFSET     0
#define REG05_MP2650_IR_COMPENSATION_RESISTOR_CLAMP_STEP       30    //default 0mV

#define REG05_MP2650_IR_COMPENSATION_RESISTOR_SETTING_MASK     (BIT(7) | BIT(6) | BIT(5))
#define REG05_MP2650_IR_COMPENSATION_RESISTOR_SETTING_SHIFT    5
#define REG05_MP2650_IR_COMPENSATION_RESISTOR_SETTING_OFFSET   0
#define REG05_MP2650_IR_COMPENSATION_RESISTOR_SETTING_STEP     25    //default 0mO


/* Address:06h */
#define REG06_MP2650_ADDRESS                                   0x06

#define REG06_MP2650_2ND_OTG_VOL_SETTING_MASK                  (BIT(3) | BIT(2) | BIT(1) | BIT(0))
#define REG06_MP2650_2ND_OTG_VOL_SETTING_SHIFT                 0
#define REG06_MP2650_2ND_OTG_VOL_SETTING_OFFSET                0
#define REG06_MP2650_2ND_OTG_VOL_SETTING_STEP                  50    //default 250mV
#define REG06_MP2650_2ND_OTG_VOL_SETTING_250MV                 (BIT(2) | BIT(0))

#define REG06_MP2650_OTG_VOL_OPTION_MASK                       (BIT(6) | BIT(5) | BIT(4))
#define REG06_MP2650_OTG_VOL_OPTION_SHIFT                      4
#define REG06_MP2650_OTG_VOL_OPTION_4750MV                     0     //default
#define REG06_MP2650_OTG_VOL_OPTION_8750MV                     BIT(4)
#define REG06_MP2650_OTG_VOL_OPTION_11750MV                    BIT(5)
#define REG06_MP2650_OTG_VOL_OPTION_14750MV                    (BIT(5) | BIT(4))
#define REG06_MP2650_OTG_VOL_OPTION_1950MV                     BIT(6)


/* Address:07h */
#define REG07_MP2650_ADDRESS                                   0x07

#define REG07_MP2650_OTG_CURRENT_LIMIT_MASK                    (BIT(3) | BIT(2) | BIT(1) | BIT(0))
#define REG07_MP2650_OTG_CURRENT_LIMIT_SHIFT                   0
#define REG07_MP2650_OTG_CURRENT_LIMIT_OFFSET                  0
#define REG07_MP2650_OTG_CURRENT_LIMIT_STEP                    250    //default 1A
#define REG07_MP2650_OTG_CURRENT_LIMIT_1A                      BIT(2)
#define REG07_MP2650_OTG_CURRENT_LIMIT_1250MA                  (BIT(2) | BIT(0))
#define MP2650_OTG_CURRENT_LIMIT_DEFAULT                       1500

#define REG07_MP2650_PRECHARGE_THRESHOLD_MASK                  (BIT(5) | BIT(4))
#define REG07_MP2650_PRECHARGE_THRESHOLD_6600MV                0
#define REG07_MP2650_PRECHARGE_THRESHOLD_6800MV                BIT(4)//default
#define REG07_MP2650_PRECHARGE_THRESHOLD_7400MV                BIT(5)
#define REG07_MP2650_PRECHARGE_THRESHOLD_7200MV                (BIT(5) | BIT(4))

#define REG07_MP2650_BAT_NUMBER_MASK                           (BIT(7) | BIT(6))
#define REG07_MP2650_BAT_NUMBER_2_CELL                         0    //default
#define REG07_MP2650_BAT_NUMBER_3_CELL                         BIT(6)
#define REG07_MP2650_BAT_NUMBER_4_CELL                         BIT(7)

/* Address:08h */
#define REG08_MP2650_ADDRESS                                   0x08

#define REG08_MP2650_BFET_ALWON_MASK                           BIT(0)
#define REG08_MP2650_BFET_ALWON_DEPEND_BFET_EN                 0    //default
#define REG08_MP2650_BFET_ALWON_FULL_ON                        BIT(0)

#define REG08_MP2650_BFET_EN_MASK                              BIT(1)
#define REG08_MP2650_BFET_DISABLE                              0    
#define REG08_MP2650_BFET_ENABLE                               BIT(1)    //default

#define REG08_MP2650_NTC_GCOMP_SEL_MASK                        BIT(2)
#define REG08_MP2650_NTC_GCOMP_SEL_INDEPENDENT                 0    
#define REG08_MP2650_NTC_GCOMP_SEL_ORIGINAL                    BIT(2)    //default

#define REG08_MP2650_LEARN_EN_MASK                             BIT(3)
#define REG08_MP2650_LEARN_EN_DISABLE                          0       //default 
#define REG08_MP2650_LEARN_EN_ENABLE                           BIT(3)

#define REG08_MP2650_CHG_EN_MASK                               BIT(4)
#define REG08_MP2650_CHG_EN_DISABLE                            0       
#define REG08_MP2650_CHG_EN_ENABLE                             BIT(4)    //default 

#define REG08_MP2650_OTG_EN_MASK                               BIT(5)
#define REG08_MP2650_OTG_EN_DISABLE                            0        //default    
#define REG08_MP2650_OTG_EN_ENABLE                             BIT(5)

#define REG08_MP2650_WTD_RST_MASK                              BIT(6)
#define REG08_MP2650_WTD_RST_NORMAL                            0        //default    
#define REG08_MP2650_WTD_RST_RESET                             BIT(6)

#define REG08_MP2650_REG_RST_MASK                              BIT(7)
#define REG08_MP2650_REG_RST_KEEP                              0        //default    
#define REG08_MP2650_REG_RST_RESET                             BIT(7)


/* Address:09h */
#define REG09_MP2650_ADDRESS                                   0x09

#define REG09_MP2650_TIM2X_EN_MASK                             BIT(0)
#define REG09_MP2650_TIM2X_EN_DISABLE                          0        //default  
#define REG09_MP2650_TIM2X_EN_ENABLE                           BIT(0)

#define REG09_MP2650_FAST_CHARGE_TIMER_MASK                    (BIT(2) | BIT(1))
#define REG09_MP2650_FAST_CHARGE_TIMER_5H                      0
#define REG09_MP2650_FAST_CHARGE_TIMER_8H                      BIT(1)
#define REG09_MP2650_FAST_CHARGE_TIMER_12H                     BIT(2)    //default
#define REG09_MP2650_FAST_CHARGE_TIMER_20H                     (BIT(2) | BIT(1))

#define REG09_MP2650_CHARGE_TIMER_EN_MASK                      BIT(3)
#define REG09_MP2650_CHARGE_TIMER_EN_DISABLE                   0    
#define REG09_MP2650_CHARGE_TIMER_EN_ENABLE                    BIT(3)    //default  

#define REG09_MP2650_WTD_TIMER_MASK                            (BIT(5) | BIT(4))
#define REG09_MP2650_WTD_TIMER_DISABLE                         0        //default
#define REG09_MP2650_WTD_TIMER_40S                             BIT(4)
#define REG09_MP2650_WTD_TIMER_80S                             BIT(5)
#define REG09_MP2650_WTD_TIMER_160S                            (BIT(5) | BIT(4))

#define REG09_MP2650_CHARGE_TERMINATION_EN_MASK                BIT(6)
#define REG09_MP2650_CHARGE_TERMINATION_EN_DISABLE             0    
#define REG09_MP2650_CCHARGE_TERMINATION_EN_ENABLE             BIT(6)    //default  


/* Address:0Ah */
#define REG0A_MP2650_ADDRESS                                   0x0A

#define REG0A_MP2650_NTC_COOL_MASK                             (BIT(1) | BIT(0))
#define REG0A_MP2650_NTC_COOL_707_PERMILLE                     0
#define REG0A_MP2650_NTC_COOL_697_PERMILLE                     BIT(0)
#define REG0A_MP2650_NTC_COOL_686_PERMILLE                     BIT(1)        //default
#define REG0A_MP2650_NTC_COOL_673_PERMILLE                     (BIT(1) | BIT(0))

#define REG0A_MP2650_NTC_WARM_MASK                             (BIT(3) | BIT(2))
#define REG0A_MP2650_NTC_WARM_583_PERMILLE                     0
#define REG0A_MP2650_NTC_WARM_561_PERMILLE                     BIT(2)        //default
#define REG0A_MP2650_NTC_WARM_537_PERMILLE                     BIT(3)
#define REG0A_MP2650_NTC_WARM_513_PERMILLE                     (BIT(3) | BIT(2))

#define REG0A_MP2650_NTC_CTRL_MASK                             (BIT(5) | BIT(4))
#define REG0A_MP2650_NTC_CTRL_JEITA                            0            //default
#define REG0A_MP2650_NTC_CTRL_STANDARD                         BIT(4)
#define REG0A_MP2650_NTC_CTRL_PCB                              BIT(5)
#define REG0A_MP2650_NTC_CTRL_DISABLE                         (BIT(5) | BIT(4))

#define REG0A_MP2650_JEITA_VSET_MASK                           BIT(6)
#define REG0A_MP2650_JEITA_VSET_150MV                          0        //default 
#define REG0A_MP2650_JEITA_VSET_300MV                          BIT(6)

#define REG0A_MP2650_JEITA_ISET_MASK                           BIT(7)
#define REG0A_MP2650_JEITA_ISET_50_PERCENT                     0    
#define REG0A_MP2650_JEITA_ISET_20_PERCENT                     BIT(7)    //default

/* Address:0Bh */
#define REG0B_MP2650_ADDRESS                                   0x0B

#define REG0B_MP2650_PROCHOT_PSYS_CFG_MASK                     (BIT(1) | BIT(0))
#define REG0B_MP2650_PROCHOT_PSYS_CFG_DISABLE                  0
#define REG0B_MP2650_PROCHOT_PSYS_CFG_PROCHOT                  BIT(0)    //default
#define REG0B_MP2650_PROCHOT_PSYS_CFG_ENABLE                   BIT(1)

#define REG0B_MP2650_AICL_EN_MASK                              BIT(2)
#define REG0B_MP2650_AICL_EN_DISABLE                           0
#define REG0B_MP2650_AICL_EN_ENABLE                            BIT(2)

#define REG0B_MP2650_SW_FREQ_MASK                              (BIT(4) | BIT(3))
#define REG0B_MP2650_SW_FREQ_600K                              0        //default
#define REG0B_MP2650_SW_FREQ_800K                              BIT(3)
#define REG0B_MP2650_SW_FREQ_1000K                             BIT(4)
#define REG0B_MP2650_SW_FREQ_1250K                             (BIT(4) | BIT(3))

#define REG0B_MP2650_IBM_CFG_MASK                              BIT(5)
#define REG0B_MP2650_IBM_CFG_REFLECT_CHARGE_CURRENT            0        //default
#define REG0B_MP2650_IBM_CFG_REFLECT_DISCHARGE_CURRENT         BIT(5)

#define REG0B_MP2650_IBM_EN_MASK                               BIT(6)
#define REG0B_MP2650_IBM_EN_DISABLE                            0        //default
#define REG0B_MP2650_IBM_EN_ENABLE                             BIT(6)


/* Address:0Ch */
#define REG0C_MP2650_ADDRESS                                   0x0C

#define REG0C_MP2650_COMPARATOR_REFERENCE_MASK                 BIT(0)
#define REG0C_MP2650_COMPARATOR_REFERENCE_2100MV               0        //default
#define REG0C_MP2650_COMPARATOR_REFERENCE_1200MV               BIT(0)

#define REG0C_MP2650_COMPARATOR_CFG_MASK                       BIT(1)
#define REG0C_MP2650_COMPARATOR_CFG_AS_PROCHOT                 0        //default
#define REG0C_MP2650_COMPARATOR_CFG_NOT_PROCHOT                BIT(1)

#define REG0C_MP2650_VIN_DSG_MASK                              BIT(2)
#define REG0C_MP2650_VIN_DSG_DISABLE                           0        //default
#define REG0C_MP2650_VIN_DSG_ENABLE                            BIT(2)

#define REG0C_MP2650_IDEAL_DIODE_EN_MASK                       BIT(3)
#define REG0C_MP2650_IDEAL_DIODE_EN_DISABLE                    0        //default
#define REG0C_MP2650_IDEAL_DIODE_EN_ENABLE                     BIT(3)

#define REG0C_MP2650_VSYS_PROCHOT_TDB_MASK                     BIT(4)
#define REG0C_MP2650_VSYS_PROCHOT_TDB_10US                     0        //default
#define REG0C_MP2650_VSYS_PROCHOT_TDB_20US                     BIT(4)

#define REG0C_MP2650_DIS_OC_PROCHOT_MASK                       (BIT(7) | BIT(6) | BIT(5))
#define REG0C_MP2650_DIS_OC_PROCHOT_SHIFT                      5
#define REG0C_MP2650_DIS_OC_PROCHOT_OFFSET                     0
#define REG0C_MP2650_DIS_OC_PROCHOT_STEP                       2   //default 12A


/* Address:0Dh */
#define REG0D_MP2650_ADDRESS                                   0x0D

#define REG0D_MP2650_SYS_UV_PROCHOT_MASK                       (BIT(1) | BIT(0))
#define REG0D_MP2650_YS_UV_PROCHOT_5600MV                      0
#define REG0D_MP2650_YS_UV_PROCHOT_5800MV                      BIT(0)        //default
#define REG0D_MP2650_YS_UV_PROCHOT_6000MV                      BIT(1)
#define REG0D_MP2650_YS_UV_PROCHOT_6200MV                      (BIT(1) | BIT(0))

#define REG0D_MP2650_VOTG_VSYS_UV_MASK                         (BIT(3) | BIT(2))
#define REG0D_MP2650_VOTG_VSYS_UV_75_PERCENT                   0            //default
#define REG0D_MP2650_VOTG_VSYS_UV_80_PERCENT                   BIT(2)
#define REG0D_MP2650_VOTG_VSYS_UV_85_PERCENT                   BIT(3)
#define REG0D_MP2650_VOTG_VSYS_UV_90_PERCENT                   (BIT(3) | BIT(2))

#define REG0D_MP2650_VOTG_VSYS_OV_MASK                         (BIT(5) | BIT(4))
#define REG0D_MP2650_VOTG_VSYS_OV_125_PERCENT                  0            //default
#define REG0D_MP2650_VOTG_VSYS_OV_120_PERCENT                  BIT(4)
#define REG0D_MP2650_VOTG_VSYS_OV_115_PERCENT                  BIT(5)
#define REG0D_MP2650_VOTG_VSYS_OV_110_PERCENT                  (BIT(5) | BIT(4))


/* Address:0Eh */
#define REG0E_MP2650_ADDRESS                                   0x0E

#define REG0E_MP2650_DURATION_PROCHOT_ASSERTED_MASK            (BIT(4) | BIT(3) | BIT(2) | BIT(1) | BIT(0))
#define REG0E_MP2650_DURATION_PROCHOT_ASSERTED_SHIFT           0
#define REG0E_MP2650_DURATION_PROCHOT_ASSERTEDT_OFFSET         0
#define REG0E_MP2650_DURATION_PROCHOT_ASSERTED_STEP            100   //default 300us

#define REG0E_MP2650_TIME_BEFORE_PROCHOT_MASK                  (BIT(7) | BIT(6) | BIT(5))
#define REG0E_MP2650_TIME_BEFORE_PROCHOT_SHIFT                 5
#define REG0E_MP2650_TIME_BEFORE_PROCHOT_OFFSET                0
#define REG0E_MP2650_TIME_BEFORE_PROCHOT_STEP                  100   //default 200us


/* Address:0Fh */
#define REG0F_MP2650_ADDRESS                                   0x0F

#define REG0F_MP2650_2ND_CURRENT_LIMIT_MASK                    (BIT(6) | BIT(5) | BIT(4) | BIT(3) | BIT(2) | BIT(1) | BIT(0))
#define REG0F_MP2650_2ND_CURRENT_LIMIT_SHIFT                   0
#define REG0F_MP2650_2ND_CURRENT_LIMIT_OFFSET                  0
#define REG0F_MP2650_2ND_CURRENT_LIMIT_STEP                    50    //default 3A
#define REG0F_MP2650_2ND_CURRENT_LIMIT_1600MA                  BIT(5)
#define REG0F_MP2650_2ND_CURRENT_LIMIT_3600MA                  (BIT(6) | BIT(3))


/* Address:10h */
#define REG10_MP2650_ADDRESS                                   0x10

#define REG10_MP2650_2ND_CURRENT_LIMIT_TIME_MASK               (BIT(7) | BIT(6) | BIT(5) | BIT(4) | BIT(3) | BIT(2) | BIT(1) | BIT(0))
#define REG10_MP2650_2ND_CURRENT_LIMIT_TIME_SHIFT              0
#define REG10_MP2650_2ND_CURRENT_LIMIT_TIME_OFFSET             100
#define REG10_MP2650_2ND_CURRENT_LIMIT_TIME_STEP               100    //default 700us


/* Address:11h */
#define REG11_MP2650_ADDRESS                                   0x11

#define REG11_MP2650_CURRENT_LIMIT_TOTAL_TIME_MASK             (BIT(7) | BIT(6) | BIT(5) | BIT(4) | BIT(3) | BIT(2) | BIT(1) | BIT(0))
#define REG11_MP2650_CURRENT_LIMIT_TOTAL_TIMEE_SHIFT           0
#define REG11_MP2650_CURRENT_LIMIT_TOTAL_TIME_OFFSET           100
#define REG11_MP2650_CURRENT_LIMIT_TOTAL_TIME_STEP             100    //default 1600us


/* Address:12h */
#define REG12_MP2650_ADDRESS                                   0x12

#define REG12_MP2650_INPUT_OVA_THRESHOLD_MASK                  (BIT(6) | BIT(5) | BIT(4) | BIT(3))
#define REG12_MP2650_INPUT_OVA_THRESHOLD_SHIFT                 3
#define REG12_MP2650_INPUT_OVA_THRESHOLD_OFFSET                0
#define REG12_MP2650_INPUT_OVA_THRESHOLD_STEP                  800    //default 11.2A

#define REG12_MP2650_BUCK_SWITCH_MASK                          BIT(2)
#define REG12_MP2650_BUCK_SWITCH_DISABLE                       BIT(2)
#define REG12_MP2650_BUCK_SWITCH_ENABLE                        0


/* Address:13h (Read only)*/
#define REG13_MP2650_ADDRESS                                   0x13

#define REG13_MP2650_IN_VSYSMIN_REGULATION_MASK                BIT(0)
#define REG13_MP2650_IN_VSYSMIN_REGULATION_YES                 0
#define REG13_MP2650_IN_VSYSMIN_REGULATION_NO                  BIT(0)        //default

#define REG13_MP2650_VIN_POWER_GOOD_MASK                       BIT(1)
#define REG13_MP2650_VIN_POWER_GOOD_NO                         0            //default
#define REG13_MP2650_VIN_POWER_GOOD_YES                        BIT(1)

#define REG13_MP2650_CHARGING_STATUS_MASK                      (BIT(3) | BIT(2))
#define REG13_MP2650_CHARGING_STATUS_NOT_CHARGING              0            //default
#define REG13_MP2650_CHARGING_STATUS_PRE_CHARGE                BIT(2)
#define REG13_MP2650_CHARGING_STATUS_FAST_CHARGE               BIT(3)
#define REG13_MP2650_CHARGING_STATUS_CHARGE_TERMINATION        (BIT(3) | BIT(2))

#define REG13_MP2650_PPM_STAT_MASK                             BIT(4)
#define REG13_MP2650_PPM_STAT_NO_DPM                           0        //default
#define REG13_MP2650_PPM_STAT_VINDPM_INDPM                     BIT(4)

#define REG13_MP2650_AICL_STAT_MASK                            BIT(5)
#define REG13_MP2650_DETECTION_IN_PROGRESS                     0        //default
#define REG13_MP2650_MAX_INPUT_DETECTED                        BIT(5)

#define REG13_MP2650_VSYS_UV_MASK                              BIT(6)
#define REG13_MP2650_VSYS_UV_NO                                0        //default
#define REG13_MP2650_VSYS_UV_YES                               BIT(6)

#define REG13_MP2650_BATT_UVLO_MASK                            BIT(7)
#define REG13_MP2650_BATT_UVLO_NO                              0        //default
#define REG13_MP2650_BATT_UVLO_YES                             BIT(7)


/* Address:14h (Read only)*/
#define REG14_MP2650_ADDRESS                                   0x14

#define REG14_MP2650_NTC_FAULT_MASK                            (BIT(2) | BIT(1) | BIT(0))
#define REG14_MP2650_NTC_FAULT_NORMAL                          0            //default
#define REG14_MP2650_NTC_FAULT_COLD                            BIT(0)
#define REG14_MP2650_NTC_FAULT_COOL                            BIT(1)
#define REG14_MP2650_NTC_FAULT_WARM                            (BIT(1) | BIT(0))
#define REG14_MP2650_NTC_FAULT_HOT                             BIT(2)

#define REG14_MP2650_BAT_FAULT_MASK                            BIT(3)
#define REG14_MP2650_BAT_FAULT_NO                              0        //default
#define REG14_MP2650_BAT_FAULT_OVP                             BIT(3)

#define REG14_MP2650_CHARGE_FAULT_MASK                         (BIT(5) | BIT(4))
#define REG14_MP2650_CHARGE_FAULT_NORMAL                       0        //default
#define REG14_MP2650_CHARGE_FAULT_INPUT_FAULT                  BIT(4)
#define REG14_MP2650_CHARGE_FAULT_THERMAL_SHUTDOWN             BIT(5)
#define REG14_MP2650_CHARGE_FAULT_SAFETY_TIMEOUT               (BIT(5) | BIT(4))

#define REG14_MP2650_OTG_MODE_FAULT_MASK                       BIT(6)
#define REG14_MP2650_OTG_MODE_FAULT_NO                         0        //default
#define REG14_MP2650_OTG_MODE_FAULT_YES                        BIT(6)

#define REG14_MP2650_WATCHDOG_FAULT_MASK                       BIT(7)
#define REG14_MP2650_WATCHDOG_FAULT_NO                         0        //default
#define REG14_MP2650_WATCHDOG_FAULT_YES                        BIT(7)


/* Address:15h (Read only)*/
#define REG15_MP2650_ADDRESS                                   0x15

#define REG15_MP2650_DEVICE_REVISION_MASK                      (BIT(2) | BIT(1) | BIT(0))
#define REG15_MP2650_DEVICE_REVISION_MP2650                    000

#define REG15_MP2650_DEVICE_CONFIGURATION_MASK                 (BIT(5) | BIT(4) | BIT(3))
#define REG15_MP2650_DEVICE_CONFIGURATION_1ST                  000

/* Address:16h 17h(Read only)*/
#define REG1617_MP2650_ADDRESS                                 0x16

#define REG1617_MP2650_BAT_VOLTAGE_MASK                        (BIT(15) | BIT(14) | BIT(13) | BIT(12) | BIT(11) | BIT(10) | BIT(9) | BIT(8) | BIT(7) | BIT(6))
#define REG1617_MP2650_BAT_VOLTAGE_SHIFT                       6
#define REG1617_MP2650_BAT_VOLTAGE_OFFSET                      0
#define REG1617_MP2650_BAT_VOLTAGE_STEP                        12.5      //12.5mV   


/* Address:18h 19h(Read only)*/
#define REG1819_MP2650_ADDRESS                                 0x18

#define REG1819_MP2650_SYS_VOLTAGE_MASK                        (BIT(15) | BIT(14) | BIT(13) | BIT(12) | BIT(11) | BIT(10) | BIT(9) | BIT(8) | BIT(7) | BIT(6))
#define REG1819_MP2650_SYS_VOLTAGE_SHIFT                       6
#define REG1819_MP2650_SYS_VOLTAGE_OFFSET                      0
#define REG1819_MP2650_SYS_VOLTAGE_STEP                        12.5      //12.5mV


/* Address:1Ah 1Bh(Read only)*/
#define REG1A1B_MP2650_ADDRESS                                 0x1A

#define REG1A1B_MP2650_CHARGE_CURRENT_MASK                     (BIT(15) | BIT(14) | BIT(13) | BIT(12) | BIT(11) | BIT(10) | BIT(9) | BIT(8) | BIT(7) | BIT(6))
#define REG1A1B_MP2650_CHARGE_CURRENT_SHIFT                    6
#define REG1A1B_MP2650_CHARGE_CURRENT_OFFSET                   0
#define REG1A1B_MP2650_CHARGE_CURRENT_STEP                     12.5     //12.5mA


/* Address:1Ch 1Dh(Read only)*/
#define REG1C1D_MP2650_ADDRESS                                 0x1C

#define REG1C1D_MP2650_INPUT_VOLTAGE_MASK                      (BIT(15) | BIT(14) | BIT(13) | BIT(12) | BIT(11) | BIT(10) | BIT(9) | BIT(8) | BIT(7) | BIT(6))
#define REG1C1D_MP2650_INPUT_VOLTAGE_SHIFT                     6
#define REG1C1D_MP2650_INPUT_VOLTAGE_OFFSET                    0
#define REG1C1D_MP2650_INPUT_VOLTAGE_STEP                      25      //25mV


/* Address:1Eh 1Fh(Read only)*/
#define REG1E1F_MP2650_ADDRESS                                 0x1E

#define REG1E1F_MP2650_INPUT_CURRENT_MASK                      (BIT(15) | BIT(14) | BIT(13) | BIT(12) | BIT(11) | BIT(10) | BIT(9) | BIT(8) | BIT(7) | BIT(6))
#define REG1E1F_MP2650_INPUT_CURRENT_SHIFT                     6
#define REG1E1F_MP2650_INPUT_CURRENT_OFFSET                    0
#define REG1E1F_MP2650_INPUT_CURRENT_STEP                      6.25    //6.25mA


/* Address:20h 21h(Read only)*/
#define REG2021_MP2650_ADDRESS                                 0x20

#define REG2021_MP2650_OTG_VOLTAGE_MASK                        (BIT(15) | BIT(14) | BIT(13) | BIT(12) | BIT(11) | BIT(10) | BIT(9) | BIT(8) | BIT(7) | BIT(6))
#define REG2021_MP2650_OTG_VOLTAGE_SHIFT                       6
#define REG2021_MP2650_OTG_VOLTAGE_OFFSET                      0
#define REG2021_MP2650_OTG_VOLTAGE_STEP                        25     //25mV
 

/* Address:22h 23h(Read only)*/
#define REG2223_MP2650_ADDRESS                                 0x22

#define REG2223_MP2650_OTG_CURRENT_MASK                        (BIT(15) | BIT(14) | BIT(13) | BIT(12) | BIT(11) | BIT(10) | BIT(9) | BIT(8) | BIT(7) | BIT(6))
#define REG2223_MP2650_OTG_CURRENT_SHIFT                       6
#define REG2223_MP2650_OTG_CURRENT_OFFSET                      0
#define REG2223_MP2650_OTG_CURRENT_STEP                        6.25   //6.25mA

/* Address:24h 25h(Read only)*/
#define REG2425_MP2650_ADDRESS                                 0x24

#define REG2425_MP2650_TEMPERATURE_MASK                        (BIT(15) | BIT(14) | BIT(13) | BIT(12) | BIT(11) | BIT(10) | BIT(9) | BIT(8) | BIT(7) | BIT(6))
#define REG2425_MP2650_TEMPERATURE_SHIFT                       6
#define REG2425_MP2650_TEMPERATURE_OFFSET                      0
#define REG2425_MP2650_TEMPERATURE_STEP                        1    //Temperature = 903 - 2.578 * T


/* Address:26h 27h(Read only)*/
#define REG2627_MP2650_ADDRESS                                 0x26

#define REG2627_MP2650_SYS_POWER_MASK                          (BIT(15) | BIT(14) | BIT(13) | BIT(12) | BIT(11) | BIT(10) | BIT(9) | BIT(8) | BIT(7) | BIT(6))
#define REG2627_MP2650_SYS_POWER_SHIFT                         6
#define REG2627_MP2650_SYS_POWER_OFFSET                        0
#define REG2627_MP2650_SYS_POWER_STEP                          125    //125mW


/* Address:28h 29h(Read only)*/
#define REG2829_MP2650_ADDRESS                                 0x28

#define REG2829_MP2650_DISCHARGE_CURRENT_MASK                  (BIT(15) | BIT(14) | BIT(13) | BIT(12) | BIT(11) | BIT(10) | BIT(9) | BIT(8) | BIT(7) | BIT(6))
#define REG2829_MP2650_SDISCHARGE_CURRENT_SHIFT                6
#define REG2829_MP2650_DISCHARGE_CURRENT_OFFSET                0
#define REG2829_MP2650_SDISCHARGE_CURRENT_STEP                 12.5    //12.5mA

#define REG2F_MP2650_ADDRESS                                 0x2F
#define REG31_MP2650_ADDRESS                                 0x31
#define REG37_MP2650_ADDRESS                                 0x37
#define REG39_MP2650_ADDRESS                                 0x39

#define REG53_MP2650_ADDRESS                                 0x53

/*Addition WPC start*/
#define WPC_CHARGER_INPUT_CURRENT_LIMIT_DEFAULT                1000
#define WPC_TERMINATION_VOLTAGE_DEFAULT                        4370
#define WPC_TERMINATION_VOLTAGE                                WPC_TERMINATION_VOLTAGE_DEFAULT
#define WPC_TERMINATION_CURRENT                                100 //200
#define WPC_CHARGE_CURRENT_DEFAULT                             500        //500mA
#define WPC_RECHARGE_VOLTAGE_OFFSET                            200
/*Addition WPC end*/

enum {
    OVERTIME_AC = 0,
    OVERTIME_USB,
    OVERTIME_DISABLED,
};


struct mp2650_charger {
        struct i2c_client       *client;
        struct device           *dev;
		struct regmap           *regmap;
        int                      hw_aicl_point;
        int                      sw_aicl_point;
        int                      pre_current_ma;
        int                      sw_aicl_result_ma;
        int                      status_irq;
        int                      prochot_irq;

        bool                     otg_enabled;
        bool                     buck_switcher_on;
        bool                     input_suspend;
        bool                     pre_chg_thd_6600;

        int                      mps_otg_en_gpio;
        struct pinctrl          *pinctrl;        
        struct pinctrl_state    *mps_otg_en_active;
        struct pinctrl_state    *mps_otg_en_sleep;
        struct pinctrl_state    *mps_otg_en_default;

        atomic_t                 charger_suspended;
        struct power_supply     *cp_psy;
        struct power_supply     *batt_psy;
		struct delayed_work      dump_reg_work;
};
#endif
