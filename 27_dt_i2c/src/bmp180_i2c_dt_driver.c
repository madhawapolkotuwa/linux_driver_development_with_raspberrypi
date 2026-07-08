/*
 * bmp180_i2c_driver.c
 *
 * BMP180 sensor example inside an I2C driver.
 * Exposes sysfs attributes:
 *   /sys/bus/i2c/devices/<bus>-<addr>/temp_input      (signed, tenths of °C)
 *   /sys/bus/i2c/devices/<bus>-<addr>/pressure_input  (signed, Pa)
 *
 * Based on user's userspace code; adapted to kernel APIs and i2c_smbus functions.
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/i2c.h>
#include <linux/delay.h>      /* msleep / udelay */
#include <linux/slab.h>
#include <linux/sysfs.h>
#include <linux/device.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("MP Coding Linux");
MODULE_DESCRIPTION("I2C BMP180 example: sysfs temp and pressure");

static uint8_t bmp_calib_reg  =    0xAA;
static uint8_t bmp_meas_reg   =    0xF4;
static uint8_t bmp_result_msb =    0xF6;
static uint8_t bmp_ctrl_temp  =    0x2E;
static uint8_t bmp_ctrl_pres  =    0x34;

/* 11 calibration coefficients */
struct bmp_calib {
    int16_t  AC1;
    int16_t  AC2;
    int16_t  AC3;
    uint16_t AC4;
    uint16_t AC5;
    uint16_t AC6;
    int16_t  B1;
    int16_t  B2;
    int16_t  MB;
    int16_t  MC;
    int16_t  MD;
};

/* Per-device data */
struct bmp_client {
    struct i2c_client *client;
    struct bmp_calib calib;
    uint8_t oversample; /* 0..3 */
};

/* Helper to read a block of bytes from device */
static int bmp_read_block(struct i2c_client *client, uint8_t start_reg, uint8_t *buf, int len)
{
    int ret;

    /* smbus read_i2c_block_data returns number of bytes read or negative errno */
    ret = i2c_smbus_read_i2c_block_data(client, start_reg, len, buf);
    if (ret < 0)
        return ret;
    if (ret != len)
        return -EIO;
    return 0;
}

/* Read and parse calibration data */
static int bmp_read_calib(struct i2c_client *client, struct bmp_calib *c)
{
    uint8_t raw[22];
    int ret;

    ret = bmp_read_block(client, bmp_calib_reg, raw, sizeof(raw));
    if (ret)
        return ret;

    /* big-endian pairs */
    c->AC1 = (int16_t)((raw[0] << 8) | raw[1]);
    c->AC2 = (int16_t)((raw[2] << 8) | raw[3]);
    c->AC3 = (int16_t)((raw[4] << 8) | raw[5]);
    c->AC4 = (uint16_t)((raw[6] << 8) | raw[7]);
    c->AC5 = (uint16_t)((raw[8] << 8) | raw[9]);
    c->AC6 = (uint16_t)((raw[10] << 8) | raw[11]);
    c->B1  = (int16_t)((raw[12] << 8) | raw[13]);
    c->B2  = (int16_t)((raw[14] << 8) | raw[15]);
    c->MB  = (int16_t)((raw[16] << 8) | raw[17]);
    c->MC  = (int16_t)((raw[18] << 8) | raw[19]);
    c->MD  = (int16_t)((raw[20] << 8) | raw[21]);

    dev_dbg(&client->dev, "AC1=%d AC2=%d AC3=%d AC4=%u AC5=%u AC6=%u\n",
            c->AC1, c->AC2, c->AC3, c->AC4, c->AC5, c->AC6);
    dev_dbg(&client->dev, "B1=%d B2=%d MB=%d MC=%d MD=%d\n",
            c->B1, c->B2, c->MB, c->MC, c->MD);

    return 0;
}

/* Trigger temperature conversion and read uncompensated temp UT */
static int bmp_read_raw_temp(struct i2c_client *client, int32_t *UT)
{
    int ret;
    uint8_t tmp[2];

    ret = i2c_smbus_write_byte_data(client, bmp_meas_reg, bmp_ctrl_temp);
    if (ret < 0)
        return ret;

    /* Wait 4.5 ms minimum; use 5ms */
    msleep(5);

    ret = bmp_read_block(client, bmp_result_msb, tmp, 2);
    if (ret < 0)
        return ret;

    *UT = (int32_t)((tmp[0] << 8) | tmp[1]);
    return 0;
}

/* Trigger pressure conversion and read uncompensated pressure UP */
static int bmp_read_raw_pres(struct i2c_client *client, uint8_t oss, int32_t *UP)
{
    int ret;
    uint8_t cmd = bmp_ctrl_pres + (oss << 6);
    uint8_t buf[3];

    ret = i2c_smbus_write_byte_data(client, bmp_meas_reg, cmd);
    if (ret < 0)
        return ret;

    /* Conversion time depends on oversampling:
     * oss=0 -> 4.5 ms (we'll use 5)
     * oss=1 -> 7.5 ms
     * oss=2 -> 13.5 ms
     * oss=3 -> 25.5 ms
     */
    switch (oss) {
        case 0: msleep(5); break;
        case 1: msleep(8); break;
        case 2: msleep(14); break;
        default: msleep(26); break;
    }

    ret = bmp_read_block(client, bmp_result_msb, buf, 3);
    if (ret < 0)
        return ret;

    /* UP is 19..16 bits left aligned depending on OSS */
    *UP = ((buf[0] << 16) | (buf[1] << 8) | buf[2]) >> (8 - oss);
    return 0;
}

/* Apply BMP180 compensation algorithm.
 * temp_out is returned as tenths of degree Celsius (i.e. T * 10)
 * pres_out is returned in Pa.
 *
 * Algorithm follows data sheet and your userspace code logic.
 */
static int bmp_compensate(struct bmp_calib *c, uint8_t oss,
                          int32_t UT, int32_t UP,
                          int32_t *temp_out, int32_t *pres_out)
{
    int32_t X1, X2, B3, B5, B6, X3, p;
    uint32_t B4, B7;

    /* Temperature calculations */
    X1 = ((UT - (int32_t)c->AC6) * (int32_t)c->AC5) >> 15;
    X2 = ((int32_t)c->MC << 11) / (X1 + (int32_t)c->MD);
    B5 = X1 + X2;
    /* Temperature in 0.1°C: (B5 + 8) >> 4 gives degrees C * 1 (not 0.1) in your userspace code,
     * but they printed temperature / 10.0, so they used B5 / 10 ? To match user's output:
     * We'll return tenths of degrees: temp_out = ((B5 + 8) >> 4) * 10 ??? Wait:
     *
     * In the userspace test they did:
     *   printf("Temperature: %.1f C\n", temperature / 10.0);
     * Where Get_TempAndPreasure set *temp = (B5+8)>>4;
     * So their *temp was in 0.1°C units (e.g. 215 => 21.5°C).
     *
     * So simply set temp_out = (B5 + 8) >> 4;
     * That value is in 0.1°C units already.
     */
    *temp_out = (B5 + 8) >> 4;

    /* Pressure calculations */
    B6 = B5 - 4000;
    X1 = (c->B2 * ( (B6 * B6) >> 12 )) >> 11;
    X2 = (c->AC2 * B6) >> 11;
    X3 = X1 + X2;
    B3 = ((((int32_t)c->AC1 * 4 + X3) << oss) + 2) >> 2;
    X1 = (c->AC3 * B6) >> 13;
    X2 = (c->B1 * ((B6 * B6) >> 12)) >> 16;
    X3 = ((X1 + X2) + 2) >> 2;
    B4 = (c->AC4 * (uint32_t)(X3 + 32768)) >> 15;
    B7 = ((uint32_t)UP - B3) * (50000 >> oss);
    if (B7 < 0x80000000U)
        p = (B7 * 2) / B4;
    else
        p = (B7 / B4) * 2;

    X1 = (p >> 8) * (p >> 8);
    X1 = (X1 * 3038) >> 16;
    X2 = (-7357 * p) >> 16;
    p = p + ((X1 + X2 + 3791) >> 4);

    *pres_out = p; /* Pressure in Pa */
    return 0;
}

/* sysfs show functions */
static ssize_t temp_input_show(struct device *dev, struct device_attribute *attr, char *buf)
{
    struct i2c_client *client = to_i2c_client(dev);
    struct bmp_client *bc = i2c_get_clientdata(client);
    int32_t UT, UP, temp10, pres;
    int ret;

    if (!bc)
        return -ENODEV;

    ret = bmp_read_raw_temp(client, &UT);
    if (ret)
        return ret;

    ret = bmp_read_raw_pres(client, bc->oversample, &UP);
    if (ret)
        return ret;

    bmp_compensate(&bc->calib, bc->oversample, UT, UP, &temp10, &pres);

    /* temp10 is in tenths of degrees Celsius */
    return scnprintf(buf, PAGE_SIZE, "%d\n", temp10);
}

static ssize_t pressure_input_show(struct device *dev, struct device_attribute *attr, char *buf)
{
    struct i2c_client *client = to_i2c_client(dev);
    struct bmp_client *bc = i2c_get_clientdata(client);
    int32_t UT, UP, temp10, pres;
    int ret;

    if (!bc)
        return -ENODEV;

    ret = bmp_read_raw_temp(client, &UT);
    if (ret)
        return ret;

    ret = bmp_read_raw_pres(client, bc->oversample, &UP);
    if (ret)
        return ret;

    bmp_compensate(&bc->calib, bc->oversample, UT, UP, &temp10, &pres);

    /* pres is in Pa */
    return scnprintf(buf, PAGE_SIZE, "%d\n", pres);
}

static DEVICE_ATTR_RO(temp_input);
static DEVICE_ATTR_RO(pressure_input);

static struct attribute *bmp_attrs[] = {
    &dev_attr_temp_input.attr,
    &dev_attr_pressure_input.attr,
    NULL,
};

static const struct attribute_group bmp_group = {
    .attrs = bmp_attrs,
};

/* Probe: read calib data and create sysfs group */
static int bmp180_probe(struct i2c_client *client)
{
    struct bmp_client *bc;
    int ret;

    dev_info(&client->dev, "Probe function called!\n");

	if(!((client->addr > 0x00) && (client->addr <= 0x7F))) { // Standard I2C address Range(7-bit address space) (0x00 - 0x7F)
		dev_err(&client->dev, "I2C address is wrong %d!\n", client->addr);
		return -1;
	}

    bc = devm_kzalloc(&client->dev, sizeof(*bc), GFP_KERNEL);
    if (!bc)
        return -ENOMEM;

    bc->client = client;
    /* default oversample: use 3 (max resolution) */
    bc->oversample = 3;

    i2c_set_clientdata(client, bc);

    /* --- u8 calib_reg --- */
    ret = device_property_read_u8(&client->dev, "calib_reg", &bmp_calib_reg);
    if (ret) { 
        dev_err(&client->dev, "Could not read 'calib_reg'\n"); 
        return ret; 
    }
    dev_info(&client->dev, "calib_reg: 0x%02X\n", bmp_calib_reg);

    /* --- u8 meas_reg --- */
    ret = device_property_read_u8(&client->dev, "meas_reg", &bmp_meas_reg);
    if (ret) { 
        dev_err(&client->dev, "Could not read 'meas_reg'\n"); 
        return ret; 
    }
    dev_info(&client->dev, "meas_reg: 0x%02X\n", bmp_meas_reg);

    /* --- u8 result_msb --- */
    ret = device_property_read_u8(&client->dev, "result_msb", &bmp_result_msb);
    if (ret) { 
        dev_err(&client->dev, "Could not read 'result_msb'\n"); 
        return ret; 
    }
    dev_info(&client->dev, "result_msb: 0x%02X\n", bmp_result_msb);

    /* --- u8 ctrl_temp --- */
    ret = device_property_read_u8(&client->dev, "ctrl_temp", &bmp_ctrl_temp);
    if (ret) { 
        dev_err(&client->dev, "Could not read 'ctrl_temp'\n"); 
        return ret; 
    }
    dev_info(&client->dev, "ctrl_temp: 0x%02X\n", bmp_ctrl_temp);

    /* --- u8 ctrl_pres --- */
    ret = device_property_read_u8(&client->dev, "ctrl_pres", &bmp_ctrl_pres);
    if (ret) { 
        dev_err(&client->dev, "Could not read 'ctrl_pres'\n"); 
        return ret; 
    }
    dev_info(&client->dev, "ctrl_pres: 0x%02X\n", bmp_ctrl_pres);

    
    /* Read calibration */
    ret = bmp_read_calib(client, &bc->calib);
    if (ret) {
        dev_err(&client->dev, "failed to read calibration: %d\n", ret);
        return ret;
    }

    /* Create sysfs attributes on device */
    ret = sysfs_create_group(&client->dev.kobj, &bmp_group);
    if (ret) {
        dev_err(&client->dev, "failed to create sysfs group: %d\n", ret);
        return ret;
    }

    dev_info(&client->dev, "BMP180 probed and sysfs created\n");
    return 0;
}

static void bmp180_remove(struct i2c_client *client)
{
    sysfs_remove_group(&client->dev.kobj, &bmp_group);
    dev_info(&client->dev, "removed device\n");
}

/* private data */
struct meta_data 
{ 
	char name[32];
	uint8_t majorVersion; 
	uint8_t minorVersion; 
}; 

/* Two sample device profiles that the driver can attach to */
static struct meta_data a = {
	.name = "BMP180-tmp-pre",
	.majorVersion = 1,
	.minorVersion = 0
};

static struct i2c_device_id bmp180_ids[] = {
    { "bmp180", (long unsigned int) &a },
    { /* essential */ }
};

MODULE_DEVICE_TABLE(bmp_i2c, bmp180_ids);

static struct of_device_id my_driver_ids[] = {
    { .compatible = "bmp180-sensor,myi2c" },
    { },
};
MODULE_DEVICE_TABLE(of, my_driver_ids);

/* I2C driver struct */
static struct i2c_driver bmp180_driver = {
    .driver = {
        .name = "bmp180-i2c-driver",
    },
    .probe = bmp180_probe,
    .remove = bmp180_remove,
    .id_table = bmp180_ids,
    .driver = {
        .name = "my_bmp180",
        .of_match_table = my_driver_ids,
    }
};

/* This will create the init and exit function automatically */
module_i2c_driver(bmp180_driver);

