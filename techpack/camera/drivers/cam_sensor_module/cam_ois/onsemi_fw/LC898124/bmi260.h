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
 * @file	bmi260.h
 * @date    1 September, 2018
 * @version 0.5.0
 * @brief	Sensor driver for BMI260 sensor
 *
 */

#ifndef BMI260_H_
#define BMI260_H_

/*! CPP guard */
#ifdef __cplusplus
extern "C"
{
#endif

/***************************************************************************/
/*!				Header files
****************************************************************************/
#include "bmi2.h"

/***************************************************************************/
/*!			      Macro definitions
****************************************************************************/
/*! @name BMI260 chip identifier */
#define BMI260_CHIP_ID                      UINT8_C(0x27)

/*! @name BMI260 feature input start addresses */
#define BMI260_AXIS_MAP_STRT_ADDR           UINT8_C(0x04)
#define BMI260_GYRO_SELF_OFF_STRT_ADDR      UINT8_C(0x05)
#define BMI260_ANY_MOT_STRT_ADDR            UINT8_C(0x06)
#define BMI260_NO_MOT_STRT_ADDR             UINT8_C(0x0A)
#define BMI260_WAKE_UP_STRT_ADDR            UINT8_C(0x0E)
#define BMI260_ORIENT_STRT_ADDR             UINT8_C(0x00)
#define BMI260_HIGH_G_STRT_ADDR             UINT8_C(0x04)
#define BMI260_LOW_G_STRT_ADDR              UINT8_C(0x0A)
#define BMI260_FLAT_STRT_ADDR               UINT8_C(0x00)
#define BMI260_SIG_MOT_STRT_ADDR            UINT8_C(0x04)
#define BMI260_STEP_COUNT_STRT_ADDR         UINT8_C(0x00)
#define BMI260_GYRO_GAIN_UPDATE_STRT_ADDR   UINT8_C(0x04)

/*! @name BMI260 feature output start addresses */
#define BMI260_STEP_CNT_OUT_STRT_ADDR       UINT8_C(0x00)
#define BMI260_STEP_ACT_OUT_STRT_ADDR       UINT8_C(0x04)
#define BMI260_ORIENT_OUT_STRT_ADDR         UINT8_C(0x06)
#define BMI260_HIGH_G_OUT_STRT_ADDR         UINT8_C(0x08)
#define BMI260_GYR_USER_GAIN_OUT_STRT_ADDR  UINT8_C(0x0A)
#define BMI260_GYRO_CROSS_SENSE_STRT_ADDR   UINT8_C(0x0C)
#define BMI260_NVM_VFRM_OUT_STRT_ADDR       UINT8_C(0x0E)

/*! @name Defines maximum number of pages */
#define BMI260_MAX_PAGE_NUM                 UINT8_C(8)

/*! @name Defines maximum number of feature input configurations */
#define BMI260_MAX_FEAT_IN                  UINT8_C(14)

/*! @name Defines maximum number of feature outputs */
#define BMI260_MAX_FEAT_OUT                 UINT8_C(8)

/*! @name Mask definitions for feature interrupt status bits */
#define	BMI260_SIG_MOT_STATUS_MASK          UINT8_C(0x01)
#define	BMI260_STEP_CNT_STATUS_MASK         UINT8_C(0x02)
#define	BMI260_HIGH_G_STATUS_MASK           UINT8_C(0x04)
#define	BMI260_LOW_G_STATUS_MASK            UINT8_C(0x04)
#define BMI260_WAKE_UP_STATUS_MASK          UINT8_C(0x08)
#define	BMI260_FLAT_STATUS_MASK             UINT8_C(0x10)
#define BMI260_NO_MOT_STATUS_MASK           UINT8_C(0x20)
#define BMI260_ANY_MOT_STATUS_MASK          UINT8_C(0x40)
#define	BMI260_ORIENT_STATUS_MASK           UINT8_C(0x80)

/***************************************************************************/
/*!		BMI260 User Interface function prototypes
****************************************************************************/
/*!
 *  @brief This API:
 *  1) updates the device structure with address of the configuration file.
 *  2) Initializes BMI260 sensor.
 *  3) Writes the configuration file.
 *  4) Updates the feature offset parameters in the device structure.
 *  5) Updates the maximum number of pages, in the device structure.
 *
 * @param[in, out] dev      : Structure instance of bmi2_dev.
 *
 * @return Result of API execution status
 *
 * @retval BMI2_OK - Success
 * @retval BMI2_E_NULL_PTR - Error: Null pointer error
 * @retval BMI2_E_COM_FAIL - Error: Communication fail
 * @retval BMI2_E_DEV_NOT_FOUND - Invalid device
 */
extern int8_t bmi2_init(struct bmi2_dev *dev);

/******************************************************************************/
/*! @name		C++ Guard Macros                                      */
/******************************************************************************/
#ifdef __cplusplus
}
#endif /* End of CPP guard */

#endif /* BMI260_H_ */
