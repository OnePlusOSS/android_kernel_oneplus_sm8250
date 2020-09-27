/**
 * Copyright (C) 2017 - 2018 Bosch Sensortec GmbH
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *
 * Redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the distribution.
 *
 * Neither the name of the copyright holder nor the names of the
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND
 * CONTRIBUTORS "AS IS" AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL COPYRIGHT HOLDER
 * OR CONTRIBUTORS BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY,
 * OR CONSEQUENTIAL DAMAGES(INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 * ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE
 *
 * The information provided is believed to be accurate and reliable.
 * The copyright holder assumes no responsibility
 * for the consequences of use
 * of such information nor for any infringement of patents or
 * other rights of third parties which may result from its use.
 * No license is granted by implication or otherwise under any patent or
 * patent rights of the copyright holder.
 *
 * @file	bmi2_defs.h
 * @date	16 August, 2018
 * @version	1.35.0
 * @brief	Sensor driver for BMI2XY sensor
 *
 */

#ifndef BMI2_DEFS_H_
#define BMI2_DEFS_H_

/******************************************************************************/
/*! @name		Header includes				              */
/******************************************************************************/
#ifdef __KERNEL__
#include <linux/types.h>
#include <linux/kernel.h>
#else
#include <stdint.h>
#include <stddef.h>
#endif

/******************************************************************************/
/*! @name		Common macros					      */
/******************************************************************************/
#ifdef __KERNEL__
#if !defined(UINT8_C) && !defined(INT8_C)
#define INT8_C(x)       S8_C(x)
#define UINT8_C(x)      U8_C(x)
#endif

#if !defined(UINT16_C) && !defined(INT16_C)
#define INT16_C(x)      S16_C(x)
#define UINT16_C(x)     U16_C(x)
#endif

#if !defined(INT32_C) && !defined(UINT32_C)
#define INT32_C(x)      S32_C(x)
#define UINT32_C(x)     U32_C(x)
#endif

#if !defined(INT64_C) && !defined(UINT64_C)
#define INT64_C(x)      S64_C(x)
#define UINT64_C(x)     U64_C(x)
#endif
#endif

/*! @name C standard macros */
#ifndef NULL
#ifdef __cplusplus
#define NULL   0
#else
#define NULL   ((void *) 0)
#endif
#endif

/******************************************************************************/
/*! @name		 General Macro Definitions			      */
/******************************************************************************/
/*! @name  Utility macros */
#define BMI2_SET_BITS(reg_data, bitname, data) \
				((reg_data & ~(bitname##_MASK)) | \
				((data << bitname##_POS) & bitname##_MASK))

#define BMI2_GET_BITS(reg_data, bitname) \
				((reg_data & (bitname##_MASK)) >> \
				(bitname##_POS))

#define BMI2_SET_BIT_POS0(reg_data, bitname, data) \
				((reg_data & ~(bitname##_MASK)) | \
				(data & bitname##_MASK))

#define BMI2_GET_BIT_POS0(reg_data, bitname) (reg_data & (bitname##_MASK))
#define BMI2_SET_BIT_VAL0(reg_data, bitname) (reg_data & ~(bitname##_MASK))

/*! @name For getting LSB and MSB */
#define BMI2_GET_LSB(var)	(uint8_t)(var & BMI2_SET_LOW_BYTE)
#define BMI2_GET_MSB(var)	(uint8_t)((var & BMI2_SET_HIGH_BYTE) >> 8)

/*! @name For defining absolute values */
#define	BMI2_ABS(a)		((a) > 0 ? (a) : -(a))

/*! @name LSB and MSB mask definitions */
#define	BMI2_SET_LOW_BYTE               UINT16_C(0x00FF)
#define	BMI2_SET_HIGH_BYTE              UINT16_C(0xFF00)
#define	BMI2_SET_LOW_NIBBLE             UINT8_C(0x0F)

/*! @name For enable and disable */
#define BMI2_ENABLE                     UINT8_C(1)
#define BMI2_DISABLE                    UINT8_C(0)

/*! @name To define TRUE or FALSE */
#define BMI2_TRUE                       UINT8_C(1)
#define BMI2_FALSE                      UINT8_C(0)

/*! @name To define success code */
#define BMI2_OK                         INT8_C(0)

/*! @name To define error codes */
#define BMI2_E_NULL_PTR                 INT8_C(-1)
#define BMI2_E_COM_FAIL                 INT8_C(-2)
#define BMI2_E_DEV_NOT_FOUND            INT8_C(-3)
#define BMI2_E_OUT_OF_RANGE             INT8_C(-4)
#define BMI2_E_ACC_INVALID_CFG          INT8_C(-5)
#define	BMI2_E_GYRO_INVALID_CFG         INT8_C(-6)
#define	BMI2_E_ACC_GYR_INVALID_CFG      INT8_C(-7)
#define	BMI2_E_INVALID_SENSOR           INT8_C(-8)
#define BMI2_E_CONFIG_LOAD              INT8_C(-9)
#define	BMI2_E_INVALID_PAGE             INT8_C(-10)
#define	BMI2_E_INVALID_FEAT_INT         INT8_C(-11)
#define	BMI2_E_INVALID_INT_PIN          INT8_C(-12)
#define	BMI2_E_SET_APS_FAIL             INT8_C(-13)
#define	BMI2_E_AUX_INVALID_CFG          INT8_C(-14)
#define	BMI2_E_AUX_BUSY                 INT8_C(-15)
#define BMI2_E_SELF_TEST_FAIL           INT8_C(-16)
#define BMI2_E_REMAP_ERROR              INT8_C(-17)
#define BMI2_E_GYR_USER_GAIN_UPD_FAIL	INT8_C(-18)
#define BMI2_E_SELF_TEST_NOT_DONE       INT8_C(-19)

/*! @name To define warnings for FIFO activity */
#define BMI2_W_FIFO_EMPTY               INT8_C(1)
#define BMI2_W_PARTIAL_READ             INT8_C(2)

/*! @name Bit wise to define information */
#define BMI2_I_MIN_VALUE                UINT8_C(1)
#define BMI2_I_MAX_VALUE                UINT8_C(2)

/*! @name BMI2 register addresses */
#define BMI2_CHIP_ID_ADDR               UINT8_C(0x00)
#define BMI2_STATUS_ADDR                UINT8_C(0x03)
#define BMI2_AUX_X_LSB_ADDR             UINT8_C(0x04)
#define BMI2_ACC_X_LSB_ADDR             UINT8_C(0x0C)
#define BMI2_GYR_X_LSB_ADDR             UINT8_C(0x12)
#define BMI2_EVENT_ADDR                 UINT8_C(0x1B)
#define BMI2_INT_STATUS_0_ADDR          UINT8_C(0x1C)
#define BMI2_INT_STATUS_1_ADDR          UINT8_C(0x1D)
#define BMI2_STEP_COUNT_OUT_0_ADDR      UINT8_C(0x1E)
#define BMI2_SYNC_COMMAND_ADDR          UINT8_C(0x1E)
#define BMI2_INTERNAL_STATUS_ADDR       UINT8_C(0x21)
#define	BMI2_FIFO_LENGTH_0_ADDR         UINT8_C(0X24)
#define	BMI2_FIFO_DATA_ADDR             UINT8_C(0X26)
#define BMI2_FEAT_PAGE_ADDR             UINT8_C(0x2F)
#define BMI2_FEATURES_REG_ADDR          UINT8_C(0x30)
#define BMI2_ACC_CONF_ADDR              UINT8_C(0x40)
#define BMI2_GYR_CONF_ADDR              UINT8_C(0x42)
#define BMI2_AUX_CONF_ADDR              UINT8_C(0x44)
#define	BMI2_FIFO_DOWNS_ADDR            UINT8_C(0X45)
#define	BMI2_FIFO_WTM_0_ADDR            UINT8_C(0X46)
#define	BMI2_FIFO_WTM_1_ADDR            UINT8_C(0X47)
#define	BMI2_FIFO_CONFIG_0_ADDR         UINT8_C(0X48)
#define	BMI2_FIFO_CONFIG_1_ADDR         UINT8_C(0X49)
#define BMI2_AUX_DEV_ID_ADDR            UINT8_C(0x4B)
#define BMI2_AUX_IF_CONF_ADDR           UINT8_C(0x4C)
#define BMI2_AUX_RD_ADDR                UINT8_C(0x4D)
#define BMI2_AUX_WR_ADDR                UINT8_C(0x4E)
#define BMI2_AUX_WR_DATA_ADDR           UINT8_C(0x4F)
#define	BMI2_INT1_IO_CTRL_ADDR          UINT8_C(0x53)
#define	BMI2_INT2_IO_CTRL_ADDR          UINT8_C(0x54)
#define	BMI2_INT1_MAP_FEAT_ADDR         UINT8_C(0x56)
#define	BMI2_INT2_MAP_FEAT_ADDR         UINT8_C(0x57)
#define	BMI2_INT_MAP_DATA_ADDR          UINT8_C(0x58)
#define BMI2_INIT_CTRL_ADDR             UINT8_C(0x59)
#define BMI2_INIT_ADDR_0                UINT8_C(0x5B)
#define BMI2_INIT_ADDR_1                UINT8_C(0x5C)
#define	BMI2_INIT_DATA_ADDR             UINT8_C(0x5E)
#define	BMI2_IF_CONF_ADDR               UINT8_C(0X6B)
#define	BMI2_ACC_SELF_TEST_ADDR         UINT8_C(0X6D)
#define	BMI2_SELF_TEST_MEMS_ADDR        UINT8_C(0X6F)
#define	BMI2_NV_CONF_ADDR               UINT8_C(0x70)
#define BMI2_ACC_OFF_COMP_0_ADDR        UINT8_C(0X71)
#define	BMI2_GYR_OFF_COMP_3_ADDR        UINT8_C(0X74)
#define	BMI2_GYR_OFF_COMP_6_ADDR        UINT8_C(0X77)
#define	BMI2_GYR_USR_GAIN_0_ADDR        UINT8_C(0X78)
#define	BMI2_PWR_CONF_ADDR              UINT8_C(0x7C)
#define BMI2_PWR_CTRL_ADDR              UINT8_C(0x7D)
#define BMI2_CMD_REG_ADDR               UINT8_C(0x7E)

/*! @name BMI2 I2C address */
#define BMI2_I2C_PRIM_ADDR              UINT8_C(0x68)
#define BMI2_I2C_SEC_ADDR               UINT8_C(0x69)

/*! @name BMI2 Commands */
#define BMI2_USR_GAIN_CMD               UINT8_C(0x03)
#define BMI2_SOFT_RESET_CMD             UINT8_C(0xB6)
#define BMI2_FIFO_FLUSH_CMD             UINT8_C(0xB0)

/*! @name BMI2 sensor data bytes */
#define BMI2_ACC_GYR_NUM_BYTES          UINT8_C(6)
#define	BMI2_AUX_NUM_BYTES              UINT8_C(8)
#define	BMI2_CONFIG_FILE_SIZE           UINT16_C(8192)
#define BMI2_FEAT_SIZE_IN_BYTES         UINT8_C(16)
#define	BMI2_ACC_CONFIG_LENGTH          UINT8_C(2)

/*! @name BMI2 configuration load status */
#define BMI2_CONFIG_LOAD_SUCCESS        UINT8_C(1)

/*! @name To define BMI2 pages */
#define BMI2_PAGE_0                     UINT8_C(0)
#define	BMI2_PAGE_1                     UINT8_C(1)
#define	BMI2_PAGE_2                     UINT8_C(2)
#define	BMI2_PAGE_3                     UINT8_C(3)
#define	BMI2_PAGE_4                     UINT8_C(4)
#define	BMI2_PAGE_5                     UINT8_C(5)
#define	BMI2_PAGE_6                     UINT8_C(6)
#define	BMI2_PAGE_7                     UINT8_C(7)

/*! @name Array Parameter DefinItions */
#define BMI2_SENSOR_TIME_LSB_BYTE       UINT8_C(0)
#define BMI2_SENSOR_TIME_XLSB_BYTE      UINT8_C(1)
#define BMI2_SENSOR_TIME_MSB_BYTE       UINT8_C(2)

/*! @name Mask definitions for SPI read/write address */
#define BMI2_SPI_RD_MASK                UINT8_C(0x80)
#define BMI2_SPI_WR_MASK                UINT8_C(0x7F)

/*! @name Mask definitions for power configuration register */
#define BMI2_ADV_POW_EN_MASK            UINT8_C(0x01)

/*! @name Mask definitions for initialization control register */
#define BMI2_CONF_LOAD_EN_MASK          UINT8_C(0x01)

/*! @name Mask definitions for power control register */
#define BMI2_AUX_EN_MASK                UINT8_C(0x01)
#define BMI2_GYR_EN_MASK                UINT8_C(0x02)
#define BMI2_ACC_EN_MASK                UINT8_C(0x04)
#define BMI2_TEMP_EN_MASK               UINT8_C(0x08)

/*! @name Bit position definitions for power control register */
#define BMI2_GYR_EN_POS                 UINT8_C(0x01)
#define BMI2_ACC_EN_POS                 UINT8_C(0x02)
#define BMI2_TEMP_EN_POS                UINT8_C(0x03)

/*! @name Mask definitions for sensor event flags */
#define BMI2_EVENT_FLAG_MASK            UINT8_C(0x1C)

/*! @name Bit position definitions for sensor event flags */
#define BMI2_EVENT_FLAG_POS             UINT8_C(0x02)

/*! @name Mask definitions to switch page */
#define BMI2_SWITCH_PAGE_EN_MASK        UINT8_C(0x07)

/*! @name Accelerometer and Gyroscope Filter/Noise performance modes */
/* Power optimized mode */
#define BMI2_POWER_OPT_MODE             UINT8_C(0)
/* Performance optimized  */
#define BMI2_PERF_OPT_MODE              UINT8_C(1)

/*! @name Mask definitions of NVM register */
#define	BMI2_NV_ACC_OFFSET_MASK         UINT8_C(0x08)

/*! @name Bit position definitions of NVM register */
#define	BMI2_NV_ACC_OFFSET_POS          UINT8_C(0x03)

/*! @name Sensor status */
#define BMI2_DRDY_ACC                   UINT8_C(0x80)
#define BMI2_DRDY_GYR                   UINT8_C(0x40)
#define BMI2_DRDY_AUX                   UINT8_C(0x20)
#define BMI2_CMD_RDY                    UINT8_C(0x10)
#define BMI2_AUX_BUSY                   UINT8_C(0x04)

/*! @name Macro to define accelerometer configuration value for FOC */
#define BMI2_FOC_ACC_CONF_VAL           UINT8_C(0xB7)

/*! @name Macro to define gyroscope configuration value for FOC */
#define BMI2_FOC_GYR_CONF_VAL           UINT8_C(0xB6)

/*! @name Macro to define X Y and Z axis for an array */
#define	BMI2_X_AXIS                     UINT8_C(0)
#define	BMI2_Y_AXIS                     UINT8_C(1)
#define	BMI2_Z_AXIS                     UINT8_C(2)

/******************************************************************************/
/*! @name		 Sensor Macro Definitions			      */
/******************************************************************************/
/*!  @name Macros to define BMI2 sensor/feature types */
#define	BMI2_ACCEL                      UINT8_C(0)
#define	BMI2_GYRO                       UINT8_C(1)
#define	BMI2_AUX                        UINT8_C(2)
#define	BMI2_TEMP                       UINT8_C(3)
#define	BMI2_ANY_MOTION                 UINT8_C(4)
#define	BMI2_NO_MOTION                  UINT8_C(5)
#define	BMI2_TILT                       UINT8_C(6)
#define	BMI2_ORIENTATION                UINT8_C(7)
#define	BMI2_SIG_MOTION                 UINT8_C(8)
#define	BMI2_STEP_DETECTOR              UINT8_C(9)
#define	BMI2_STEP_COUNTER               UINT8_C(10)
#define BMI2_STEP_ACTIVITY              UINT8_C(11)
#define BMI2_GYRO_GAIN_UPDATE           UINT8_C(12)
#define	BMI2_PICK_UP                    UINT8_C(13)
#define	BMI2_GLANCE_DETECTOR            UINT8_C(14)
#define	BMI2_WAKE_UP                    UINT8_C(15)
#define	BMI2_HIGH_G                     UINT8_C(16)
#define	BMI2_LOW_G                      UINT8_C(17)
#define	BMI2_FLAT                       UINT8_C(18)
#define BMI2_EXT_SENS_SYNC              UINT8_C(19)
#define BMI2_GYRO_SELF_OFF              UINT8_C(20)
#define BMI2_WRIST_GESTURE              UINT8_C(21)
#define BMI2_WRIST_WEAR_WAKE_UP         UINT8_C(22)
#define BMI2_ACTIVITY_RECOG             UINT8_C(23)

/*! @name Bit wise for selecting BMI2 sensors/features */
#define BMI2_ACCEL_SENS_SEL             (1)
#define BMI2_GYRO_SENS_SEL              (1 << BMI2_GYRO)
#define BMI2_AUX_SENS_SEL               (1 << BMI2_AUX)
#define BMI2_TEMP_SENS_SEL              (1 << BMI2_TEMP)
#define BMI2_ANY_MOT_SEL                (1 << BMI2_ANY_MOTION)
#define BMI2_NO_MOT_SEL                 (1 << BMI2_NO_MOTION)
#define BMI2_TILT_SEL                   (1 << BMI2_TILT)
#define BMI2_ORIENT_SEL                 (1 << BMI2_ORIENTATION)
#define BMI2_SIG_MOTION_SEL             (1 << BMI2_SIG_MOTION)
#define BMI2_STEP_DETECT_SEL            (1 << BMI2_STEP_DETECTOR)
#define BMI2_STEP_COUNT_SEL             (1 << BMI2_STEP_COUNTER)
#define BMI2_STEP_ACT_SEL               (1 << BMI2_STEP_ACTIVITY)
#define BMI2_GYRO_GAIN_UPDATE_SEL       (1 << BMI2_GYRO_GAIN_UPDATE)
#define BMI2_PICK_UP_SEL                (1 << BMI2_PICK_UP)
#define BMI2_GLANCE_DET_SEL             (1 << BMI2_GLANCE_DETECTOR)
#define BMI2_WAKE_UP_SEL                (1 << BMI2_WAKE_UP)
#define BMI2_HIGH_G_SEL                 (1 << BMI2_HIGH_G)
#define BMI2_LOW_G_SEL                  (1 << BMI2_LOW_G)
#define BMI2_FLAT_SEL                   (1 << BMI2_FLAT)
#define BMI2_EXT_SENS_SEL               (1 << BMI2_EXT_SENS_SYNC)
#define BMI2_GYRO_SELF_OFF_SEL          (1 << BMI2_GYRO_SELF_OFF)
#define BMI2_WRIST_GEST_SEL             (1 << BMI2_WRIST_GESTURE)
#define BMI2_WRIST_WEAR_WAKE_UP_SEL     (1 << BMI2_WRIST_WEAR_WAKE_UP)
#define BMI2_ACTIVITY_RECOG_SEL         (1 << BMI2_ACTIVITY_RECOG)

/*! @name Bit wise selection of BMI2 sensors */
#define BMI2_MAIN_SENSORS	(BMI2_ACCEL_SENS_SEL | BMI2_GYRO_SENS_SEL \
				| BMI2_AUX_SENS_SEL | BMI2_TEMP_SENS_SEL)

/*!  @name Macro to define axis-re-mapping feature for BMI2 */
#define BMI2_AXIS_MAP                   UINT8_C(24)

/*!  @name Macro to define the step counter/detector parameters for BMI2 */
#define BMI2_STEP_CNT_PARAMS            UINT8_C(25)

/*!  @name Macro to define error status for NVM and virtual frames */
#define BMI2_NVM_STATUS                 UINT8_C(26)
#define BMI2_VFRM_STATUS                UINT8_C(27)

/*!  @name Macro to define gyroscope cross-sensitivity */
#define BMI2_GYRO_CROSS_SENSE           UINT8_C(28)

/*!  @name Macro to define maximum number of available sensors/features */
#define	BMI2_SENS_MAX_NUM               UINT8_C(29)

/*!  @name Maximum number of BMI2 main sensors */
#define	BMI2_MAIN_SENS_MAX_NUM          UINT8_C(4)

/*!  @name Macro to select between single and double tap */
#define	BMI2_DOUBLE_TAP_SEL             UINT8_C(0)
#define	BMI2_SINGLE_TAP_SEL             UINT8_C(1)

/******************************************************************************/
/*! @name		Accelerometer Macro Definitions			      */
/******************************************************************************/
/*! @name Accelerometer Bandwidth parameters */
#define BMI2_ACC_OSR4_AVG1              UINT8_C(0x00)
#define BMI2_ACC_OSR2_AVG2              UINT8_C(0x01)
#define	BMI2_ACC_NORMAL_AVG4            UINT8_C(0x02)
#define	BMI2_ACC_CIC_AVG8               UINT8_C(0x03)
#define	BMI2_ACC_RES_AVG16              UINT8_C(0x04)
#define	BMI2_ACC_RES_AVG32              UINT8_C(0x05)
#define	BMI2_ACC_RES_AVG64              UINT8_C(0x06)
#define	BMI2_ACC_RES_AVG128             UINT8_C(0x07)

/*! @name Accelerometer Output Data Rate */
#define	BMI2_ACC_ODR_0_78HZ             UINT8_C(0x01)
#define	BMI2_ACC_ODR_1_56HZ             UINT8_C(0x02)
#define	BMI2_ACC_ODR_3_12HZ             UINT8_C(0x03)
#define	BMI2_ACC_ODR_6_25HZ             UINT8_C(0x04)
#define	BMI2_ACC_ODR_12_5HZ             UINT8_C(0x05)
#define	BMI2_ACC_ODR_25HZ               UINT8_C(0x06)
#define	BMI2_ACC_ODR_50HZ               UINT8_C(0x07)
#define	BMI2_ACC_ODR_100HZ              UINT8_C(0x08)
#define	BMI2_ACC_ODR_200HZ              UINT8_C(0x09)
#define	BMI2_ACC_ODR_400HZ              UINT8_C(0x0A)
#define	BMI2_ACC_ODR_800HZ              UINT8_C(0x0B)
#define	BMI2_ACC_ODR_1600HZ             UINT8_C(0x0C)

/*! @name Accelerometer G Range */
#define	BMI2_ACC_RANGE_2G               UINT8_C(0x00)
#define	BMI2_ACC_RANGE_4G               UINT8_C(0x01)
#define	BMI2_ACC_RANGE_8G               UINT8_C(0x02)
#define	BMI2_ACC_RANGE_16G              UINT8_C(0x03)

/*! @name Mask definitions for accelerometer configuration register */
#define BMI2_ACC_RANGE_MASK             UINT8_C(0x03)
#define BMI2_ACC_ODR_MASK               UINT8_C(0x0F)
#define BMI2_ACC_BW_PARAM_MASK          UINT8_C(0x70)
#define BMI2_ACC_FILTER_PERF_MODE_MASK  UINT8_C(0x80)

/*! @name Bit position definitions for accelerometer configuration register */
#define BMI2_ACC_BW_PARAM_POS           UINT8_C(0x04)
#define BMI2_ACC_FILTER_PERF_MODE_POS   UINT8_C(0x07)

/*! @name Self test macro to define range */
#define BMI2_ACC_SELF_TEST_RANGE        UINT8_C(16)

/*! @name Self test macro to show resulting minimum and maximum difference
	signal of the axes in mg */
#define	BMI2_ST_ACC_X_SIG_MIN_DIFF      INT16_C(1800)
#define	BMI2_ST_ACC_X_SIG_MAX_DIFF      INT16_C(10200)

#define	BMI2_ST_ACC_Y_SIG_MIN_DIFF      INT16_C(-10200)
#define	BMI2_ST_ACC_Y_SIG_MAX_DIFF      INT16_C(-1800)

#define	BMI2_ST_ACC_Z_SIG_MIN_DIFF      INT16_C(800)
#define	BMI2_ST_ACC_Z_SIG_MAX_DIFF      INT16_C(10200)

/*! @name Mask definitions for accelerometer self-test */
#define	BMI2_ACC_SELF_TEST_EN_MASK      UINT8_C(0x01)
#define	BMI2_ACC_SELF_TEST_SIGN_MASK    UINT8_C(0x04)
#define	BMI2_ACC_SELF_TEST_AMP_MASK     UINT8_C(0x08)

/*! @name Bit Positions for accelerometer self-test */
#define	BMI2_ACC_SELF_TEST_SIGN_POS     UINT8_C(0x02)
#define	BMI2_ACC_SELF_TEST_AMP_POS      UINT8_C(0x03)

/*! @name Mask definitions for gyroscope MEMS self-test */
#define	BMI2_GYR_ST_MEMS_START_MASK     UINT8_C(0x01)
#define	BMI2_GYR_MEMS_OK_MASK           UINT8_C(0x02)

/*! @name Bit Positions for gyroscope MEMS self-test */
#define	BMI2_GYR_MEMS_OK_POS            UINT8_C(0x01)

/******************************************************************************/
/*! @name		Gyroscope Macro Definitions			      */
/******************************************************************************/
/*! @name Gyroscope Bandwidth parameters */
#define BMI2_GYR_OSR4_MODE              UINT8_C(0x00)
#define BMI2_GYR_OSR2_MODE              UINT8_C(0x01)
#define	BMI2_GYR_NORMAL_MODE            UINT8_C(0x02)
#define	BMI2_GYR_CIC_MODE               UINT8_C(0x03)

/*! @name Gyroscope Output Data Rate */
#define	BMI2_GYR_ODR_25HZ               UINT8_C(0x06)
#define	BMI2_GYR_ODR_50HZ               UINT8_C(0x07)
#define	BMI2_GYR_ODR_100HZ              UINT8_C(0x08)
#define	BMI2_GYR_ODR_200HZ              UINT8_C(0x09)
#define	BMI2_GYR_ODR_400HZ              UINT8_C(0x0A)
#define	BMI2_GYR_ODR_800HZ              UINT8_C(0x0B)
#define	BMI2_GYR_ODR_1600HZ             UINT8_C(0x0C)
#define	BMI2_GYR_ODR_3200HZ             UINT8_C(0x0D)

/*! @name Gyroscope OIS Range */
#define	BMI2_GYR_OIS_250                UINT8_C(0x00)
#define	BMI2_GYR_OIS_2000               UINT8_C(0x01)

/*! @name Gyroscope Angular Rate Measurement Range */
#define	BMI2_GYR_RANGE_2000             UINT8_C(0x00)
#define	BMI2_GYR_RANGE_1000             UINT8_C(0x01)
#define	BMI2_GYR_RANGE_500              UINT8_C(0x02)
#define	BMI2_GYR_RANGE_250              UINT8_C(0x03)
#define	BMI2_GYR_RANGE_125              UINT8_C(0x04)

/*! @name Mask definitions for gyroscope configuration register */
#define BMI2_GYR_RANGE_MASK             UINT8_C(0x07)
#define BMI2_GYR_OIS_RANGE_MASK         UINT8_C(0x08)
#define BMI2_GYR_ODR_MASK               UINT8_C(0x0F)
#define BMI2_GYR_BW_PARAM_MASK          UINT8_C(0x30)
#define BMI2_GYR_NOISE_PERF_MODE_MASK   UINT8_C(0x40)
#define BMI2_GYR_FILTER_PERF_MODE_MASK  UINT8_C(0x80)

/*! @name Bit position definitions for gyroscope configuration register */
#define BMI2_GYR_OIS_RANGE_POS          UINT8_C(0x03)
#define BMI2_GYR_BW_PARAM_POS           UINT8_C(0x04)
#define BMI2_GYR_NOISE_PERF_MODE_POS    UINT8_C(0x06)
#define BMI2_GYR_FILTER_PERF_MODE_POS   UINT8_C(0x07)

/******************************************************************************/
/*! @name		Auxiliary Macro Definitions			      */
/******************************************************************************/
/*! @name Auxiliary Output Data Rate */
#define BMI2_AUX_ODR_RESERVED           UINT8_C(0x00)
#define BMI2_AUX_ODR_0_78HZ             UINT8_C(0x01)
#define BMI2_AUX_ODR_1_56HZ             UINT8_C(0x02)
#define BMI2_AUX_ODR_3_12HZ             UINT8_C(0x03)
#define BMI2_AUX_ODR_6_25HZ             UINT8_C(0x04)
#define BMI2_AUX_ODR_12_5HZ             UINT8_C(0x05)
#define BMI2_AUX_ODR_25HZ               UINT8_C(0x06)
#define BMI2_AUX_ODR_50HZ               UINT8_C(0x07)
#define BMI2_AUX_ODR_100HZ              UINT8_C(0x08)
#define BMI2_AUX_ODR_200HZ              UINT8_C(0x09)
#define BMI2_AUX_ODR_400HZ              UINT8_C(0x0A)
#define BMI2_AUX_ODR_800HZ              UINT8_C(0x0B)

/*! @name Macro to define burst read lengths for both manual and auto modes */
#define BMI2_AUX_READ_LEN_0             UINT8_C(0x00)
#define	BMI2_AUX_READ_LEN_1             UINT8_C(0x01)
#define	BMI2_AUX_READ_LEN_2             UINT8_C(0x02)
#define	BMI2_AUX_READ_LEN_3             UINT8_C(0x03)

/*! @name Mask definitions for auxiliary interface configuration register */
#define BMI2_AUX_SET_I2C_ADDR_MASK      UINT8_C(0xFE)
#define BMI2_AUX_MAN_MODE_EN_MASK       UINT8_C(0x80)
#define BMI2_AUX_FCU_WR_EN_MASK         UINT8_C(0x40)
#define BMI2_AUX_MAN_READ_BURST_MASK    UINT8_C(0x0C)
#define BMI2_AUX_READ_BURST_MASK        UINT8_C(0x03)
#define BMI2_AUX_ODR_EN_MASK            UINT8_C(0x0F)
#define BMI2_AUX_OFFSET_READ_OUT_MASK   UINT8_C(0xF0)

/*! @name Bit positions for auxiliary interface configuration register */
#define BMI2_AUX_SET_I2C_ADDR_POS       UINT8_C(0x01)
#define BMI2_AUX_MAN_MODE_EN_POS        UINT8_C(0x07)
#define BMI2_AUX_FCU_WR_EN_POS          UINT8_C(0x06)
#define BMI2_AUX_MAN_READ_BURST_POS     UINT8_C(0x02)
#define BMI2_AUX_OFFSET_READ_OUT_POS    UINT8_C(0x04)

/******************************************************************************/
/*! @name       FIFO Macro Definitions                                        */
/******************************************************************************/
/*! @name Macros to define virtual FIFO frame mode */
#define BMI2_FIFO_VIRT_FRM_MODE         UINT8_C(0x03)

/*! @name FIFO Header Mask definitions */
#define BMI2_FIFO_HEADER_ACC_FRM        UINT8_C(0x84)
#define BMI2_FIFO_HEADER_AUX_FRM        UINT8_C(0x90)
#define BMI2_FIFO_HEADER_GYR_FRM        UINT8_C(0x88)
#define	BMI2_FIFO_HEADER_GYR_ACC_FRM    UINT8_C(0x8C)
#define	BMI2_FIFO_HEADER_AUX_ACC_FRM    UINT8_C(0x94)
#define	BMI2_FIFO_HEADER_AUX_GYR_FRM    UINT8_C(0x98)
#define BMI2_FIFO_HEADER_ALL_FRM        UINT8_C(0x9C)
#define BMI2_FIFO_HEADER_SENS_TIME_FRM  UINT8_C(0x44)
#define BMI2_FIFO_HEADER_SKIP_FRM       UINT8_C(0x40)
#define BMI2_FIFO_HEADER_INPUT_CFG_FRM  UINT8_C(0x48)
#define BMI2_FIFO_HEAD_OVER_READ_MSB    UINT8_C(0x80)
#define BMI2_FIFO_VIRT_ACT_RECOG_FRM    UINT8_C(0xC8)

/*! @name BMI2 sensor selection for header-less frames  */
#define	BMI2_FIFO_HEAD_LESS_ACC_FRM     UINT8_C(0x40)
#define	BMI2_FIFO_HEAD_LESS_AUX_FRM     UINT8_C(0x20)
#define	BMI2_FIFO_HEAD_LESS_GYR_FRM     UINT8_C(0x80)
#define	BMI2_FIFO_HEAD_LESS_GYR_AUX_FRM UINT8_C(0xA0)
#define	BMI2_FIFO_HEAD_LESS_GYR_ACC_FRM UINT8_C(0xC0)
#define	BMI2_FIFO_HEAD_LESS_AUX_ACC_FRM UINT8_C(0x60)
#define	BMI2_FIFO_HEAD_LESS_ALL_FRM     UINT8_C(0xE0)

/*! @name Mask definitions for FIFO frame content configuration */
#define	BMI2_FIFO_STOP_ON_FULL          UINT16_C(0x0001)
#define	BMI2_FIFO_TIME_EN               UINT16_C(0x0002)
#define	BMI2_FIFO_TAG_INT1              UINT16_C(0x0300)
#define	BMI2_FIFO_TAG_INT2              UINT16_C(0x0C00)
#define	BMI2_FIFO_HEADER_EN             UINT16_C(0x1000)
#define	BMI2_FIFO_AUX_EN                UINT16_C(0x2000)
#define	BMI2_FIFO_ACC_EN                UINT16_C(0x4000)
#define	BMI2_FIFO_GYR_EN                UINT16_C(0x8000)
#define	BMI2_FIFO_ALL_EN                UINT16_C(0xE000)

/*! @name FIFO sensor data lengths */
#define	BMI2_FIFO_ACC_LENGTH            UINT8_C(6)
#define	BMI2_FIFO_GYR_LENGTH            UINT8_C(6)
#define	BMI2_FIFO_AUX_LENGTH            UINT8_C(8)
#define	BMI2_FIFO_ACC_AUX_LENGTH        UINT8_C(14)
#define BMI2_FIFO_GYR_AUX_LENGTH        UINT8_C(14)
#define BMI2_FIFO_ACC_GYR_LENGTH        UINT8_C(12)
#define BMI2_FIFO_ALL_LENGTH            UINT8_C(20)
#define BMI2_SENSOR_TIME_LENGTH         UINT8_C(3)
#define	BMI2_FIFO_CONFIG_LENGTH         UINT8_C(2)
#define	BMI2_FIFO_WM_LENGTH             UINT8_C(2)
#define	BMI2_MAX_VALUE_FIFO_FILTER      UINT8_C(1)
#define	BMI2_FIFO_DATA_LENGTH           UINT8_C(2)
#define	BMI2_FIFO_LENGTH_MSB_BYTE       UINT8_C(1)
#define	BMI2_FIFO_INPUT_CFG_LENGTH      UINT8_C(4)
#define	BMI2_FIFO_SKIP_FRM_LENGTH       UINT8_C(1)

/*! @name FIFO sensor virtual data lengths: sensor data plus sensor time */
#define	BMI2_FIFO_VIRT_ACC_LENGTH       UINT8_C(9)
#define	BMI2_FIFO_VIRT_GYR_LENGTH       UINT8_C(9)
#define	BMI2_FIFO_VIRT_AUX_LENGTH       UINT8_C(11)
#define	BMI2_FIFO_VIRT_ACC_AUX_LENGTH   UINT8_C(17)
#define BMI2_FIFO_VIRT_GYR_AUX_LENGTH   UINT8_C(17)
#define BMI2_FIFO_VIRT_ACC_GYR_LENGTH   UINT8_C(15)
#define BMI2_FIFO_VIRT_ALL_LENGTH       UINT8_C(23)

/*! @name FIFO sensor virtual data lengths: activity recognition */
#define	BMI2_FIFO_VIRT_ACT_DATA_LENGTH	UINT8_C(6)
#define	BMI2_FIFO_VIRT_ACT_TIME_LENGTH	UINT8_C(4)
#define	BMI2_FIFO_VIRT_ACT_TYPE_LENGTH	UINT8_C(1)
#define	BMI2_FIFO_VIRT_ACT_STAT_LENGTH	UINT8_C(1)

/*! @name BMI2 FIFO data filter modes */
#define BMI2_FIFO_UNFILTERED_DATA       UINT8_C(0)
#define BMI2_FIFO_FILTERED_DATA         UINT8_C(1)

/*! @name FIFO frame masks */
#define BMI2_FIFO_LSB_CONFIG_CHECK      UINT8_C(0x00)
#define BMI2_FIFO_MSB_CONFIG_CHECK      UINT8_C(0x80)
#define	BMI2_FIFO_TAG_INTR_MASK         UINT8_C(0xFF)

/*! @name BMI2 Mask definitions of FIFO configuration registers */
#define	BMI2_FIFO_CONFIG_0_MASK         UINT16_C(0x0003)
#define	BMI2_FIFO_CONFIG_1_MASK         UINT16_C(0xFF00)

/*! @name FIFO self wake-up mask definition */
#define	BMI2_FIFO_SELF_WAKE_UP_MASK     UINT8_C(0x02)

/*! @name FIFO down sampling mask definition */
#define	BMI2_ACC_FIFO_DOWNS_MASK        UINT8_C(0x70)
#define	BMI2_GYR_FIFO_DOWNS_MASK        UINT8_C(0x07)

/*! @name FIFO down sampling bit positions */
#define	BMI2_ACC_FIFO_DOWNS_POS         UINT8_C(0x04)

/*! @name FIFO filter mask definition */
#define	BMI2_ACC_FIFO_FILT_DATA_MASK	UINT8_C(0x80)
#define	BMI2_GYR_FIFO_FILT_DATA_MASK	UINT8_C(0x08)

/*! @name FIFO filter bit positions */
#define	BMI2_ACC_FIFO_FILT_DATA_POS     UINT8_C(0x07)
#define	BMI2_GYR_FIFO_FILT_DATA_POS     UINT8_C(0x03)

/*! @name FIFO byte counter mask definition */
#define	BMI2_FIFO_BYTE_COUNTER_MSB_MASK UINT8_C(0x3F)

/*! @name FIFO self wake-up bit positions */
#define	BMI2_FIFO_SELF_WAKE_UP_POS      UINT8_C(0x01)

/*! @name Mask Definitions for Virtual FIFO frames */
#define BMI2_FIFO_VIRT_FRM_MODE_MASK    UINT8_C(0xC0)
#define BMI2_FIFO_VIRT_PAYLOAD_MASK     UINT8_C(0x3C)

/*! @name Bit Positions for Virtual FIFO frames */
#define BMI2_FIFO_VIRT_FRM_MODE_POS     UINT8_C(0x06)
#define BMI2_FIFO_VIRT_PAYLOAD_POS      UINT8_C(0x02)

/******************************************************************************/
/*! @name		 Interrupt Macro Definitions			      */
/******************************************************************************/
/*! @name BMI2 Interrupt Modes */
/* Non latched */
#define BMI2_INT_NON_LATCH              UINT8_C(0)
/* Permanently latched */
#define BMI2_INT_LATCH                  UINT8_C(1)

/*! @name BMI2 Interrupt Pin Behavior */
#define	BMI2_INT_PUSH_PULL              UINT8_C(0)
#define	BMI2_INT_OPEN_DRAIN             UINT8_C(1)

/*! @name BMI2 Interrupt Pin Level */
#define	BMI2_INT_ACTIVE_LOW             UINT8_C(0)
#define	BMI2_INT_ACTIVE_HIGH            UINT8_C(1)

/*! @name BMI2 Interrupt Output Enable */
#define	BMI2_INT_OUTPUT_DISABLE         UINT8_C(0)
#define	BMI2_INT_OUTPUT_ENABLE          UINT8_C(1)

/*! @name BMI2 Interrupt Input Enable */
#define	BMI2_INT_INPUT_DISABLE          UINT8_C(0)
#define	BMI2_INT_INPUT_ENABLE           UINT8_C(1)

/*! @name Mask definitions for interrupt pin configuration */
#define	BMI2_INT_LATCH_MASK             UINT8_C(0x01)
#define	BMI2_INT_LEVEL_MASK             UINT8_C(0x02)
#define	BMI2_INT_OPEN_DRAIN_MASK        UINT8_C(0x04)
#define	BMI2_INT_OUTPUT_EN_MASK         UINT8_C(0x08)
#define	BMI2_INT_INPUT_EN_MASK          UINT8_C(0x10)

/*! @name Bit position definitions for interrupt pin configuration */
#define	BMI2_INT_LEVEL_POS              UINT8_C(0x01)
#define	BMI2_INT_OPEN_DRAIN_POS         UINT8_C(0x02)
#define	BMI2_INT_OUTPUT_EN_POS          UINT8_C(0x03)
#define	BMI2_INT_INPUT_EN_POS           UINT8_C(0x04)

/*! @name Mask definitions for data interrupt mapping */
#define BMI2_FFULL_INT                  UINT8_C(0x01)
#define BMI2_FWM_INT                    UINT8_C(0x02)
#define BMI2_DRDY_INT                   UINT8_C(0x04)
#define BMI2_ERR_INT                    UINT8_C(0x08)

/*! @name Mask definitions for data interrupt status bits */
#define	BMI2_FFULL_INT_STATUS_MASK      UINT16_C(0x0100)
#define	BMI2_FWM_INT_STATUS_MASK        UINT16_C(0x0200)
#define	BMI2_ERR_INT_STATUS_MASK        UINT16_C(0x0400)
#define BMI2_AUX_DRDY_INT_MASK          UINT16_C(0x2000)
#define BMI2_GYR_DRDY_INT_MASK          UINT16_C(0x4000)
#define BMI2_ACC_DRDY_INT_MASK          UINT16_C(0x8000)

/*!  @name Maximum number of interrupt pins */
#define	BMI2_INT_PIN_MAX_NUM            UINT8_C(2)

/*!  @name Macro for mapping feature interrupts */
#define	BMI2_FEAT_INT_DISABLE           UINT8_C(0)
#define	BMI2_FEAT_INTA                  UINT8_C(1)
#define	BMI2_FEAT_INTB                  UINT8_C(2)
#define	BMI2_FEAT_INTC                  UINT8_C(3)
#define	BMI2_FEAT_INTD                  UINT8_C(4)
#define	BMI2_FEAT_INTE                  UINT8_C(5)
#define	BMI2_FEAT_INTF                  UINT8_C(6)
#define	BMI2_FEAT_INTG                  UINT8_C(7)
#define	BMI2_FEAT_INTH                  UINT8_C(8)
#define	BMI2_FEAT_INT_MAX               UINT8_C(9)

/******************************************************************************/
/*! @name               OIS Interface Macro Definitions                       */
/******************************************************************************/
/*! @name Mask definitions for interface configuration register */
#define BMI2_OIS_IF_EN_MASK             UINT8_C(0x10)
#define BMI2_AUX_IF_EN_MASK             UINT8_C(0x20)

/*! @name Bit positions for OIS interface enable */
#define BMI2_OIS_IF_EN_POS              UINT8_C(0x04)
#define BMI2_AUX_IF_EN_POS              UINT8_C(0x05)

/******************************************************************************/
/*! @name		Macro Definitions for Axes re-mapping		      */
/******************************************************************************/
/*! @name Macros for the user-defined values of axes and their polarities */
#define BMI2_X                          UINT8_C(0x01)
#define BMI2_NEG_X                      UINT8_C(0x09)
#define BMI2_Y                          UINT8_C(0x02)
#define BMI2_NEG_Y                      UINT8_C(0x0A)
#define BMI2_Z                          UINT8_C(0x04)
#define BMI2_NEG_Z                      UINT8_C(0x0C)
#define BMI2_AXIS_MASK                  UINT8_C(0x07)
#define BMI2_AXIS_SIGN                  UINT8_C(0x08)

/******************************************************************************/
/*! @name         Macro Definitions for offset and gain compensation          */
/******************************************************************************/
/*! @name Mask definitions of gyroscope offset compensation registers */
#define BMI2_GYR_GAIN_EN_MASK           UINT8_C(0x80)
#define BMI2_GYR_OFF_COMP_EN_MASK       UINT8_C(0x40)

/*! @name Bit positions of gyroscope offset compensation registers */
#define BMI2_GYR_OFF_COMP_EN_POS       UINT8_C(0x06)

/*! @name Mask definitions of gyroscope user-gain registers */
#define BMI2_GYR_USR_GAIN_X_MASK        UINT8_C(0x7F)
#define BMI2_GYR_USR_GAIN_Y_MASK        UINT8_C(0x7F)
#define BMI2_GYR_USR_GAIN_Z_MASK        UINT8_C(0x7F)

/*! @name Bit positions of gyroscope offset compensation registers */
#define BMI2_GYR_GAIN_EN_POS            UINT8_C(0x07)

/******************************************************************************/
/*! @name		Macro Definitions for internal status	              */
/******************************************************************************/
#define BMI2_NOT_INIT                   UINT8_C(0x00)
#define BMI2_INIT_OK                    UINT8_C(0x01)
#define BMI2_INIT_ERR                   UINT8_C(0x02)
#define BMI2_DRV_ERR                    UINT8_C(0x03)
#define BMI2_SNS_STOP                   UINT8_C(0x04)
#define BMI2_NVM_ERROR                  UINT8_C(0x05)
#define BMI2_START_UP_ERROR             UINT8_C(0x06)
#define BMI2_COMPAT_ERROR               UINT8_C(0x07)
#define BMI2_VFM_SKIPPED                UINT8_C(0x10)
#define BMI2_AXES_MAP_ERROR             UINT8_C(0x20)
#define BMI2_ODR_50_HZ_ERROR            UINT8_C(0x40)
#define BMI2_ODR_HIGH_ERROR             UINT8_C(0x80)

/******************************************************************************/
/*! @name			Function Pointers                             */
/******************************************************************************/
/*! For interfacing to the I2C or SPI read functions */
typedef int8_t (*bmi2_read_fptr_t)(uint8_t dev_addr, uint8_t reg_addr, uint8_t *data, uint16_t len);
/*! For interfacing to the I2C or SPI write functions */
typedef int8_t (*bmi2_write_fptr_t)(uint8_t dev_addr, uint8_t reg_addr, const uint8_t *data, uint16_t len);
/*! For interfacing to the delay function */
typedef void (*bmi2_delay_fptr_t)(uint32_t period);

/******************************************************************************/
/*!  @name		   Enum Declarations                                  */
/******************************************************************************/
/*!  @name Enum to define BMI2 sensor interfaces */
enum  bmi2_intf_type {
	BMI2_SPI_INTERFACE = 1,
	BMI2_I2C_INTERFACE
};

/*!  @name Enum to define BMI2 sensor configuration errors for accelerometer
 *   and gyroscope
 */
enum bmi2_sensor_config_error {
	BMI2_NO_ERROR,
	BMI2_ACC_ERROR,
	BMI2_GYR_ERROR,
	BMI2_ACC_GYR_ERROR
};

/*!  @name Enum to define interrupt lines */
enum bmi2_hw_int_pin {
	BMI2_INT_NONE,
	BMI2_INT1,
	BMI2_INT2,
	BMI2_INT_BOTH,
	BMI2_INT_PIN_MAX
};

/*!  @name Enum for the position of the wearable device */
enum bmi2_wear_arm_pos {
	BMI2_ARM_LEFT,
	BMI2_ARM_RIGHT
};

/*!  @name Enum to display type of activity recognition */
enum bmi2_act_recog_type {
	BMI2_ACT_UNKNOWN,
	BMI2_ACT_STILL,
	BMI2_ACT_WALK,
	BMI2_ACT_RUN,
	BMI2_ACT_BIKE,
	BMI2_ACT_VEHICLE,
	BMI2_ACT_TILTED
};

/*!  @name Enum to display activity recognition status */
enum bmi2_act_recog_stat {
	BMI2_ACT_START = 1,
	BMI2_ACT_END
};

/******************************************************************************/
/*!  @name		   Structure Declarations                             */
/******************************************************************************/
/*! @name Structure to store the compensated user-gain data of gyroscope */
struct bmi2_gyro_user_gain_data {
	/*! x-axis */
	int8_t x;
	/*! y-axis */
	int8_t y;
	/*! z-axis */
	int8_t z;
};

/*! @name Structure to store the re-mapped axis */
struct bmi2_remap {
	/*! Re-mapped x-axis */
	uint8_t x;
	/*! Re-mapped y-axis */
	uint8_t y;
	/*! Re-mapped z-axis */
	uint8_t z;
};

/*! @name Structure to store the value of re-mapped axis and its sign */
struct bmi2_axes_remap {
	/*! Re-mapped x-axis */
	uint8_t x_axis;
	/*! Re-mapped y-axis */
	uint8_t y_axis;
	/*! Re-mapped z-axis */
	uint8_t z_axis;
	/*! Re-mapped x-axis sign */
	int16_t x_axis_sign;
	/*! Re-mapped y-axis sign */
	int16_t y_axis_sign;
	/*! Re-mapped z-axis sign */
	int16_t z_axis_sign;
};

/*! @name Structure to define the type of sensor and its interrupt pin */
struct bmi2_sens_int_config {
	/*! Defines the type of sensor */
	uint8_t type;
	/*! Type of interrupt pin */
	enum bmi2_hw_int_pin hw_int_pin;
};

/*! @name Structure to define the output configuration value of features */
struct bmi2_int_map {
	/*! Output configuration value of sig-motion */
	uint8_t sig_mot_out_conf;
	/*! Output configuration value of any-motion */
	uint8_t any_mot_out_conf;
	/*! Output configuration value of no-motion */
	uint8_t no_mot_out_conf;
	/*! Output configuration value of step-detector */
	uint8_t step_det_out_conf;
	/*! Output configuration value of step-activity */
	uint8_t step_act_out_conf;
	/*! Output configuration value of tilt */
	uint8_t tilt_out_conf;
	/*! Output configuration value of pick-up */
	uint8_t pick_up_out_conf;
	/*! Output configuration value of glance */
	uint8_t glance_out_conf;
	/*! Output configuration value of wake-up */
	uint8_t wake_up_out_conf;
	/*! Output configuration value of orientation */
	uint8_t orient_out_conf;
	/*! Output configuration value of high-g */
	uint8_t high_g_out_conf;
	/*! Output configuration value of low-g */
	uint8_t low_g_out_conf;
	/*! Output configuration value of flat */
	uint8_t flat_out_conf;
	/*! Output configuration value of S4S */
	uint8_t ext_sync_out_conf;
	/*! Output configuration value of wrist gesture */
	uint8_t wrist_gest_out_conf;
	/*! Output configuration value of wrist wear wake-up */
	uint8_t wrist_wear_wake_up_out_conf;
};

/*! @name Structure to define output for activity recognition */
struct bmi2_act_recog_output {
	/*! Time stamp */
	uint32_t time_stamp;
	/*! Type of activity */
	uint8_t type;
	/*! Status of the activity */
	uint8_t stat;
};

/*! @name Structure to define FIFO frame configuration */
struct bmi2_fifo_frame {
	/*! Pointer to FIFO data */
	uint8_t *data;
	/*! Number of user defined bytes of FIFO to be read */
	uint16_t length;
	/*! Defines header/header-less mode */
	uint8_t header_enable;
	/*! Enables type of data to be streamed - accelerometer, auxiliary or
	gyroscope */
	uint16_t data_enable;
	/*! To index accelerometer bytes */
	uint16_t acc_byte_start_idx;
	/*! To index activity output bytes */
	uint16_t act_recog_byte_start_idx;
	/*! To index auxiliary bytes */
	uint16_t aux_byte_start_idx;
	/*! To index gyroscope bytes */
	uint16_t gyr_byte_start_idx;
	/*! FIFO sensor time */
	uint32_t sensor_time;
	/*! Skipped frame count */
	uint8_t skipped_frame_count;
	/*! Type of data interrupt to be mapped */
	uint8_t data_int_map;
	/*! Water-mark level for water-mark interrupt */
	uint16_t wm_lvl;
	/*! Accelerometer frame length */
	uint8_t acc_frm_len;
	/*! Gyroscope frame length */
	uint8_t gyr_frm_len;
	/*! Auxiliary frame length */
	uint8_t aux_frm_len;
	/*! Accelerometer and gyroscope frame length */
	uint8_t acc_gyr_frm_len;
	/*! Accelerometer and auxiliary frame length */
	uint8_t acc_aux_frm_len;
	/*! Gyroscope and auxiliary frame length */
	uint8_t aux_gyr_frm_len;
	/*! Accelerometer, Gyroscope and auxiliary frame length */
	uint8_t all_frm_len;
};

/*! @name Structure to define Interrupt pin configuration */
struct	bmi2_int_pin_cfg {
	/*! Configure level of interrupt pin */
	uint8_t lvl;
	/*! Configure behavior of interrupt pin */
	uint8_t od;
	/*! Output enable for interrupt pin */
	uint8_t output_en;
	/*! Input enable for interrupt pin */
	uint8_t input_en;
};

/*! @name Structure to define interrupt pin type, mode and configurations */
struct bmi2_int_pin_config {
	/*! Interrupt pin type: INT1 or INT2 or BOTH */
	uint8_t pin_type;
	/*! Latched or non-latched mode*/
	uint8_t int_latch;
	/*! Structure to define Interrupt pin configuration */
	struct bmi2_int_pin_cfg pin_cfg[BMI2_INT_PIN_MAX_NUM];
};

/*! @name Structure to define an array of 8 auxiliary data bytes */
struct bmi2_aux_fifo_data {
	/*! Auxiliary data */
	uint8_t data[8];
	/*! Sensor time for virtual frames */
	uint32_t virt_sens_time;
};

/*! @name Structure to define accelerometer and gyroscope sensor axes and
	sensor time for virtual frames */
struct bmi2_sens_axes_data {
	/*! Data in x-axis */
	int16_t x;
	/*! Data in y-axis */
	int16_t y;
	/*! Data in z-axis */
	int16_t z;
	/*! Sensor time for virtual frames */
	uint32_t virt_sens_time;
};

/*! @name Structure to define gyroscope saturation status of user gain */
struct bmi2_gyr_user_gain_status {
	/*! Status in x-axis */
	uint8_t sat_x;
	/*! Status in y-axis */
	uint8_t sat_y;
	/*! Status in z-axis */
	uint8_t sat_z;
};

/*! @name Structure to define NVM error status */
struct bmi2_nvm_err_status {
	/*! NVM load action error */
	uint8_t load_error;
	/*! NVM program action error */
	uint8_t prog_error;
	/*! NVM erase action error */
	uint8_t erase_error;
	/*! NVM program limit exceeded */
	uint8_t exceed_error;
	/*! NVM privilege error */
	uint8_t privil_error;
};

/*! @name Structure to define VFRM error status */
struct bmi2_vfrm_err_status {
	/*! VFRM lock acquire error */
	uint8_t lock_error;
	/*! VFRM write error */
	uint8_t write_error;
	/*! VFRM fatal err */
	uint8_t fatal_error;
};

/*! @name Structure to define orientation output */
struct bmi2_orientation_output {
	/*! Orientation portrait landscape */
	uint8_t portrait_landscape;
	/*! Orientation face-up down  */
	uint8_t faceup_down;
};

/*! @name Union to define BMI2 sensor data */
union bmi2_sens_data {
	/*! Accelerometer axes data */
	struct bmi2_sens_axes_data acc;
	/*! Gyroscope axes data */
	struct bmi2_sens_axes_data gyr;
	/*! Auxiliary sensor data */
	uint8_t aux_data[BMI2_AUX_NUM_BYTES];
	/*! Step counter output */
	uint32_t step_counter_output;
	/*! Step activity output */
	uint8_t activity_output;
	/*! Orientation output */
	struct bmi2_orientation_output orient_output;
	/*! High-g output */
	uint8_t high_g_output;
	/*! Gyroscope user gain saturation status */
	struct bmi2_gyr_user_gain_status gyro_user_gain_status;
	/*! NVM error status */
	struct bmi2_nvm_err_status nvm_status;
	/*! Virtual frame error status */
	struct bmi2_vfrm_err_status vfrm_status;
	/*! Wrist gesture output */
	uint8_t wrist_gest;
	/*! Gyroscope cross sense value of z axis */
	int16_t correction_factor_zx;
};

/*! @name Structure to define type of sensor and their respective data */
struct bmi2_sensor_data {
	/*! Defines the type of sensor */
	uint8_t type;
	/*! Defines various sensor data */
	union bmi2_sens_data sens_data;
};

/*! @name Structure to define accelerometer configuration */
struct bmi2_accel_config {
	/*! Output data rate in Hz */
	uint8_t odr;
	/*! Bandwidth parameter */
	uint8_t bwp;
	/*! Filter performance mode */
	uint8_t filter_perf;
	/*! g-range */
	uint8_t range;
};

/*! @name Structure to define gyroscope configuration */
struct bmi2_gyro_config {
	/*! Output data rate in Hz */
	uint8_t odr;
	/*! Bandwidth parameter */
	uint8_t bwp;
	/*! Filter performance mode */
	uint8_t filter_perf;
	/*! OIS Range */
	uint8_t ois_range;
	/*! Gyroscope Range */
	uint8_t range;
	/*! Selects noise performance */
	uint8_t noise_perf;
};

/*! @name Structure to define auxiliary sensor configuration */
struct bmi2_aux_config {
	/*! Enable/Disable auxiliary interface */
	uint8_t aux_en;
	/*! Manual or Auto mode*/
	uint8_t manual_en;
	/*! Enables FCU write command on auxiliary interface */
	uint8_t fcu_write_en;
	/*! Read burst length for manual mode */
	uint8_t man_rd_burst;
	/*! Read burst length for data mode */
	uint8_t aux_rd_burst;
	/*! Output data rate */
	uint8_t odr;
	/*! Read-out offset */
	uint8_t offset;
	/*! I2c address of auxiliary sensor */
	uint8_t i2c_device_addr;
	/*! Read address of auxiliary sensor */
	uint8_t read_addr;
};

/*! @name Structure to define any-motion configuration */
struct bmi2_any_motion_config {
	/*! Duration in 50Hz samples(20msec) */
	uint16_t duration;
	/*! Acceleration slope threshold */
	uint16_t threshold;
	/*! To select per x-axis */
	uint16_t select_x;
	/*! To select per y-axis */
	uint16_t select_y;
	/*! To select per z-axis */
	uint16_t select_z;
	/*! Enable bits for enabling output into the register status bits */
	uint16_t out_conf;
};

/*! @name Structure to define no-motion configuration */
struct bmi2_no_motion_config {
	/*! Duration in 50Hz samples(20msec) */
	uint16_t duration;
	/*! Acceleration slope threshold */
	uint16_t threshold;
	/*! To select per x-axis */
	uint16_t select_x;
	/*! To select per y-axis */
	uint16_t select_y;
	/*! To select per z-axis */
	uint16_t select_z;
	/*! Enable bits for enabling output into the register status bits */
	uint16_t out_conf;
};

/*! @name Structure to define sig-motion configuration */
struct bmi2_sig_motion_config {
	/*! Block size */
	uint16_t block_size;
	/*! Parameter 2 */
	uint16_t param_2;
	/*! Parameter 3 */
	uint16_t param_3;
	/*! Parameter 4 */
	uint16_t param_4;
	/*! Parameter 5 */
	uint16_t param_5;
	/*! Enable bits for enabling output into the register status bits */
	uint16_t out_conf;
};

/*! @name Structure to define step counter/detector/activity configuration */
struct bmi2_step_config {
	/*! Water-mark level */
	uint16_t watermark_level;
	/*! Reset counter */
	uint16_t reset_counter;
	/*! Enable bits for enabling output into the register status bits
	for step-detector */
	uint16_t out_conf_step_detector;
	/*! Enable bits for enabling output into the register status bits
	for step-activity */
	uint16_t out_conf_activity;
};

/*! @name Structure to define gyroscope user gain configuration */
struct bmi2_gyro_user_gain_config {
	/*! Gain update value for x-axis */
	uint16_t ratio_x;
	/*! Gain update value for y-axis */
	uint16_t ratio_y;
	/*! Gain update value for z-axis */
	uint16_t ratio_z;
};

/*! @name Structure to define tilt configuration */
struct bmi2_tilt_config {
	/*! Enable bits for enabling output into the register status bits */
	uint16_t out_conf;
};

/*! @name Structure to define pick-up configuration */
struct bmi2_pick_up_config {
	/*! Enable bits for enabling output into the register status bits */
	uint16_t out_conf;
};

/*! @name Structure to define glance detector configuration */
struct bmi2_glance_det_config {
	/*! Enable bits for enabling output into the register status bits */
	uint16_t out_conf;
};

/*! @name Structure to define wake-up configuration */
struct bmi2_wake_up_config {
	/*!  Wake-up sensitivity */
	uint16_t sensitivity;
	/*!  Enable -> Single Tap; Disable -> Double Tap */
	uint16_t single_tap_en;
	/*! Enable bits for enabling output into the register status bits */
	uint16_t out_conf;
};

/*! @name Structure to define orientation configuration */
struct bmi2_orient_config {
	/*!  Upside/down detection */
	uint16_t ud_en;
	/*!  Symmetrical, high or low Symmetrical */
	uint16_t mode;
	/*!  Blocking mode */
	uint16_t blocking;
	/*!  Threshold angle */
	uint16_t theta;
	/*!  Acceleration hysteresis for orientation detection */
	uint16_t hysteresis;
	/*! Enable bits for enabling output into the register status bits */
	uint16_t out_conf;
};

/*! @name Structure to define high-g configuration */
struct bmi2_high_g_config {
	/*!  Acceleration threshold */
	uint16_t threshold;
	/*!  Hysteresis */
	uint16_t hysteresis;
	/*! To select per x-axis */
	uint16_t select_x;
	/*! To select per y-axis */
	uint16_t select_y;
	/*! To select per z-axis */
	uint16_t select_z;
	/*!  Duration interval */
	uint16_t duration;
	/*! Enable bits for enabling output into the register status bits */
	uint16_t out_conf;
};

/*! @name Structure to define low-g configuration */
struct bmi2_low_g_config {
	/*!  Acceleration threshold */
	uint16_t threshold;
	/*!  Hysteresis */
	uint16_t hysteresis;
	/*!  Duration interval */
	uint16_t duration;
	/*! Enable bits for enabling output into the register status bits */
	uint16_t out_conf;
};

/*! @name Structure to define flat configuration */
struct bmi2_flat_config {
	/*!  Theta angle for flat detection */
	uint16_t theta;
	/*!  Blocking mode */
	uint16_t blocking;
	/*!  Hysteresis for theta flat detection */
	uint16_t hysteresis;
	/*! Holds the duration in 50Hz samples(20msec) */
	uint16_t hold_time;
	/*! Enable bits for enabling output into the register status bits */
	uint16_t out_conf;
};

/*! @name Structure to define external sensor sync configuration */
struct bmi2_ext_sens_sync_config {
	/*! Enable bits for enabling output into the register status bits */
	uint16_t out_conf;
};

/*! @name Structure to define wrist gesture configuration */
struct bmi2_wrist_gest_config {
	/*!  Wearable arm (left or right) */
	uint16_t wear_arm;
	/*! Enable bits for enabling output into the register status bits */
	uint16_t out_conf;
};

/*! @name Structure to define wrist wear wake-up configuration */
struct bmi2_wrist_wear_wake_up_config {
	/*! Enable bits for enabling output into the register status bits */
	uint16_t out_conf;
};

/*!  @name Union to define the sensor configurations */
union bmi2_sens_config_types {
	/*! Accelerometer configuration */
	struct bmi2_accel_config acc;
	/*! Gyroscope configuration */
	struct bmi2_gyro_config gyr;
	/*! Auxiliary configuration */
	struct bmi2_aux_config aux;
	/*! Any-motion configuration */
	struct bmi2_any_motion_config any_motion;
	/*! No-motion configuration */
	struct bmi2_no_motion_config no_motion;
	/*! Sig_motion configuration */
	struct bmi2_sig_motion_config sig_motion;
	/*! Step counter/detector/activity configuration */
	struct bmi2_step_config step_counter;
	/*! Gyroscope user gain configuration */
	struct bmi2_gyro_user_gain_config gyro_gain_update;
	/*! Tilt configuration */
	struct bmi2_tilt_config tilt;
	/*! Pick-up configuration */
	struct bmi2_pick_up_config pick_up;
	/*! Glance detector configuration */
	struct bmi2_glance_det_config glance_det;
	/*! Wake-up configuration */
	struct bmi2_wake_up_config tap;
	/*! Orientation configuration */
	struct bmi2_orient_config orientation;
	/*! High-g configuration */
	struct bmi2_high_g_config high_g;
	/*! Low-g configuration */
	struct bmi2_low_g_config  low_g;
	/*! Flat configuration */
	struct bmi2_flat_config flat;
	/*! External sensor sync configuration */
	struct bmi2_ext_sens_sync_config ext_sens_sync;
	/*! Wrist gesture configuration */
	struct bmi2_wrist_gest_config wrist_gest;
	/*! Wrist wear wake-up configuration */
	struct bmi2_wrist_wear_wake_up_config wrist_wear_wake_up;
};

/*!  @name Structure to define the type of the sensor and its configurations */
struct bmi2_sens_config {
	/*! Defines the type of sensor */
	uint8_t type;
	/*! Defines various sensor configurations */
	union bmi2_sens_config_types cfg;
};

/*!  @name Structure to define the feature configuration */
struct bmi2_feature_config {
	/*! Defines the type of sensor */
	uint8_t type;
	/*! Page to where the feature is mapped */
	uint8_t page;
	/*! Address of the feature */
	uint8_t start_addr;
};

/*!  @name Structure to define BMI2 sensor configurations */
struct bmi2_dev {
	/*! Chip id of BMI2 */
	uint8_t chip_id;
	/*! Device id of BMI2 */
	uint8_t dev_id;
	/*! To store warnings */
	uint8_t info;
	/*! Type of Interface  */
	enum bmi2_intf_type intf;
	/*! For switching from I2C to SPI */
	uint8_t dummy_byte;
	/*! Resolution for FOC */
	uint8_t resolution;
	/*! User set read/write length */
	uint16_t read_write_len;
	/*! Pointer to the configuration data buffer address */
	const uint8_t *config_file_ptr;
	/*! To define maximum page number */
	uint8_t page_max;
	/*! To define maximum number of input sensors/features */
	uint8_t input_sens;
	/*! To define maximum number of output sensors/features */
	uint8_t out_sens;
	/*! Indicate manual enable for auxiliary communication */
	uint8_t aux_man_en;
	/*! Defines manual read burst length for auxiliary communication */
	uint8_t aux_man_rd_burst_len;
	/*! Array of feature input configuration structure */
	const struct bmi2_feature_config *feat_config;
	/*! Array of feature output configuration structure */
	const struct bmi2_feature_config *feat_output;
	/*! Structure to maintain a copy of feature out_conf values */
	struct bmi2_int_map int_map;
	/*! Structure to maintain a copy of the re-mapped axis */
	struct bmi2_axes_remap remap;
	/*! Flag to hold enable status of sensors */
	uint32_t sens_en_stat;
	/*! Read function pointer */
	bmi2_read_fptr_t read;
	/*! Write function pointer */
	bmi2_write_fptr_t write;
	/*!  Delay function pointer */
	bmi2_delay_fptr_t delay_ms;
	/*! To store the gyroscope cross sensitivity value */
	int16_t gyr_cross_sens_zx;
};

#endif /* BMI2_DEFS_H_ */
