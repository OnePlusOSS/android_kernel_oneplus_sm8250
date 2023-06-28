/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2018-2020 Oplus. All rights reserved.
 */

#ifndef _CS_PRESS_H
#define _CS_PRESS_H


/*IIC REG*/
#define IIC_EEPROM       0x00
#define IIC_DEV_ID       0x02
#define IIC_MANU_ID      0x03
#define IIC_MODULE_ID    0x04
#define IIC_FW_VER       0x05
#define IIC_WAKE_UP      0x06
#define IIC_SLEEP        0x07
#define IIC_GREEN        0x08
#define IIC_GREEN2NORMAL 0x10
#define IIC_HOSTSTATUS   0x50
#define IIC_DEBUG_MODE   0x60
#define IIC_DATA_READY   0x61
#define IIC_DEBUG_DATA1  0x62
#define IIC_DEBUG_DATA2  0x63
#define IIC_DEBUG_DATA3  0x64
#define IIC_DEBUG_DATA4  0x65
#define IIC_KEY_SEN      0xD0
#define IIC_KEY_STATUS   0xD3
#define IIC_DOWN_THD     0x90
#define IIC_UP_THD       0x91
#define IIC_WORK_MODE    0x99

#define IIC_RESETCMD    0xf17c

#define CS_DEVID_LEN    0xA         /* device id length */
#define CS_MANUID_LEN   0x2         /* manufacture id length */
#define CS_MODULEID_LEN 0x2         /* module id length */
#define CS_FWID_LEN     0x2         /* firmware image length */
#define CS_FW_LENPOS    0x0c
#define CS_FW_VERPOS    0x08
#define CS_FW_STARTPOS  0x100

#define IIC_MAX_TRSANFER    5

enum mode {
    MODE_REPORT_KEY,
    MODE_REPORT_TOUCH,
    MODE_SLEEP_IN     = 10,
    MODE_REPORT_POWER = 100,
    MODE_REPORT_HOME  = 101,
    MODE_REPORT_BACK  = 102,
};

struct point {
    uint16_t x;
    uint16_t y;
};

struct cs_device {
    int                 irq;                //press irq number
    uint32_t            irq_flag;           //irq trigger flasg
    int                 irq_gpio;           //irq gpio num
    int                 reset_gpio;         //Reset gpio
    bool                force_update;       //set to 1 when fw check failed
    struct regulator    *vdd_2v8;           //press power
    struct i2c_client   *client;            //i2c client
    struct device       *dev;               //the device structure
    struct mutex        mutex;              //mutex for control operate flow
    struct mutex        i2c_mutex;          //mutex for control i2c opearation
    struct input_dev    *input_dev;         //input device for report event
    struct miscdevice   cs_misc;            //misc device
    int                 report_mode;        //shows which event should report
    struct point        current_point;      //point this key want to report
};

#endif/*_CS_PRESS_H*/
