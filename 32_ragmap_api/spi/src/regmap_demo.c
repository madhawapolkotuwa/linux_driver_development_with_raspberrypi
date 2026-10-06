#include <linux/module.h>
#include <linux/spi/spi.h>
#include <linux/regmap.h>
#include <linux/of.h>
#include <linux/bitops.h>
#include <linux/delay.h>

/* MPU6500 register map (subset used by this demo) */
#define MPU6500_SMPLRT_DIV       0x19
#define MPU6500_CONFIG           0x1A
#define MPU6500_GYRO_CONFIG      0x1B
#define MPU6500_ACCEL_CONFIG     0x1C
#define MPU6500_ACCEL_XOUT_H     0x3B  /* first of a 14-byte burst */
#define MPU6500_PWR_MGMT_1       0x6B
#define MPU6500_WHO_AM_I         0x75

#define MPU6500_WHO_AM_I_VAL     0x70
#define MPU6500_MAX_REGISTER     0x7E

#define PWR_MGMT_1_SLEEP_MASK    BIT(6)
#define PWR_MGMT_1_CLKSEL_MASK   GENMASK(2, 0)
#define PWR_MGMT_1_CLKSEL_PLL    0x01  /* auto-select best clock source */

#define MPU6500_SAMPLE_BURST_LEN 14    /* accel(6) + temp(2) + gyro(6) */

/* MPU6500 SPI register protocol: bit 7 of the address byte selects
 * read (1) vs write (0). regmap's read_flag_mask takes care of OR-ing
 * this in automatically for every regmap_read/regmap_bulk_read. */
#define MPU6500_SPI_READ_FLAG    0x80

struct MPU6500_sample {
    s16 accel_x, accel_y, accel_z;
    s16 temp;
    s16 gyro_x, gyro_y, gyro_z;
};

struct regmap_demo_priv {
    struct regmap *regmap;
    struct device *dev;
};

/*
 * All of the raw sensor output registers change on their own between
 * reads (new samples arrive continuously from the sensor's internal
 * ADCs), so they must never be served from the register cache.
 * Config/control registers only change when we write them, so those
 * are safe to cache.
 */
static bool regmap_demo_volatile_reg(struct device *dev, unsigned int reg)
{
    switch (reg) {
    case MPU6500_ACCEL_XOUT_H ... MPU6500_ACCEL_XOUT_H + MPU6500_SAMPLE_BURST_LEN - 1:
        return true;
    default:
        return false;
    }
}

static const struct regmap_config regmap_demo_config = {
    .reg_bits = 8,
    .val_bits = 8,
    .max_register = MPU6500_MAX_REGISTER,
    .cache_type = REGCACHE_MAPLE,
    .volatile_reg = regmap_demo_volatile_reg,
    .read_flag_mask = MPU6500_SPI_READ_FLAG,
};

/*
 * regmap_demo_read_sample - burst-read all 14 bytes of motion data in
 * one SPI transaction and convert to signed 16-bit values.
 *
 * Because the accel/temp/gyro registers are marked volatile, this
 * always issues a real SPI transaction rather than returning cached
 * bytes.
 */
static int regmap_demo_read_sample(struct regmap_demo_priv *priv, struct MPU6500_sample *sample)
{
    u8 raw[MPU6500_SAMPLE_BURST_LEN];
    int ret;

    ret = regmap_bulk_read(priv->regmap, MPU6500_ACCEL_XOUT_H,
                            raw, sizeof(raw));
    if (ret)
        return ret;

    sample->accel_x = (s16)((raw[0] << 8) | raw[1]);   // ACCEL_XOUT_H << 8 | ACCEL_XOUT_L
    sample->accel_y = (s16)((raw[2] << 8) | raw[3]);   // ACCEL_YOUT_H << 8 | ACCEL_YOUT_L
    sample->accel_z = (s16)((raw[4] << 8) | raw[5]);   // ACCEL_ZOUT_H << 8 | ACCEL_ZOUT_L
    sample->temp    = (s16)((raw[6] << 8) | raw[7]);   // TEMP_OUT_H   << 8 | TEMP_OUT_L
    sample->gyro_x  = (s16)((raw[8] << 8) | raw[9]);   // GYRO_XOUT_H  << 8 | GYRO_XOUT_L
    sample->gyro_y  = (s16)((raw[10] << 8) | raw[11]); // GYRO_YOUT_H  << 8 | GYRO_YOUT_L
    sample->gyro_z  = (s16)((raw[12] << 8) | raw[13]); // GYRO_ZOUT_H  << 8 | GYRO_ZOUT_L

    return 0;
}

/*
 * regmap_demo_wake - clear the SLEEP bit in PWR_MGMT_1.
 *
 * The MPU6500 powers up with SLEEP set, so nothing else on the device
 * works (registers just read back stale/zero data) until this is
 * cleared. We also select the PLL clock source in the same write for
 * better long-term timing accuracy than the internal oscillator.
 */
static int regmap_demo_wake(struct regmap_demo_priv *priv)
{
    int ret;

    ret = regmap_write(priv->regmap, MPU6500_PWR_MGMT_1,
                        PWR_MGMT_1_CLKSEL_PLL);
    if (ret)
        return ret;

    /* Datasheet: allow time for the clock/PLL to stabilize after
     * leaving sleep and switching clock source. */
    usleep_range(10000, 12000);

    return 0;
}

static ssize_t sensor_read_show(struct device *dev, struct device_attribute *attr, char *buf){
    struct regmap_demo_priv *priv = dev_get_drvdata(dev);

    struct MPU6500_sample sample;

    /* Sanity-check readback of live sensor data at probe time */
    int ret = regmap_demo_read_sample(priv, &sample);
    if (ret) {
        return snprintf(buf, PAGE_SIZE,"Failed to read initial sample: %d\n", ret);
    }

    return snprintf(buf, PAGE_SIZE, "Sensor row readings: accel=(%d,%d,%d) temp=%d gyro=(%d,%d,%d)\n",
             sample.accel_x, sample.accel_y, sample.accel_z,
             sample.temp,
             sample.gyro_x, sample.gyro_y, sample.gyro_z);
}
static DEVICE_ATTR_RO(sensor_read);

static ssize_t set_grconf_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count){
    struct regmap_demo_priv *priv = dev_get_drvdata(dev);

    int val;
    int ret;

    ret = kstrtoint(buf, 10, &val);
    if (ret < 0)
        return ret;

    if(val < 0 || val > 3)
        val = 0;

    val = val << 3;

    // This safely overwrites ONLY bits 4 and 3 with 'val', ignoring everything else
    ret = regmap_update_bits(priv->regmap, MPU6500_GYRO_CONFIG, 0x18, val); // 0x18 -> specify bits 4 and 3
    if(ret){
        dev_err(priv->dev, "Failed regmap_update_bits: %d\n", ret);
    }

    dev_info(priv->dev, "Updated GYRO_FS_SEL: 0x%02x\n",val);
    return count;
}
static DEVICE_ATTR_WO(set_grconf);

static ssize_t get_grconf_show(struct device *dev, struct device_attribute *attr, char *buf){
    struct regmap_demo_priv *priv = dev_get_drvdata(dev);

    unsigned int val;
    int ret;

    ret= regmap_read(priv->regmap, MPU6500_GYRO_CONFIG, &val);
    if(ret)
        return ret;
    
    return snprintf(buf, PAGE_SIZE, "GYRO_CONFIG = 0x%02x\n", val);
}
static DEVICE_ATTR_RO(get_grconf);

static struct attribute *regmap_demo_attrs[] = {
    &dev_attr_sensor_read.attr,
    &dev_attr_set_grconf.attr,
    &dev_attr_get_grconf.attr,
    NULL,
};
ATTRIBUTE_GROUPS(regmap_demo);

static int regmap_demo_probe(struct spi_device *spi)
{
    struct regmap_demo_priv *priv;
    unsigned int whoami;
    int ret;

    dev_info(&spi->dev, "Probed called\n");
    /* MPU6500 SPI register interface tops out at 1 MHz; the DTS sets
     * spi-max-frequency, but pin it here too as a safety net in case
     * an overlay omits it. */
    spi->max_speed_hz = min(spi->max_speed_hz, 1000000U);
    spi->mode = SPI_MODE_0;
    ret = spi_setup(spi);
    if (ret) {
        dev_err(&spi->dev, "spi_setup failed: %d\n", ret);
        return ret;
    }

    priv = devm_kzalloc(&spi->dev, sizeof(*priv), GFP_KERNEL);
    if (!priv)
        return -ENOMEM;

    priv->regmap = devm_regmap_init_spi(spi, &regmap_demo_config);

    if (IS_ERR(priv->regmap)) {
        dev_err(&spi->dev, "Failed to init regmap: %ld\n",
                PTR_ERR(priv->regmap));
        return PTR_ERR(priv->regmap);
    }

    priv->dev = &spi->dev;
    spi_set_drvdata(spi, priv);

    /* Confirm we're actually talking to an MPU6500 before touching
     * anything else. WHO_AM_I is fixed silicon ID, not user data. */
    ret = regmap_read(priv->regmap, MPU6500_WHO_AM_I, &whoami);
    if (ret) {
        dev_err(&spi->dev, "Failed to read WHO_AM_I: %d\n", ret);
        return ret;
    }

    if (whoami != MPU6500_WHO_AM_I_VAL) {
        dev_err(&spi->dev,
                "Unexpected WHO_AM_I: 0x%02x -> expected 0x%02x)\n",
                whoami, MPU6500_WHO_AM_I_VAL);
        return -ENODEV;
    }

    ret = regmap_demo_wake(priv);
    if (ret) {
        dev_err(&spi->dev, "Failed to wake device: %d\n", ret);
        return ret;
    }

    /* Sample rate = Gyro output rate / (1 + SMPLRT_DIV). With DLPF
     * enabled the gyro output rate is 1kHz, so divider 9 -> 100Hz. */
    ret = regmap_write(priv->regmap, MPU6500_SMPLRT_DIV, 9);
    if (ret) {
        dev_err(&spi->dev, "Failed to set sample rate: %d\n", ret);
        return ret;
    }

    regmap_update_bits(priv->regmap, 0x1B, 0x18, 0x00);

    dev_info(&spi->dev, "MPU6500 probed OK (SPI0)\n");

    return 0;
}

static void regmap_demo_remove(struct spi_device *spi)
{
    struct regmap_demo_priv *priv = spi_get_drvdata(spi);

    /* Put the device back to sleep on removal to save power. */
    regmap_update_bits(priv->regmap, MPU6500_PWR_MGMT_1,
                        PWR_MGMT_1_SLEEP_MASK, PWR_MGMT_1_SLEEP_MASK);

    dev_info(priv->dev, "MPU6500 regmap-demo device removed\n");
}

static const struct of_device_id regmap_demo_of_match[] = {
    { .compatible = "mpcoding,regmap-demo" },
    { }
};
MODULE_DEVICE_TABLE(of, regmap_demo_of_match);

/* Ensure regmap-spi is loaded before this module, so devm_regmap_init_spi
 * resolves correctly with modprobe-based loading. */
MODULE_SOFTDEP("pre: regmap-spi");

static struct spi_driver regmap_demo_driver = {
    .driver = {
        .name = "regmap-demo",
        .of_match_table = regmap_demo_of_match,
        .dev_groups = regmap_demo_groups,
    },
    .probe = regmap_demo_probe,
    .remove = regmap_demo_remove,
};

module_spi_driver(regmap_demo_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("MPCoding - LDD");
MODULE_DESCRIPTION("Regmap API demo driver for the MPU6500 6-axis IMU (SPI0)");