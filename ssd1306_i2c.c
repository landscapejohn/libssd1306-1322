/*
 * COPYRIGHT: 2020. Stealthy Labs LLC.
 * DATE: 2020-01-15
 * SOFTWARE: libssd1306-i2c
 * LICENSE: Refer license file
 */
#include <features.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
//#include <i2c/smbus.h>
#include <sys/ioctl.h>
#include <linux/i2c-dev.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ssd1306_i2c.h>

ssd1306_i2c_t *ssd1306_i2c_open(
        const char *dev, // name of the device such as /dev/i2c-1. cannot be NULL
        uint8_t addr, // I2C address of the device. valid values: 0 (default) or 0x3c or 0x3d
        uint8_t width, // OLED display width. valid values: 0 (default) or 128
        uint8_t height, // OLED display height. valid values: 0 (default) or 32 or 64
        FILE *logerr
    )
{
    ssd1306_i2c_t *oled = NULL;
    int rc = 0;
    FILE *err_fp = logerr == NULL ? stderr : logerr;
    do {
        if (!dev) {
            fprintf(err_fp, "ERROR: No device given.\n");
            rc = -1;
            break;
        }
        oled = calloc(1, sizeof(*oled));
        if (!oled) {
            fprintf(err_fp, "ERROR: Failed to allocate memory of size %zu bytes\n", sizeof(*oled));
            rc = -1;
            break;
        }
        oled->fd = -1; // force fd to be -1
        oled->err.err_fp = err_fp;
        oled->err.errstr = calloc(1, 256);
        if (!oled->err.errstr) {
            fprintf(err_fp, "ERROR: Failed to allocate memory of size 256 bytes\n");
            rc = -1;
            break;
        }
        oled->err.errstr_max_len = 256;
        oled->dev = strdup(dev);
        if (!oled->dev) {
            oled->err.errnum = errno;
            strerror_r(oled->err.errnum, oled->err.errstr, oled->err.errstr_max_len);
            fprintf(err_fp, "WARN: Failed to copy device name: %s. Ignoring potential memory error: %s\n", dev, oled->err.errstr);
        } else {
            oled->err.errnum = 0;
            memset(oled->err.errstr, 0, oled->err.errstr_max_len);
            strerror_r(oled->err.errnum, oled->err.errstr, oled->err.errstr_max_len);
        }
        if (addr == 0x3c || addr == 0) {
            oled->addr = 0x3c;
        } else if (addr == 0x3d) {
            oled->addr = 0x3d;
        } else {
            fprintf(err_fp, "WARN: I2C device addr cannot be 0x%x. Using 0x3c\n",
                    addr);
            oled->addr = 0x3c;
        }
        oled->width = 128; //XXX: can this be 64 ?
        if (height == 32 || height == 0) {
            oled->height = 32;
        } else if (height == 64) {
            oled->height = 64;
        } else {
            fprintf(err_fp, "WARN: OLED screen height cannot be %d. Using %d\n",
                    height, 32);
            oled->height = 32;
        }
        oled->screen_buffer = calloc(1, (oled->width * oled->height + 1) * sizeof(uint8_t));
        if (!oled->screen_buffer) {
            oled->err.errnum = errno;
            strerror_r(oled->err.errnum, oled->err.errstr, oled->err.errstr_max_len);
            fprintf(err_fp, "ERROR: Out of memory allocating %zu bytes for screen buffer\n",
                    (oled->width * oled->height + 1) * sizeof(uint8_t));
            rc = -1;
            break;
        }
        oled->fd = open(dev, O_RDWR);
        if (oled->fd < 0) {
            oled->err.errnum = errno;
            strerror_r(oled->err.errnum, oled->err.errstr, oled->err.errstr_max_len);
            fprintf(err_fp, "ERROR: Failed to open %s in read/write mode: %s\n",
                    dev, oled->err.errstr);
            rc = -1;
            break;
        } else {
            fprintf(err_fp, "INFO: Opened %s at fd %d\n", dev, oled->fd);
            uint32_t addr = (uint32_t)oled->addr;
            if (ioctl(oled->fd, I2C_SLAVE, addr) < 0) {
                oled->err.errnum = errno;
                strerror_r(oled->err.errnum, oled->err.errstr, oled->err.errstr_max_len);
                fprintf(err_fp, "ERROR: Failed to set I2C_SLAVE for %s addr 0x%x: %s\n",
                        dev, addr, oled->err.errstr);
                rc = -1;
                break;
            } else {
                fprintf(err_fp, "INFO: I2C_SLAVE for %s addr 0x%x opened in RDWR mode\n",
                        dev, addr);
                rc = 0;
            }
        }
    } while (0);
    if (rc < 0) {
        ssd1306_i2c_close(oled);
        oled = NULL;
    }
    return oled;
}

void ssd1306_i2c_close(ssd1306_i2c_t *oled)
{
    if (oled) {
        if (oled->fd > 0) {
            close(oled->fd);
        }
        if (oled->screen_buffer) {
            free(oled->screen_buffer);
        }
        oled->screen_buffer = NULL;
        if (oled->dev) {
            free(oled->dev);
        }
        oled->dev = NULL;
        if (oled->err.errstr) {
            free(oled->err.errstr);
            oled->err.errstr = NULL;
        }
        memset(oled, 0, sizeof(*oled));
        free(oled);
        oled = NULL;
    }
}

static size_t ssd1306_i2c_internal_get_cmd_bytes(ssd1306_i2c_cmd_t cmd,
        uint8_t *data, size_t dlen, uint8_t *cmdbuf, size_t cmd_buf_max)
{
    size_t sz = 2; // default
    if (!cmdbuf || cmd_buf_max < 16 || (data != NULL && dlen == 0)) {
        return 0;//error
    }
    // fill it up
    for (size_t idx = 0; idx < cmd_buf_max; ++idx) {
        if (idx % 2 == 0) {
            cmdbuf[idx] = 0x80; // Co: 1 D/C#: 0 0b10000000
        } else {
            cmdbuf[idx] = 0xE3; // NOP by default
        }
    }
    switch (cmd) {
    case SSD1306_I2C_CMD_POWER_OFF: cmdbuf[1] = 0xAE; break;
    case SSD1306_I2C_CMD_POWER_ON: cmdbuf[1] = 0xAF; break;
    case SSD1306_I2C_CMD_MEM_ADDR_HORIZ:
        cmdbuf[1] = 0x20; // Set memory address
        cmdbuf[3] = 0x00; // horizontal
        sz = 4;
        break;
    case SSD1306_I2C_CMD_MEM_ADDR_VERT:
        cmdbuf[1] = 0x20; // Set memory address
        cmdbuf[3] = 0x01; // vertical
        sz = 4;
        break;
    case SSD1306_I2C_CMD_MEM_ADDR_PAGE:
        cmdbuf[1] = 0x20; // Set memory address
        cmdbuf[3] = 0x02; // page / reset
        sz = 4;
        break;
    case SSD1306_I2C_CMD_COLUMN_ADDR:
        cmdbuf[1] = 0x21; // set column address
        if (data && dlen >= 2) {
            cmdbuf[3] = data[0];
            cmdbuf[5] = data[1];
        } else {
            cmdbuf[3] = 0x00; // RESET
            cmdbuf[5] = 0x7F; // RESET
        }
        sz = 6;
        break;
    case SSD1306_I2C_CMD_PAGE_ADDR:
        cmdbuf[1] = 0x21; // set column address
        if (data && dlen >= 2) {
            cmdbuf[3] = data[0];
            cmdbuf[5] = data[1];
        } else {
            cmdbuf[3] = 0x00; // RESET
            cmdbuf[5] = 0x7F; // RESET
        }
        sz = 6;
        break;
    case SSD1306_I2C_CMD_DISP_START_LINE:
        if (data && dlen > 0) {
            cmdbuf[1] = 0x40 | (data[0] & 0x3F); // set display start line bytes. 40-7F
        } else {
            cmdbuf[1] = 0x40; // set display start line bytes. 40-7F
        }
        break;
    case SSD1306_I2C_CMD_DISP_OFFSET:
        cmdbuf[1] = 0xD3;
        cmdbuf[3] = (data && dlen > 0) ? data[0] & 0x3F : 0x00; // 0x00-0x3F;
        sz = 4;
        break;
    case SSD1306_I2C_CMD_DISP_CLOCK_DIVFREQ:
        cmdbuf[1] = 0xD5;
        cmdbuf[3] = (data && dlen > 0) ? data[0] : 0x80;
        sz = 4;
        break;
    case SSD1306_I2C_CMD_DISP_CONTRAST:
        cmdbuf[1] = 0x81;
        cmdbuf[3] = (data && dlen > 0) ? data[0] : 0x7F;
        sz = 4;
        break;
    case SSD1306_I2C_CMD_DISP_NORMAL: cmdbuf[1] = 0xA6; break;
    case SSD1306_I2C_CMD_DISP_INVERTED: cmdbuf[1] = 0xA7; break;
    case SSD1306_I2C_CMD_DISP_DISABLE_ENTIRE_ON: cmdbuf[1] = 0xA4; break;
    case SSD1306_I2C_CMD_DISP_ENTIRE_ON: cmdbuf[1] = 0xA5; break;
    case SSD1306_I2C_CMD_SEG_REMAP:
        if (data && dlen > 0) {
            cmdbuf[1] = 0xA0 | (data[0] & 0x1);
        } else {
            cmdbuf[1] = 0xA0;
        }
        break;
    case SSD1306_I2C_CMD_MUX_RATIO:
        cmdbuf[1] = 0xA8;
        cmdbuf[3] = (data && dlen > 0) ? data[0] : 0xFF;
        sz = 4;
        break;
    case SSD1306_I2C_CMD_COM_SCAN_DIRXN_NORMAL: cmdbuf[1] = 0xC0; break;
    case SSD1306_I2C_CMD_COM_SCAN_DIRXN_INVERT: cmdbuf[1] = 0xC8; break;
    case SSD1306_I2C_CMD_COM_PIN_CFG:
        cmdbuf[1] = 0xDA;
        cmdbuf[3] = (data && dlen > 0) ? (data[0] & 0x32) : 0x02; // valid values: 0x02, 0x12, 0x22, 0x32
        sz = 4;
        break;
    case SSD1306_I2C_CMD_PRECHARGE_PERIOD:
        cmdbuf[1] = 0xD9;
        cmdbuf[3] = (data && dlen > 0) ? data[0] : 0x22; // 0 is invalid
        sz = 4;
        break;
    case SSD1306_I2C_CMD_VCOMH_DESELECT:
        cmdbuf[1] = 0xDB;
        cmdbuf[3] = (data && dlen > 0) ? (data[0] & 0x70) : 0x30; // 0b0AAA0000
        sz = 4;
        break;
    case SSD1306_I2C_CMD_ENABLE_CHARGE_PUMP:
        cmdbuf[1] = 0x8D;
        cmdbuf[3] = 0x14; // 0b00010100
        sz = 4;
        break;
    case SSD1306_I2C_CMD_DISABLE_CHARGE_PUMP:
        cmdbuf[1] = 0x8D;
        cmdbuf[3] = 0x10; // 0b00010000
        sz = 4;
        break;
    case SSD1306_I2C_CMD_NOP: // fallthrough
    default:
        cmdbuf[1] = 0xE3; // NOP
        break;
    }
    return sz;
}

int ssd1306_i2c_display_initialize(ssd1306_i2c_t *oled)
{
    int rc = 0;
    uint8_t data;
    // power off the display before doing anything
    rc |= ssd1306_i2c_run_cmd(oled, SSD1306_I2C_CMD_POWER_OFF, 0, 0);
    // force horizontal memory addressing
    rc |= ssd1306_i2c_run_cmd(oled, SSD1306_I2C_CMD_MEM_ADDR_HORIZ, 0, 0);
    // these instructions are from the software configuration section 15.2.3 in
    // the datasheet
    // Set MUX Ratio 0xA8, 0x3F
    data = oled->height - 1;
    rc |= ssd1306_i2c_run_cmd(oled, SSD1306_I2C_CMD_MUX_RATIO, &data, 1);
    // Set display offset 0xD3, 0x00
    data = 0x00;
    rc |= ssd1306_i2c_run_cmd(oled, SSD1306_I2C_CMD_DISP_OFFSET, &data, 1);
    // set display start line 0x40
    rc |= ssd1306_i2c_run_cmd(oled, SSD1306_I2C_CMD_DISP_START_LINE, 0, 0);
    // set segment remap 0xA0/0xA1
    data = 0x01;
    rc |= ssd1306_i2c_run_cmd(oled, SSD1306_I2C_CMD_SEG_REMAP, &data, 1);
    // set com output scan direction 0xC0/0xC8
    rc |= ssd1306_i2c_run_cmd(oled, SSD1306_I2C_CMD_COM_SCAN_DIRXN_INVERT, 0, 0);
    // set com pins hardware config 0xDA, 0x02
    data = 0x02;
    rc |= ssd1306_i2c_run_cmd(oled, SSD1306_I2C_CMD_COM_PIN_CFG, &data, 1);
    // set contrast control 0x81, 0x7F
    data = 0x7F;
    rc |= ssd1306_i2c_run_cmd(oled, SSD1306_I2C_CMD_DISP_CONTRAST, &data, 1);
    // disable entire display on 0xA4
    rc |= ssd1306_i2c_run_cmd(oled, SSD1306_I2C_CMD_DISP_DISABLE_ENTIRE_ON, 0, 0);
    // set normal display 0xA6
    rc |= ssd1306_i2c_run_cmd(oled, SSD1306_I2C_CMD_DISP_NORMAL, 0, 0);
    // set osc frequency 0xD5, 0x80
    data = 0x80;//RESET 0b10000000
    rc |= ssd1306_i2c_run_cmd(oled, SSD1306_I2C_CMD_DISP_CLOCK_DIVFREQ, &data, 1);
    data = 0xF1;
    rc |= ssd1306_i2c_run_cmd(oled, SSD1306_I2C_CMD_PRECHARGE_PERIOD, &data, 1);
    data = 0x30;
    rc |= ssd1306_i2c_run_cmd(oled, SSD1306_I2C_CMD_VCOMH_DESELECT, &data, 1);
    // enable charge pump regulator 0x8D, 0x14
    // charge pump has to be followed by a power on. section 15.2.1 in datasheet
    rc |= ssd1306_i2c_run_cmd(oled, SSD1306_I2C_CMD_ENABLE_CHARGE_PUMP, 0, 0);
    // power display on 0xAF
    rc |= ssd1306_i2c_run_cmd(oled, SSD1306_I2C_CMD_POWER_ON, 0, 0);
    return rc;
}

int ssd1306_i2c_run_cmd(ssd1306_i2c_t *oled, ssd1306_i2c_cmd_t cmd, uint8_t *data, size_t dlen)
{
    FILE *err_fp = (oled != NULL && oled->err.err_fp != NULL) ? oled->err.err_fp : stderr;
    if (!oled || oled->fd < 0) {
        fprintf(err_fp, "ERROR: Invalid ssd1306 I2C object\n");
        return -1;
    }
    if (dlen > 0 && !data) {
        fprintf(err_fp, "WARN: data pointer is NULL but dlen is %zu. Ignoring\n", dlen);
        dlen = 0;
        data = NULL;
    }
    if (dlen > 6 && data != NULL) {
        fprintf(err_fp, "WARN: the maximum accepted data bytes for a command is 6. You gave %zu, adjusting to 6\n", dlen);
        dlen = 6;
    }
    uint8_t cmd_buf[16];
    const size_t cmd_buf_max = 16;
    size_t cmd_sz = ssd1306_i2c_internal_get_cmd_bytes(cmd, data, dlen,
                        cmd_buf, cmd_buf_max);
    if (cmd_sz == 0 || cmd_sz > cmd_buf_max) {
        fprintf(err_fp, "WARN: Unknown cmd given %d\n", cmd);
        return -1;
    }
    ssize_t nb = write(oled->fd, cmd_buf, cmd_sz);
    if (nb < 0) {
        oled->err.errnum = errno;
        strerror_r(oled->err.errnum, oled->err.errstr, oled->err.errstr_max_len);
        fprintf(err_fp, "ERROR: Failed to write cmd ");
        for (size_t idx = 0; idx < cmd_sz; ++idx) {
            fprintf(err_fp, "%c0x%x%c", (idx == 0) ? '[' : ' ',
                    cmd_buf[idx], (idx == (cmd_sz - 1)) ? ',' : ']');
        }
        fprintf(err_fp, " to device fd %d: %s\n",
                oled->fd, oled->err.errstr);
        return -1;
    }
    fprintf(err_fp, "INFO: Wrote %zd bytes of cmd ", nb);
    for (size_t idx = 0; idx < cmd_sz; ++idx) {
        fprintf(err_fp, "%c0x%x%c", (idx == 0) ? '[' : ' ',
                cmd_buf[idx], (idx == (cmd_sz - 1)) ? ',' : ']');
    }
    fprintf(err_fp, " to device fd %d: %s\n",
                oled->fd, oled->err.errstr);
    return 0;
}

int ssd1306_i2c_display_update(ssd1306_i2c_t *oled)
{
    FILE *err_fp = (oled != NULL && oled->err.err_fp != NULL) ? oled->err.err_fp : stderr;
    if (!oled || oled->fd < 0) {
        fprintf(err_fp, "ERROR: Invalid ssd1306 I2C object\n");
        return -1;
    }
    int rc = 0;
    uint8_t x[2] = { 0, oled->width - 1 };
    rc |= ssd1306_i2c_run_cmd(oled, SSD1306_I2C_CMD_COLUMN_ADDR, x, 2);
    return rc;
}

