/* drivers/misc/cs_press/cs_press_f61.c*/
/************************************************************
 * Copyright 2018 OPLUS Mobile Comm Corp., Ltd.
 * All rights reserved.
 *
 * Description  : driver for chip sea IC
 * History      : ( ID, Date, Author, Description)
 * Data         : 2018/11/03
 ************************************************************/

#include <linux/device.h>
#include <linux/init.h>
#include <linux/string.h>
#include <linux/platform_device.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/input.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/timer.h>
#include <linux/delay.h>
#include <linux/hrtimer.h>
#include <linux/i2c.h>
#include <linux/gpio.h>
#include <linux/io.h>
#include <linux/slab.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/miscdevice.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/of_gpio.h>
#include <linux/pm.h>
#include <linux/netdevice.h>
#include <linux/mount.h>
#include <linux/proc_fs.h>
#include <linux/gpio.h>
#include <linux/io.h>
#include <linux/of_gpio.h>
#include <linux/of_device.h>
#include <linux/of_address.h>
#include <linux/interrupt.h>
#include <linux/of_irq.h>
#include <linux/completion.h>
#include <linux/kthread.h>
#include <linux/firmware.h>
#include <linux/regulator/of_regulator.h>
#include <linux/regulator/consumer.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/machine.h>
#include <linux/pinctrl/consumer.h>
#include "cs_press_f61.h"

#define CS_LOG(a, arg...)  pr_err("[cs_press]: " a, ##arg)
#define CS_CHRDEV_NAME "cs_press"
#define FW_PATH "press/19065/FW_F61_NDT.nfw"

__attribute__((weak)) void external_report_touch(int id, bool down_status, int x, int y) {return;}

/**
 * cs_i2c_read - Using for read data through i2c
 * @cd: cd press device handler
 * @reg: register for start reading
 * @datbuf: buffer for stroring data read from IC
 * @byte_len: data length we want to read
 *
 * Actully, This function call i2c_transfer for IIC transfer,
 * Returning transfer length(transfer success) or most likely negative errno(transfer error)
 */
static int cs_i2c_read(struct cs_device* cd, unsigned char reg, unsigned char *datbuf, int byte_len)
{
    struct i2c_msg msg[2];
    int ret = 0;
    int i = IIC_MAX_TRSANFER;

    msg[0].addr  = cd->client->addr;
    msg[0].flags = 0;
    msg[0].len   = 1;
    msg[0].buf   = &reg;

    msg[1].addr  = cd->client->addr;
    msg[1].flags = I2C_M_RD;
    msg[1].len   = byte_len;
    msg[1].buf   = datbuf;

    mutex_lock(&cd->i2c_mutex);
    do {
        ret = i2c_transfer(cd->client->adapter, msg, 2);
        if ((ret <= 0) && (i < 4)) {
            CS_LOG("i2c_transfer Error, err_code:%d,i=%d\n", ret, i);
        } else {
            break;
        }

        i--;
    } while (i > 0);
    mutex_unlock(&cd->i2c_mutex);

    return ret;
}

/**
 * cs_i2c_write - Using for write data through i2c
 * @cd: cd press device handler
 * @reg: register for start writing
 * @datbuf: buffer for stroring writing data
 * @byte_len: data length we want to write
 *
 * Actully, This function call i2c_transfer for IIC transfer,
 * Returning transfer length(transfer success) or most likely negative errno(transfer error)
 */
static int cs_i2c_write(struct cs_device* cd, unsigned char reg, unsigned char *datbuf, int byte_len)
{
    unsigned char *buf = NULL;
    struct i2c_msg msg;
    int ret = 0;
    int i = IIC_MAX_TRSANFER;

    if (!datbuf || byte_len <= 0)
        return -1;

    buf = (unsigned char *)kmalloc(byte_len + 1, GFP_KERNEL);
    if (!buf)
        return -1;

    memset(buf, 0, byte_len + 1);
    buf[0] = reg;
    memcpy(buf + 1, datbuf, byte_len);

    msg.addr  = cd->client->addr;
    msg.flags = 0;
    msg.len   = byte_len + 1;
    msg.buf   = buf;

    mutex_lock(&cd->i2c_mutex);
    do {
        ret = i2c_transfer(cd->client->adapter, &msg, 1);
        if ((ret <= 0) && (i < 4)) {
            CS_LOG("i2c_transfer Error! err_code:%d,i=%d\n", ret, i);
        } else {
            break;
        }

        i--;
    } while (i > 0);
    mutex_unlock(&cd->i2c_mutex);

    kfree(buf);

    return ret;
}

static int cs_read_eeprom(struct cs_device *cd, unsigned short reg, unsigned char *datbuf, int byte_len)
{
    struct i2c_msg msg[2];
    int ret = 0;
    unsigned char reg16[2];

    if (!datbuf)
        return -1;

    reg16[0] = (reg >> 8) & 0xff;
    reg16[1] = reg & 0xff;

    msg[0].addr  = cd->client->addr;
    msg[0].flags = 0;
    msg[0].len   = sizeof(reg16);
    msg[0].buf   = reg16;

    msg[1].addr  = cd->client->addr;
    msg[1].flags = I2C_M_RD;
    msg[1].len   = byte_len;
    msg[1].buf   = datbuf;

    mutex_lock(&cd->i2c_mutex);
    ret = i2c_transfer(cd->client->adapter, msg, 2);
    if (ret < 0) {
        CS_LOG("i2c_transfer Error, err_code:%d\n", ret);
    }
    mutex_unlock(&cd->i2c_mutex);

    return ret;
}

static int cs_write_eeprom(struct cs_device *cd, unsigned short reg, unsigned char *datbuf, int byte_len)
{
    unsigned char *buf = NULL;
    struct i2c_msg msg;
    int ret = 0;

    if (!datbuf || byte_len <= 0)
        return -1;
    buf = (unsigned char *)kmalloc(byte_len + sizeof(reg), GFP_KERNEL);
    if (!buf)
        return -1;

    memset(buf, 0, byte_len + sizeof(reg));
    buf[0] = (reg >> 8) & 0xff;
    buf[1] = reg & 0xff;
    memcpy(buf + sizeof(reg), datbuf, byte_len);

    msg.addr  = cd->client->addr;
    msg.flags = 0;
    msg.len   = byte_len + sizeof(reg);
    msg.buf   = buf;

    mutex_lock(&cd->i2c_mutex);
    ret = i2c_transfer(cd->client->adapter, &msg, 1);
    if (ret < 0) {
        CS_LOG("i2c_master_send Error, err_code:%d\n", ret);
    }
    mutex_unlock(&cd->i2c_mutex);

    kfree(buf);
    return ret;
}

static int cs_eeprom_erase(struct cs_device *cd)
{
    unsigned char erase_cmd[] = {0xAA, 0x55, 0xA5, 0x5A};

    /*erase flash*/
    if (cs_write_eeprom(cd, IIC_EEPROM, erase_cmd, sizeof(erase_cmd)) <= 0) {
        CS_LOG("cs_write_eeprom fails in cs_eeprom_erase\n");
        return -1;
    }

    msleep(2000);
    return 0;
}

int cs_eeprom_skip(struct cs_device *cd)
{
    unsigned char skip_cmd[] = {0x7E, 0xE7, 0xEE, 0x77};

    if (cs_write_eeprom(cd, IIC_EEPROM, skip_cmd, sizeof(skip_cmd)) <= 0) {
        CS_LOG("cs_write_eeprom fails in cs_eeprom_skip\n");
        return -1;
    }

    return 0;
}

int cs_eeprom_read(struct cs_device *cd)
{
    unsigned char cmd[] = {0xA7, 0x00, 0x00, 0x59};

    if (cs_write_eeprom(cd, IIC_RESETCMD, cmd, sizeof(cmd)) <= 0) {
        CS_LOG("cs_write_eeprom fail !\n");
        return -1;
    }

    return 0;
}

int wake_up_fw(struct cs_device *cd)
{
    int i = 5;
    int ret = 0;
    char reg_data[2] = {0};

    do {
        ret = cs_i2c_read(cd, IIC_MANU_ID, reg_data, 1);
        i--;
    } while (ret <= 0 && i > 0);

    return 0;
}

/**
 * cs_mode_switch - for switch work mode
 * cd : cs press handler
 * mode : 0 for normal mode, 1 for standby mode.
 */
static int cs_mode_switch(struct cs_device *cd, int mode)
{
    int ret = 0;
    int retry = 5;
    unsigned char data[1] = {0};

    wake_up_fw(cd);     //wakeup i2c

    if (0 == mode) {
        data[0] = 0x00;     //scan freq auto
    } else if (1 == mode) {
        data[0] = 0x01;     //max scan freq:100hz
    } else if (2 == mode) {
        data[0] = 0x02;     //min scan freq:10hz
    } else if (3 == mode) {
        data[0] = 0x03;     //sleep mode
    } else {
        CS_LOG("not support mode(%d).\n", mode);
    }
    ret = cs_i2c_write(cd, IIC_WORK_MODE, data, sizeof(data));
    data[0] = 0x02;
    ret |= cs_i2c_write(cd, 0x0F, data, sizeof(data));
    if (ret <= 0) {
        CS_LOG("send mode switch cmd:%d failed.\n", data[0]);
        return ret;
    }

    while (retry) {
        msleep(10);
        ret = cs_i2c_read(cd, 0x0F, data, sizeof(data));
        if ((ret > 0) && (0x00 == data[0])) {
            break;
        }
        retry--;
    }
    CS_LOG("wait mode(%d) switch left %d ms.\n", mode, retry*10);

    return ret;
}

/**
* cs_parse_dt - parse dts of cs press, acquire harware resource
* @cd: cs press handler, init it's harfware member
*/
static int cs_parse_dt(struct cs_device *cd)
{
    int ret = 0;

    cd->vdd_2v8 = regulator_get(cd->dev, "vdd_2v8");
    if(IS_ERR_OR_NULL(cd->vdd_2v8)) {
       CS_LOG("cs press regulator no defined.\n"); 
    } else {
        if (regulator_count_voltages(cd->vdd_2v8) > 0) {
            ret = regulator_set_voltage(cd->vdd_2v8, 2800000, 3300000);
            if (ret) {
                CS_LOG("Regulator set_vtg failed vdd ret=%d\n", ret);
            }
        }
    }

    cd->irq_gpio = of_get_named_gpio_flags(cd->dev->of_node, "press,irq-gpio", 0, &cd->irq_flag);
    if(!gpio_is_valid(cd->irq_gpio)) {
        CS_LOG("cspress request_irq IRQ fail.\n");
    } else {
        ret = gpio_request(cd->irq_gpio, "press,irq-gpio");
        if(ret) {
            CS_LOG("cs press request_irq IRQ fail, ret=%d.\n", ret);
        }
    }

    cd->reset_gpio = of_get_named_gpio(cd->dev->of_node, "press,rst-gpio", 0);
    if(!gpio_is_valid(cd->reset_gpio)) {
        CS_LOG("cs_press request_rst fail.\n");
    } else {
        ret = gpio_request(cd->reset_gpio, "press,rst-gpio");
        if(ret) {
            CS_LOG("cs_press request rst fail, ret=%d.\n", ret);
        }
    }

    return 0;
}

/**
* cs_power_control - interface for power on and power off
* @cd: cs press handler
* @on: true for power on, false for power off
*/
static int cs_power_control(struct cs_device *cd, bool on)
{
    int ret = 0;

    if (on) {
        if (!IS_ERR_OR_NULL(cd->vdd_2v8)) {
            ret = regulator_enable(cd->vdd_2v8);        //power on
            if (ret) {
                CS_LOG("cs press power on failed(%d).\n", ret);
            }
        }

        if (gpio_is_valid(cd->reset_gpio)) {
            gpio_direction_output(cd->reset_gpio, 0);     //default to low state
        }
    } else {
        if (gpio_is_valid(cd->reset_gpio)) {
            gpio_direction_output(cd->reset_gpio, 1);     //pull to reset state
        }

        if (!IS_ERR_OR_NULL(cd->vdd_2v8)) {
            ret = regulator_disable(cd->vdd_2v8);       //power off
            if (ret) {
                CS_LOG("cs press power off failed(%d).\n", ret);
            }
        }
    }
    CS_LOG("%s state: %d.\n", __func__, on);

    return ret;
}

/**
* cs_reset - interface for reset
* @cd: cs press handler
* @shval: 0 for hardware reset, 1 for software reset, 2 for defalut reset
*/
static int cs_reset(struct cs_device *cd)
{
    unsigned char erase_cmd[] = {0xA0, 0x00, 0x00, 0x60};

    if(gpio_is_valid(cd->reset_gpio)) {
        gpio_direction_output(cd->reset_gpio, 1);
        msleep(10);
        gpio_direction_output(cd->reset_gpio, 0);
        CS_LOG("harware reset.\n");
    } else {
        cs_write_eeprom(cd, IIC_RESETCMD, erase_cmd, sizeof(erase_cmd));
        CS_LOG("software reset.\n");
    }

    return 0;
}

/**
* cs_i2c_check - interface for reset
* @cd: cs press handler
*/
int cs_i2c_check(struct cs_device *cd)
{
    int retry = 4;
    unsigned char rbuf[2] = {0};
    int ret = 0;

    do {
        wake_up_fw(cd);
        if (cs_i2c_read(cd, IIC_MANU_ID, rbuf, 1) > 0) {
            CS_LOG("i2c check success.\n");
            if (!rbuf[0]) {
                CS_LOG("read no fw id, need do force update.\n");
                cd->force_update = true;
            }
            return 0;
        } else {
            cs_reset(cd);
            msleep(300);
        }

        retry--;
    } while (retry > 0);

    if (retry <= 0) {
        ret = -1;
        CS_LOG("i2c check failed.\n");
    }

    return ret;
}

/**
* init_input_device - init input device for reporting
* @cd: cs press handler
*/
static int init_input_device(struct cs_device *cd)
{
    int ret = 0;

    cd->input_dev = input_allocate_device();
    if (cd->input_dev == NULL) {
        ret = -ENOMEM;
        CS_LOG("Failed to allocate input device\n");
        return ret;
    }

    cd->input_dev->name = "cs_press";
    set_bit(EV_SYN, cd->input_dev->evbit);
    set_bit(EV_KEY, cd->input_dev->evbit);
    set_bit(KEY_POWER, cd->input_dev->keybit);
    set_bit(KEY_HOMEPAGE, cd->input_dev->keybit);
    set_bit(KEY_BACK, cd->input_dev->keybit);
    set_bit(KEY_F24, cd->input_dev->keybit);

    ret = input_register_device(cd->input_dev);
    if (ret) {
        input_free_device(cd->input_dev);
        cd->input_dev = NULL;
        CS_LOG("Failed to register input device.\n");
    } else {
        CS_LOG("success to register input device.\n");
    }

    return ret;
}

static int fw_burn(struct cs_device *cd, unsigned char *buf, int len)
{
    unsigned short reg = 0;
    int byte_len = 0, pos = 0;
    unsigned char *read_buf;
    int ret = 0, i = 0, err_len = 0, number = 0;
    bool i2c_ok = true;
    int page_end = len%256;
    char err_buf[256*3+3] = {0};

    if (len % 128) {
        CS_LOG("burn len is not 128*\n");
        return -1;
    }
    CS_LOG("write len:%d, read page_end:%d\n", len, page_end);
    read_buf = (unsigned char *)kmalloc(len, GFP_KERNEL);
    if (!read_buf) {
        CS_LOG("kmalloc for read_buf fails\n");
        return -1;
    }

    do {
        cs_reset(cd);
        msleep(60);

        i2c_ok = true;
        ret = cs_eeprom_erase(cd);
        if (ret < 0) {
            i2c_ok = false;
            goto I2C_BAD_OUT;
        }

        /*write eeprom*/
        pos = 0;
        reg = 0x00;
        byte_len = 128;
        while (pos < len) {
            ret = cs_write_eeprom(cd, pos, buf + pos, byte_len);
            if (ret < 0) {
                CS_LOG("cs_write_eeprom fails, page:%d\n", reg);
                i2c_ok = false;
                goto I2C_BAD_OUT;
            }
            pos += byte_len;
            reg++;
            msleep(15);
        }

        /*read eeprom*/
        pos = 0;
        reg = 0x00;
        byte_len = 256;
        while (pos < len) {
            ret = cs_read_eeprom(cd, pos, read_buf + pos, byte_len);
            if (ret < 0) {
                CS_LOG("read page fail, page:%d\n", reg);
                i2c_ok = false;
                goto I2C_BAD_OUT;
            }
            /*check*/
            if ((page_end > 0) && (reg >= len/256))
                byte_len = page_end;
            if (memcmp(buf + pos, read_buf + pos, byte_len)) {
                CS_LOG("read page cmp fail, page:%d\n", reg);
                i2c_ok = false;

                err_len = 0;
                for (i = 0; i < byte_len; i++)
                    err_len += sprintf(err_buf + err_len, "%02x ", read_buf[pos + i]);
                err_len += sprintf(err_buf + err_len, "\n");
                CS_LOG("buf=%s\n", err_buf);

                goto I2C_BAD_OUT;
            }
            pos += byte_len;
            reg++;
            msleep(15);
        }
I2C_BAD_OUT:
        number++;
    } while ( number < 3 && !i2c_ok);


    if (!i2c_ok)
        CS_LOG("burn firmware err.\n");
    else
        CS_LOG("burn firmware success.\n");

    msleep(100);
    cs_reset(cd);
    msleep(100);
    cs_eeprom_skip(cd);
    if (read_buf)
        kfree(read_buf);

    return ret;
}

/**
* cs_set_press_threshold - set corresponding channel touchdown and touchup threshold
* @cd: cs press handler
* @touchTh: touchdown threshold
* @leaveTh: touchup threshold
* @channel: channel number we want to write
*/
static int set_press_threshold(struct cs_device *cd, int touchTh, int leaveTh, char channel)
{
    int ret = 0;
    unsigned char datbuf[8] = {0};

    CS_LOG("set TouchTh: %d, LeaveTh: %d, channel: %d\n", touchTh, leaveTh, channel);
    if (leaveTh > touchTh) {
        CS_LOG("incorrect TouchTh and LeaveTh, return.\n");
        return -1;
    }

    wake_up_fw(cd);

    datbuf[0] = touchTh & 0xff;
    datbuf[1] = (touchTh >> 8) & 0xff;
    ret = cs_i2c_write(cd, IIC_DOWN_THD, datbuf, 2);

    datbuf[0] = leaveTh & 0xff;
    datbuf[1] = (leaveTh >> 8) & 0xff;
    ret |= cs_i2c_write(cd, IIC_UP_THD, datbuf, 2);
    
    datbuf[0] = 0x01;
    ret |= cs_i2c_write(cd, 0x0f, datbuf, 1);

    return ret;
}

/**
* cs_get_press_threshold - acquire corresponding channel touchdown and touchup threshold
* @cd: cs press handler
* @*touchTh: pointer for store touchdown threshold
* @*leaveTh: pointer for store touchup threshold
* @channel: channel number we want to read
*/
static int get_press_threshold(struct cs_device *cd, unsigned int *touchTh, unsigned int *leaveTh, char channel)
{
    int ret = 0;
    unsigned char datbuf[8] = {0};

    wake_up_fw(cd);

    memset(datbuf, 0, 8*sizeof(unsigned char));
    ret = cs_i2c_read(cd, IIC_DOWN_THD, datbuf, 2);
    *touchTh = (unsigned int)(short)(datbuf[0] | ((datbuf[1]<<8)&0xff00));

    memset(datbuf, 0, 8*sizeof(unsigned char));
    ret |= cs_i2c_read(cd, IIC_UP_THD, datbuf, 2);
    *leaveTh = (unsigned int)(short)(datbuf[0] | ((datbuf[1]<<8)&0xff00));
    CS_LOG("get TouchTh: %d, LeaveTh: %d\n", *touchTh, *leaveTh);

    return ret;
}

/**
* cs_get_pressure - acquire current pressure
* @cd: cs press handler
*/
int cs_get_pressure(struct cs_device *cd)
{
    char reg_addr = 0x20;
    char reg_data[2] = {0};
    int pressdata = 0;

    if (cs_i2c_read(cd, reg_addr, reg_data, 1) < 0) {
        CS_LOG("err: reg=0x%02x, data=0x%02x\n", reg_addr, reg_data[0]);
    }
    if (reg_data[0] != 0) {
        reg_addr = 0x21;
        if (cs_i2c_read(cd, reg_addr, reg_data, 2) < 0) {
            CS_LOG("err: reg=0x%02x,data0=0x%02x,data1=0x%02x\n", reg_addr, reg_data[0], reg_data[1]);
        }
    } else {
        CS_LOG("err: regdata is 0\n");
        return 0;
    }

    pressdata = ((int)(reg_data[0] & 0xff) | ((int)(reg_data[1] & 0xff) << 8));
    return pressdata;
}

int read_eeprom_data(struct cs_device *cd, char *buf)
{
    unsigned char datbuf[33];
    int i;
    int len = 33;
    int ret = 1;

    datbuf[0] = 0x00;
    cs_i2c_write(cd, 0x80, datbuf, 1);

    datbuf[0] = 0x40;
    cs_i2c_write(cd, 0x80, datbuf, 1);
    msleep(30);

    memset(datbuf, 0, len * sizeof(unsigned char));
    cs_i2c_read(cd, 0x81, datbuf, len);
    CS_LOG("R 81:%02x,%02x \n", datbuf[0], datbuf[1]);
    if (datbuf[0] == len-1) {
        CS_LOG("Read eeprom 32 Data:\n");
        for (i = 0; i < datbuf[0]; i++)
            CS_LOG("%d %02x\n", i, datbuf[i]);
    }

    for (i = 0; i < len; i++)
        ret += sprintf(buf + 3 * i, "%02x ", datbuf[i]);
    ret += sprintf(buf + 3 * i, "\n");

    return ret;
}

static int cs_write_calibrate_param(struct cs_device *cd, unsigned int data1, char channel)
{
    unsigned char datbuf[8] = {0};

    CS_LOG("w calibrate_param:%d,channel:%d\n", data1, channel);
    datbuf[0] = 0x00;
    cs_i2c_write(cd, 0x80, datbuf, 1);

    datbuf[0] = 0x30;
    cs_i2c_write(cd, 0x80, datbuf, 1);

    datbuf[0] = channel;
    datbuf[1] = 0x00;
    datbuf[2] = data1 & 0xff;
    datbuf[3] = (data1 >> 8) & 0xff;
    datbuf[4] = (data1 >> 16) & 0xff;
    datbuf[5] = (data1 >> 24) & 0xff;
    datbuf[6] = datbuf[0] + datbuf[1] + datbuf[2] + datbuf[3] + datbuf[4] + datbuf[5]; /*checksum*/
    cs_i2c_write(cd, 0x82, datbuf, 7);

    datbuf[0] = 0x07;
    cs_i2c_write(cd, 0x81, datbuf, 1);

    return 0;

}

static int cs_read_calibrate_param(struct cs_device *cd, unsigned int *data, char channel)
{
    unsigned char datbuf[8] = {0};

    datbuf[0] = 0x00;
    cs_i2c_write(cd, 0x80, datbuf, 1);

    datbuf[0] = 0x31;
    cs_i2c_write(cd, 0x80, datbuf, 1);

    datbuf[0] = channel;
    datbuf[1] = 0x00;
    cs_i2c_write(cd, 0x82, datbuf, 2);

    datbuf[0] = 0x02;
    cs_i2c_write(cd, 0x81, datbuf, 1);
    msleep(30);

    memset(datbuf, 0, 8 * sizeof(unsigned char));
    cs_i2c_read(cd, 0x81, datbuf, 5);
    CS_LOG("R:%02x,%02x,%02x,%02x,%02x\n", datbuf[0], datbuf[1], datbuf[2], datbuf[3], datbuf[4]);
    if (datbuf[0] == 4) {
        *data = (unsigned int)(datbuf[1] + (datbuf[2] << 8) + (datbuf[3]<<16) + (datbuf[4] << 24));
        CS_LOG("R calibrate_param:%d\n", *data);
        return 0;
    }
    return -1;
}

int cs_compare_cali_param(struct cs_device *cd)
{
    char temp[256] = {0};
    int is_cal = 0;
    char count = 5;
    unsigned int cali_ic0[2];
    unsigned int cali_ic1[2];
    unsigned int cali_data1 = 0;
    int slp1 = 0, itr1 = 0, slp2 = 0, itr2 = 0, ret = 0;

    sprintf(temp, "1 4567 -100 -4668 -200");
    ret = sscanf(temp, "%d %d %d %d %d", &is_cal, &slp1, &itr1, &slp2, &itr2);
    CS_LOG("read ROM Cali:%d %d %d %d %d\n", is_cal, slp1, itr1, slp2, itr2);

    if (is_cal == 1) {
        /*ch0*/
        count = 5;
        cali_data1 = (unsigned int)(slp1 + (itr1 << 16));
        do {
            cs_read_calibrate_param(cd, cali_ic0, 0);
            CS_LOG("read Cali CH0 : 0x%08x\n", cali_ic0[0]);

            if (cali_data1 == cali_ic0[0]) {
                CS_LOG("CH0 is Same!\n");
                break;
            }

            cs_write_calibrate_param(cd, cali_data1, 0);
            CS_LOG("write Cali CH0 : 0x%08x\n", cali_data1);
            msleep(20);

            count--;
        } while (count);

        /*ch1*/
        count = 5;
        cali_data1 = (unsigned int)(slp2 + (itr2 << 16));
        do {
            cs_read_calibrate_param(cd, cali_ic1, 1);
            CS_LOG("read Cali CH1 : 0x%08x\n", cali_ic1[0]);

            if (cali_data1 == cali_ic1[0]) {
                CS_LOG("CH1 is Same!\n");
                break;
            }

            cs_write_calibrate_param(cd, cali_data1, 1);
            CS_LOG("write Cali CH1 : 0x%08x\n", cali_data1);
            msleep(20);

            count--;
        } while (count);
    }

    return 0;
}

static irqreturn_t cs_irq_thread_func(int irq, void *dev_id)
{
    uint8_t key_status = 0;
    struct cs_device *cd = (struct cs_device *)dev_id;

    mutex_lock(&cd->mutex);

    cs_i2c_read(cd, IIC_KEY_STATUS, &key_status, 1);
    CS_LOG("%s: keystatus:%d.\n", __func__, key_status);

    if (MODE_REPORT_KEY == cd->report_mode) {
        input_report_key(cd->input_dev, KEY_F24, key_status > 0);
        input_sync(cd->input_dev);
    } else if (MODE_REPORT_TOUCH == cd->report_mode) {
        external_report_touch(10, key_status > 0, cd->current_point.x, cd->current_point.y);
    } else if (MODE_REPORT_POWER == cd->report_mode) {
        input_report_key(cd->input_dev, KEY_POWER, key_status > 0);
        input_sync(cd->input_dev);
    } else if (MODE_REPORT_HOME == cd->report_mode) {
        input_report_key(cd->input_dev, KEY_HOMEPAGE, key_status > 0);
        input_sync(cd->input_dev);
    } else if (MODE_REPORT_BACK == cd->report_mode) {
        input_report_key(cd->input_dev, KEY_BACK, key_status > 0);
        input_sync(cd->input_dev);
    } else {
        CS_LOG("%s: no handle.\n", __func__);
    }

    mutex_unlock(&cd->mutex);

    return IRQ_HANDLED;
}

static ssize_t cs_read(struct file *file, char __user *buf, size_t count, loff_t *offset)
{
    int err = 0;
    char *kbuf = NULL;
    char reg = 0;
    struct miscdevice *mc = file->private_data;
    struct cs_device *cd = container_of(mc, struct cs_device, cs_misc);

    kbuf = kzalloc(count, GFP_KERNEL);
    if (!kbuf) {
        err = -ENOMEM;
        goto exit;
    }

    /*get reg addr buf[0]*/
    if (copy_from_user(&reg, buf, 1)) {
        err = -EFAULT;
        CS_LOG("%s, copy reg:0x%02x from user failed.\n", __func__, reg);
        goto exit_kfree;
    }

    mutex_lock(&cd->mutex);
    err = cs_i2c_read(cd, reg, kbuf, count);
    mutex_unlock(&cd->mutex);
    if (err < 0) {
        CS_LOG("%s, read reg:0x%02x failed.\n", __func__, reg);
        goto exit_kfree;
    }

    if (copy_to_user(buf+1, kbuf, count)) {
        CS_LOG("%s, copy to user failed.\n", __func__);
        err = -EFAULT;
    }

exit_kfree:
    kfree(kbuf);

exit:
    return err < 0 ? err : count;
}

static ssize_t cs_write(struct file *file, const char __user *buf, size_t count, loff_t *offset)
{
    int err = 0;
    char *kbuf = NULL;
    char reg = 0;
    struct miscdevice *mc = file->private_data;
    struct cs_device *cd = container_of(mc, struct cs_device, cs_misc);

    kbuf = kzalloc(count, GFP_KERNEL);
    if (!kbuf) {
        err = -ENOMEM;
        goto exit;
    }

    if (copy_from_user(&reg, buf, 1) || copy_from_user(kbuf, buf+1, count)) {
        err = -EFAULT;
        CS_LOG("%s, w reg:0x%02x failed.\n", __func__, reg);
        goto exit_kfree;
    }

    mutex_lock(&cd->mutex);
    err = cs_i2c_write(cd, reg, kbuf, count);
    mutex_unlock(&cd->mutex);

exit_kfree:
    kfree(kbuf);

exit:
    return err < 0 ? err : count;
}

static const struct file_operations cs_fops = {
    .owner  = THIS_MODULE,
    .open   = simple_open,
    .read   = cs_read,
    .write  = cs_write,
};

static ssize_t proc_fw_update_write(struct file *file, const char __user *page, size_t size, loff_t *lo)
{
    char buf[4] = {0};
    int val = 0, ret = 0;
    uint32_t fw_len = 0;
    const struct firmware *fw = NULL;
    uint16_t ic_version = 0, image_version = 0;
    struct cs_device *cd = PDE_DATA(file_inode(file));

    if (!cd)
        return size;
    if (size > 2)
        return size;

    if (copy_from_user(buf, page, size)) {
        CS_LOG("%s: read proc input error.\n", __func__);
        return size;
    }

    sscanf(buf, "%d", &val);

    disable_irq(cd->irq); /*close enit irq.*/
    mutex_lock(&cd->mutex);

    ret = request_firmware(&fw, FW_PATH, cd->dev);
    if (ret < 0) {
        CS_LOG("%s request fw failed,ret:%d\n", __func__, ret);
        goto out;
    }

    wake_up_fw(cd);
    memset(buf, 0, CS_FWID_LEN);
    ret = cs_i2c_read(cd, IIC_FW_VER, buf, CS_FWID_LEN);
    if (ret < 0) {
        CS_LOG("%s read fw version,ret:%d\n", __func__, ret);
        goto out;
    }
    ic_version = buf[0] | (buf[1] << 8);
    image_version = (fw->data[CS_FW_VERPOS] << 8) | fw->data[CS_FW_VERPOS+1];
    CS_LOG("fw ic version:0x%04x, fw image version:0x%04x\n", ic_version, image_version);

    if ((image_version == ic_version) && !cd->force_update && !val) {
        CS_LOG("no need do fw update.\n");
        goto out;
    }

    fw_len = fw->data[CS_FW_LENPOS+3] | (fw->data[CS_FW_LENPOS+2] << 8) | (fw->data[CS_FW_LENPOS+1] << 16) | (fw->data[CS_FW_LENPOS] << 24);
    ret = fw_burn(cd, (unsigned char *)&fw->data[CS_FW_STARTPOS], fw_len);
    if (ret < 0) {
        CS_LOG("%s: fw burn failed.\n", __func__);
        goto out;
    }

    CS_LOG("fw update finished\n");
out:
    if (fw) {
         release_firmware(fw);
         fw = NULL;
    }
    mutex_unlock(&cd->mutex);
    enable_irq(cd->irq);    /*open enit irq.*/

    return size;
}

static const struct file_operations proc_fw_update_ops =
{
    .write = proc_fw_update_write,
    .open = simple_open,
    .owner = THIS_MODULE,
};

static int fw_info_read_func(struct seq_file *s, void *v)
{
    int i = 0;
    unsigned char buf[32]  = {0};
    struct cs_device *cd = s->private;

    if (!cd)
        return 0;

    disable_irq(cd->irq);
    mutex_lock(&cd->mutex);

    cs_i2c_read(cd, IIC_DEV_ID, buf, CS_DEVID_LEN);
    seq_printf(s, "Device ID:");
    for (i = 0; i < CS_DEVID_LEN; i++)
        seq_printf(s, "%02x ", buf[i]);
    seq_printf(s, "\n");

    cs_i2c_read(cd, IIC_MANU_ID, buf, CS_MANUID_LEN);
    seq_printf(s, "Manu ID:");
    for (i = 0; i < CS_MANUID_LEN; i++)
        seq_printf(s, "%02x ", buf[i]);
    seq_printf(s, "\n");

    cs_i2c_read(cd, IIC_MODULE_ID, buf, CS_MODULEID_LEN);
    seq_printf(s, "Module ID:");
    for (i = 0; i < CS_MODULEID_LEN; i++)
        seq_printf(s, "%02x ", buf[i]);
    seq_printf(s, "\n");

    cs_i2c_read(cd, IIC_FW_VER, buf, CS_FWID_LEN);
    seq_printf(s, "Fw ID:");
    for (i = 0; i < CS_FWID_LEN; i++)
        seq_printf(s, "%02x ", buf[i]);
    seq_printf(s, "\n");

    cs_i2c_read(cd, IIC_WORK_MODE, buf, 1);
    seq_printf(s, "WORK MODE:");
    for (i = 0; i < 1; i++)
        seq_printf(s, "%02x ", buf[i]);
    seq_printf(s, "\n");

    mutex_unlock(&cd->mutex);
    enable_irq(cd->irq);

    return 0;
}

static int fw_info_open(struct inode *inode, struct file *file)
{
    return single_open(file, fw_info_read_func, PDE_DATA(inode));
}

static const struct file_operations proc_fw_info_ops =
{
    .owner = THIS_MODULE,
    .read  = seq_read,
    .open = fw_info_open,
    .release = single_release,
};

static int rawdata_read_func(struct seq_file *s, void *v)
{
    char reg_addr = 0;
    char reg_data[32] = {0};
    int i = 0,len = 0;
    int16_t raw_data = 0;
    struct cs_device *cd = s->private;

    if (!cd)
        return 0;

    disable_irq(cd->irq);
    mutex_lock(&cd->mutex);

    wake_up_fw(cd);

    reg_addr = 0x80;
    len = 1;
    reg_data[0] = 0x10;
    if (cs_i2c_write(cd, reg_addr, reg_data, len) <= 0) {
        CS_LOG("reg=0x%02x,data=0x%02x,count=%d,err\n",  reg_addr, reg_data[0], len);
    }

    reg_addr = 0x81;
    len = 1;
    reg_data[0] = 0x0;
    if (cs_i2c_write(cd, reg_addr, reg_data, len) <= 0) {
        CS_LOG("reg=0x%02x,data=0x%02x,count=%d,err\n", reg_addr, reg_data[0], len);
    }
    i=50;
    do {
        reg_addr = 0x81;
        len = 1;
        reg_data[0] = 0x0;
        if (cs_i2c_read(cd, reg_addr, reg_data, len) <= 0) {
            CS_LOG("reg=0x%02x,d0=0x%02x,count=%d,err\n", reg_addr, reg_data[0], len);
        }
        len = reg_data[0];
        if (len != 0) {
            reg_addr = 0x82;
            if (cs_i2c_read(cd, reg_addr, reg_data, len) <= 0) {
                CS_LOG("reg=0x%02x,d0=0x%02x,d1=0x%02x,count=%d\n", reg_addr, reg_data[0], reg_data[1], len);
            }
        } else {
            msleep(5);
        }
        i--;
    } while(len == 0 && i > 0);

    if (len == 0) {
        seq_printf(s, "read rawdata length is zero.\n");
    }

    for (i = 0; i < len/2; i++) {
        raw_data = ((reg_data[i*2] & 0xff) | ((reg_data[i*2 + 1] & 0xff) << 8));
        seq_printf(s, "rawdata[%d]: %d\n", i, raw_data);
    }

    reg_addr = 0x80;
    len = 1;
    reg_data[0] = 0x00;
    cs_i2c_write(cd, reg_addr, reg_data, len);

    mutex_unlock(&cd->mutex);
    enable_irq(cd->irq);

    return 0;
}

static int rawdata_open(struct inode *inode, struct file *file)
{
    return single_open(file, rawdata_read_func, PDE_DATA(inode));
}

static const struct file_operations proc_rawdata_ops =
{
    .owner = THIS_MODULE,
    .read  = seq_read,
    .open = rawdata_open,
    .release = single_release,
};

static int diffdata_read_func(struct seq_file *s, void *v)
{
    char reg_addr = 0;
    char reg_data[64] = {0};
    int i = 0,len = 0;
    int16_t diff_data = 0;
    struct cs_device *cd = s->private;

    if (!cd)
        return 0;

    disable_irq(cd->irq);
    mutex_lock(&cd->mutex);

    wake_up_fw(cd);

    reg_addr = 0x80;
    len = 1;
    reg_data[0] = 0x20;
    if (cs_i2c_write(cd, reg_addr, reg_data, len) <= 0) {
        CS_LOG("reg=0x%02x,data=0x%02x,count=%d,err\n",  reg_addr, reg_data[0], len);
    }

    reg_addr = 0x81;
    len = 1;
    reg_data[0] = 0x0;
    if (cs_i2c_write(cd, reg_addr, reg_data, len) <= 0) {
        CS_LOG("reg=0x%02x,data=0x%02x,count=%d,err\n", reg_addr, reg_data[0], len);
    }
    i=50;
    do {
        reg_addr = 0x81;
        len = 1;
        reg_data[0] = 0x0;
        if (cs_i2c_read(cd, reg_addr, reg_data, len) <= 0) {
            CS_LOG("reg=0x%02x,d0=0x%02x,count=%d,err\n", reg_addr, reg_data[0], len);
        }
        len = reg_data[0];
        if (len != 0) {
            reg_addr = 0x82;
            if (cs_i2c_read(cd, reg_addr, reg_data, len) <= 0) {
                CS_LOG("reg=0x%02x,d0=0x%02x,d1=0x%02x,count=%d\n", reg_addr, reg_data[0], reg_data[1], len);
            }
        } else {
            msleep(5);
        }
        i--;
    } while(len == 0 && i > 0);

    if (len == 0) {
        seq_printf(s, "read diffdata length is zero.\n");
    }

    for (i = 0; i < len/2; i++) {
        diff_data = ((reg_data[i*2] & 0xff) | ((reg_data[i*2 + 1] & 0xff) << 8));
        seq_printf(s, "diffdata[%d]: %d\n", i, diff_data);
    }

    reg_addr = 0x80;
    len = 1;
    reg_data[0] = 0x00;
    cs_i2c_write(cd, reg_addr, reg_data, len);

    mutex_unlock(&cd->mutex);
    enable_irq(cd->irq);

    return 0;
}

static int diffdata_open(struct inode *inode, struct file *file)
{
    return single_open(file, diffdata_read_func, PDE_DATA(inode));
}

static const struct file_operations proc_diffdata_ops =
{
    .owner = THIS_MODULE,
    .read  = seq_read,
    .open = diffdata_open,
    .release = single_release,
};

static ssize_t proc_debug_th_read(struct file *file, char __user *user_buf, size_t count, loff_t *ppos)
{
    char buf[128];
    int ret = 0, channel = 0, touch_th = 0, leave_th = 0;
    struct cs_device *cd = PDE_DATA(file_inode(file));

    if (!cd)
        return count;

    sscanf(buf, "%d,%d,%d", &channel, &touch_th, &leave_th);
    mutex_lock(&cd->mutex);
    ret = get_press_threshold(cd, &touch_th, &leave_th, channel);
    if (ret < 0) {
        CS_LOG("%s: get pressure failed.\n", __func__);
    }
    mutex_unlock(&cd->mutex);

    sprintf(buf, "touch_th: %d, leave_th: %d\n", touch_th, leave_th);
    ret = simple_read_from_buffer(user_buf, count, ppos, buf, strlen(buf));
    return ret;
}

static ssize_t proc_debug_th_write(struct file *file, const char __user *buffer, size_t count, loff_t *ppos)
{
    char buf[64];
    int ret = 0, channel = 0, touch_th = 0, leave_th = 0;
    struct cs_device *cd = PDE_DATA(file_inode(file));

    if (!cd)
        return count;

    if (!buffer || count <= 0) {
        CS_LOG("%s, argument err.\n", __func__);
        return count;
    }

    if (copy_from_user(buf, buffer, count)) {
        CS_LOG("%s: read proc input error.\n", __func__);
        return count;
    }

    sscanf(buf, "%d,%d,%d", &channel, &touch_th, &leave_th);
    mutex_lock(&cd->mutex);
    ret = set_press_threshold(cd, touch_th, leave_th, channel);
    mutex_unlock(&cd->mutex);

    return ret < 0 ? ret : count;
}

static const struct file_operations proc_debug_th_ops =
{
    .read  = proc_debug_th_read,
    .write = proc_debug_th_write,
    .open  = simple_open,
    .owner = THIS_MODULE,
};

static ssize_t proc_report_switch_write(struct file *file, const char __user *buffer, size_t count, loff_t *ppos)
{
    char buf[64];
    int val = 0;
    struct cs_device *cd = PDE_DATA(file_inode(file));

    if (!cd)
        return count;

    if (!buffer || count <= 0) {
        CS_LOG("%s, argument err.\n", __func__);
        return count;
    }

    if (copy_from_user(buf, buffer, count)) {
        CS_LOG("%s: read proc input error.\n", __func__);
        return count;
    }

    sscanf(buf, "%d", &val);
    CS_LOG("%s: set mode %d.\n", __func__, val);

    mutex_lock(&cd->mutex);
    if (val == cd->report_mode) {
        mutex_unlock(&cd->mutex);
        return count;
    }
    if (MODE_SLEEP_IN == val) {
        disable_irq(cd->irq);
        cs_mode_switch(cd, 3);
    } else if (MODE_SLEEP_IN == cd->report_mode) {
        cs_mode_switch(cd, 0);
        enable_irq(cd->irq);
    }
    cd->report_mode = val;      //set report mode
    mutex_unlock(&cd->mutex);

    return count;
}

static ssize_t proc_report_switch_read(struct file *file, char __user *user_buf, size_t count, loff_t *ppos)
{
    char buf[16];
    int ret = 0;
    struct cs_device *cd = PDE_DATA(file_inode(file));

    if (!cd)
        return count;

    sprintf(buf, "%d\n", cd->report_mode);
    ret = simple_read_from_buffer(user_buf, count, ppos, buf, strlen(buf));
    return ret;
}

static const struct file_operations proc_report_switch_ops =
{
    .read  = proc_report_switch_read,
    .write = proc_report_switch_write,
    .open  = simple_open,
    .owner = THIS_MODULE,
};

static ssize_t proc_coordinate_write(struct file *file, const char __user *buffer, size_t count, loff_t *ppos)
{
    char buf[64];
    int val_x = 0, val_y = 0;
    struct cs_device *cd = PDE_DATA(file_inode(file));

    if (!cd)
        return count;

    if (!buffer || count <= 0) {
        CS_LOG("%s, argument err.\n", __func__);
        return count;
    }

    if (copy_from_user(buf, buffer, count)) {
        CS_LOG("%s: read proc input error.\n", __func__);
        return count;
    }

    sscanf(buf, "%d,%d", &val_x, &val_y);
    CS_LOG("%s, set x:%d, y:%d.\n", __func__, val_x, val_y);

    mutex_lock(&cd->mutex);
    cd->current_point.x = val_x;
    cd->current_point.y = val_y;
    mutex_unlock(&cd->mutex);

    return count;
}

static ssize_t proc_coordinate_read(struct file *file, char __user *user_buf, size_t count, loff_t *ppos)
{
    char buf[16];
    int ret = 0;
    struct cs_device *cd = PDE_DATA(file_inode(file));

    if (!cd)
        return count;

    sprintf(buf, "x:%d, y:%d\n", cd->current_point.x, cd->current_point.y);
    ret = simple_read_from_buffer(user_buf, count, ppos, buf, strlen(buf));
    return ret;
}

static const struct file_operations proc_coordinate_ops =
{
    .read  = proc_coordinate_read,
    .write = proc_coordinate_write,
    .open  = simple_open,
    .owner = THIS_MODULE,
};

static int cs_init_proc(struct cs_device *cd)
{
    int ret = 0;
    struct proc_dir_entry *ptr_cs = NULL, *ptr_tmp = NULL;

    ptr_cs = proc_mkdir("press", NULL);
    if (ptr_cs == NULL) {
        CS_LOG("%s: Couldn't create cs proc entry\n", __func__);
        return -ENOMEM;
    }

    ptr_tmp = proc_create_data("fw_update", 0666, ptr_cs, &proc_fw_update_ops, cd);
    if (ptr_tmp == NULL) {
        ret = -ENOMEM;
        CS_LOG("%s: Couldn't create proc entry, %d\n", __func__, __LINE__);
    }

    ptr_tmp = proc_create_data("fw_info", 0444, ptr_cs, &proc_fw_info_ops, cd);
    if (ptr_tmp == NULL) {
        ret = -ENOMEM;
        CS_LOG("%s: Couldn't create proc entry, %d\n", __func__, __LINE__);
    }

    ptr_tmp = proc_create_data("rawdata", 0444, ptr_cs, &proc_rawdata_ops, cd);
    if (ptr_tmp == NULL) {
        ret = -ENOMEM;
        CS_LOG("%s: Couldn't create proc entry, %d\n", __func__, __LINE__);
    }

    ptr_tmp = proc_create_data("diffdata", 0444, ptr_cs, &proc_diffdata_ops, cd);
    if (ptr_tmp == NULL) {
        ret = -ENOMEM;
        CS_LOG("%s: Couldn't create proc entry, %d\n", __func__, __LINE__);
    }

    ptr_tmp = proc_create_data("press_thd", 0666, ptr_cs, &proc_debug_th_ops, cd);
    if (ptr_tmp == NULL) {
        ret = -ENOMEM;
        CS_LOG("%s: Couldn't create proc entry, %d\n", __func__, __LINE__);
    }

    ptr_tmp = proc_create_data("report_mode", 0666, ptr_cs, &proc_report_switch_ops, cd);
    if (ptr_tmp == NULL) {
        ret = -ENOMEM;
        CS_LOG("%s: Couldn't create proc entry, %d\n", __func__, __LINE__);
    }

    ptr_tmp = proc_create_data("touch_coordinate", 0666, ptr_cs, &proc_coordinate_ops, cd);
    if (ptr_tmp == NULL) {
        ret = -ENOMEM;
        CS_LOG("%s: Couldn't create proc entry, %d\n", __func__, __LINE__);
    }

    CS_LOG("%s: init proc file %s.\n", __func__, ret == 0 ? "susccess" : "failed");
    return ret;
}

static int cs_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
    int ret = 0;
    struct cs_device *cd = NULL;

    CS_LOG(" %s start.\n", __func__);

    //1. malloc mem for cs device
    cd = kzalloc(sizeof(struct cs_device), GFP_KERNEL);
    if (!cd) {
        CS_LOG("malloc space for cs device failed.\n");
        return -ENOMEM;
    }
    memset(cd, 0, sizeof(*cd));

    //2. init cs device member
    cd->client = client;
    cd->irq = client->irq;
    cd->dev = &client->dev;
    mutex_init(&cd->mutex);
    mutex_init(&cd->i2c_mutex);
    i2c_set_clientdata(client, cd);

    //3. parse dts
    cs_parse_dt(cd);

    //4. power on
    ret = cs_power_control(cd, true);
    if (ret) {
        ret= -1;
        goto power_on_failed;
    }

    //5. init input device
    ret = init_input_device(cd);
    if (ret) {
        goto input_register_failed;
    }

    //6. ic fw check
    ret = cs_i2c_check(cd);
    if (ret) {
        CS_LOG("fw check failed, need do force update\n");
        goto irq_register_failed;
    }

    //7. register irq handler
    ret = request_threaded_irq(cd->irq, NULL, cs_irq_thread_func,
                                        cd->irq_flag | IRQF_ONESHOT, CS_CHRDEV_NAME, cd);
    if (ret) {
        CS_LOG("requst irq failed:%d\n", ret);
        goto irq_register_failed;
    }

    //8. register misc device
    cd->cs_misc.minor = MISC_DYNAMIC_MINOR;
    cd->cs_misc.name  = "ndt";
    cd->cs_misc.fops  = &cs_fops;
    misc_register(&cd->cs_misc);

    //9. create proc file
    cs_init_proc(cd);

    CS_LOG(" %s normal end.\n", __func__);
    return 0;

irq_register_failed:
    input_unregister_device(cd->input_dev);
    cd->input_dev = NULL;
input_register_failed:
    cs_power_control(cd, false);
power_on_failed:
    if (!IS_ERR_OR_NULL(cd->vdd_2v8)) {
        regulator_put(cd->vdd_2v8);
        cd->vdd_2v8 = NULL;
    }

    if (gpio_is_valid(cd->irq_gpio)) {
        gpio_free(cd->irq_gpio);
    }

    if (gpio_is_valid(cd->reset_gpio)) {
        gpio_free(cd->reset_gpio);
    }

    if (cd) {
        kfree(cd);
        cd = NULL;
    }

    return ret;

}

static int cs_remove(struct i2c_client *client)
{
    struct cs_device *cd = i2c_get_clientdata(client);

    i2c_unregister_device(client);
    misc_deregister(&cd->cs_misc);
    kfree(cd);

    return 0;
}

static int cs_suspend(struct device *device)
{
    struct cs_device *cd = dev_get_drvdata(device);

    disable_irq(cd->irq);
    cs_mode_switch(cd, 3);
    return 0;
}

static int cs_resume(struct device *device)
{
    struct cs_device *cd = dev_get_drvdata(device);

    cs_mode_switch(cd, 0);
    enable_irq(cd->irq);
    return 0;
}

static const struct dev_pm_ops cs_pm_ops = {
    .suspend = cs_suspend,
    .resume  = cs_resume,
};

static const struct i2c_device_id cs_id_table[] = {
    {CS_CHRDEV_NAME, 0},
    {},
};

static struct of_device_id cs_match_table[] = {
    { .compatible = "qcom,cs_press", },
    {},
};
MODULE_DEVICE_TABLE(i2c, cs_id_table);

static struct i2c_driver cs_driver = {
    .id_table = cs_id_table,
    .probe = cs_probe,
    .remove = cs_remove,
    .driver = {
        .owner = THIS_MODULE,
        .name = CS_CHRDEV_NAME,
        .of_match_table = cs_match_table,
        .pm = &cs_pm_ops,
    },
};

static int __init cs_init(void)
{
    int ret = 0;

    CS_LOG(" %s start.\n", __func__);
    ret = i2c_add_driver(&cs_driver);
    if (ret < 0) {
        CS_LOG("i2c_add_driver fail,status=%d\n", ret);
    }

    return 0;
}

static void __exit cs_exit(void)
{
    i2c_del_driver(&cs_driver);
}

module_init(cs_init);
module_exit(cs_exit);

MODULE_AUTHOR("ChipSea, Inc.");
MODULE_DESCRIPTION("cs press Driver");
MODULE_LICENSE("GPL v2");
