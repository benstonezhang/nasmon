/*
 * Created by benstone on 2019/10/6.
 */

#include <sys/ioctl.h>
#include <linux/hdreg.h>
#include <scsi/sg.h>
#include <scsi/scsi.h>
#include <scsi/scsi_ioctl.h>
#include <byteswap.h>
#include <dirent.h>
#include <fcntl.h>
#include <errno.h>
#include <unitypes.h>
#include <unistd.h>
#include <syslog.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#include "nasmon.h"

static const time_t smart_update_interval = 600;
static const unsigned char temp_notice = 45;
static const unsigned char temp_warn = 50;
static const unsigned char temp_halt = 55;

static unsigned char temp_high = 0;

enum e_powermode {
    PWM_UNKNOWN,
    PWM_ACTIVE,
    PWM_SLEEPING,
    PWM_STANDBY
};

/* default is 194 */
static unsigned char temp_attr_ids[] = {194, 190};

static enum e_powermode ata_get_powermode(const int fd) {
    unsigned char args[4] = {0xE5, 0, 0, 0}; /* try first with 0xe5 */
    enum e_powermode state = PWM_UNKNOWN;

    /*
      After ioctl:
        args[0] = status;
        args[1] = error;
        args[2] = nsector_reg;
    */

    if (ioctl(fd, HDIO_DRIVE_CMD, &args)
        && (args[0] = 0x98) /* try again with 0x98 */
        && ioctl(fd, HDIO_DRIVE_CMD, &args)) {
        if (errno != EIO || args[0] != 0 || args[1] != 0) {
            state = PWM_UNKNOWN;
        } else {
            state = PWM_SLEEPING;
        }
    } else {
        state = ((args[2] == 0xFF) ? PWM_ACTIVE : PWM_STANDBY);
    }

    return state;
}

static void hd_fixstring(unsigned char *s, const int bytecount,
                         const int word_swap) {
    unsigned char *p, *end;

    p = s;
    if (word_swap == 0) {
        end = s + bytecount;
    } else {
        /* bytecount must be even */
        end = &s[bytecount & ~1];

        /* convert from big-endian to string order */
        for (p = end; p != s;) {
            unsigned short *pp = (unsigned short *) (p -= 2);
            *pp = bswap_16(*pp);
        }
    }

    /* strip leading blanks */
    while (s != end && *s == ' ') {
        s++;
    }
    /* compress internal blanks and strip trailing blanks */
    while (s != end && *s) {
        if (*s++ != ' ' || (s != end && *s && *s != ' ')) {
            *p++ = *(s - 1);
        }
    }
    /* wipe out trailing garbage */
    while (p != end) {
        *p++ = '\0';
    }
}

static int scsi_SG_IO(const int fd, unsigned char *cdb, const int cdb_len,
                      unsigned char *buffer, const int buffer_len,
                      unsigned char *sense, const unsigned char sense_len,
                      const int dxfer_direction) {
    struct sg_io_hdr io_hdr;

    memset(&io_hdr, 0, sizeof(struct sg_io_hdr));
    io_hdr.interface_id = 'S';
    io_hdr.cmdp = cdb;
    io_hdr.cmd_len = cdb_len;
    io_hdr.dxfer_len = buffer_len;
    io_hdr.dxferp = buffer;
    io_hdr.mx_sb_len = sense_len;
    io_hdr.sbp = sense;
    io_hdr.dxfer_direction = dxfer_direction;
    io_hdr.timeout = 3000; /* 3 seconds should be ample */

    return ioctl(fd, SG_IO, &io_hdr);
}

static int scsi_SEND_COMMAND(const int fd, unsigned char *cdb, int cdb_len,
                             unsigned char *buffer, const int buffer_len,
                             const int dxfer_direction) {
    unsigned int inbufsize, outbufsize, ret;
    unsigned char buf[2048];

    switch (dxfer_direction) {
        case SG_DXFER_FROM_DEV:
            inbufsize = 0;
            outbufsize = buffer_len;
            break;
        case SG_DXFER_TO_DEV:
            inbufsize = buffer_len;
            outbufsize = 0;
            break;
        default:
            inbufsize = 0;
            outbufsize = 0;
            break;
    }
    memcpy(buf, &inbufsize, sizeof(inbufsize));
    memcpy(buf + sizeof(inbufsize), &outbufsize, sizeof(outbufsize));
    memcpy(buf + sizeof(inbufsize) + sizeof(outbufsize), cdb, cdb_len);
    memcpy(buf + sizeof(inbufsize) + sizeof(outbufsize) + cdb_len, buffer,
           buffer_len);

    ret = ioctl(fd, SCSI_IOCTL_SEND_COMMAND, buf);
    memcpy(buffer, buf + sizeof(inbufsize) + sizeof(outbufsize), buffer_len);

    return ret;
}

static int
scsi_command(int device, unsigned char *cdb, int cdb_len, unsigned char *buffer,
             int buffer_len, int dxfer_direction) {
    static int SG_IO_supported = -1;
    int ret;

    if (SG_IO_supported == 1) {
        return scsi_SG_IO(device, cdb, cdb_len, buffer, buffer_len, NULL, 0,
                          dxfer_direction);
    }

    if (SG_IO_supported == 0) {
        return scsi_SEND_COMMAND(device, cdb, cdb_len, buffer, buffer_len,
                                 dxfer_direction);
    }

    ret = scsi_SG_IO(device, cdb, cdb_len, buffer, buffer_len, NULL, 0,
                     dxfer_direction);
    if (ret == 0) {
        SG_IO_supported = 1;
        return ret;
    } else {
        SG_IO_supported = 0;
        return scsi_SEND_COMMAND(device, cdb, cdb_len, buffer, buffer_len,
                                 dxfer_direction);
    }
}

static int
scsi_inquiry(int device, unsigned char *buffer, const unsigned char size) {
    int ret;
    unsigned char cdb[6];

    memset(cdb, 0, sizeof(cdb));
    cdb[0] = INQUIRY;
    cdb[4] = size;

    ret = scsi_command(device, cdb, sizeof(cdb), buffer, cdb[4],
                       SG_DXFER_FROM_DEV);
    if (ret == 0) {
        hd_fixstring(buffer + 8, 24, 0);
        buffer[32] = 0;
    }

    return ret;
}

static int
sata_pass_thru(const int fd, const unsigned char *cmd, unsigned char *buffer) {
    int dxfer_direction;
    int ret;
    unsigned char cdb[16], sense[32];

    memset(cdb, 0, sizeof(cdb));
    cdb[0] = 0x85;  /* 16-byte pass-thru */
    if (cmd[3]) {
        cdb[1] = (4 << 1); /* PIO Data-in */
        cdb[2] = 0x2e;
        /* no off.line, cc, read from dev, lock count in sector count field */
        dxfer_direction = SG_DXFER_FROM_DEV;
    } else {
        cdb[1] = (3 << 1); /* Non-data */
        cdb[2] = 0x20;     /* cc */
        dxfer_direction = SG_DXFER_NONE;
    }
    cdb[4] = cmd[2];
    if (cmd[0] == WIN_SMART) {
        cdb[6] = cmd[3];
        cdb[8] = cmd[1];
        cdb[10] = 0x4f;
        cdb[12] = 0xc2;
    } else {
        cdb[6] = cmd[1];
    }
    cdb[14] = cmd[0];

    ret = scsi_SG_IO(fd, cdb, sizeof(cdb), buffer, cmd[3] * 512, sense,
                     sizeof(sense), dxfer_direction);

    /* Verify SATA magic */
    return sense[0] == 0x72 ? ret : 1;
}

static inline int sata_enable_smart(const int fd) {
    unsigned char cmd[4] = {WIN_SMART, 0, SMART_ENABLE, 0};
    return sata_pass_thru(fd, cmd, NULL);
}

static inline int sata_get_smart_values(const int fd, unsigned char *buff) {
    unsigned char cmd[4] = {WIN_SMART, 0, SMART_READ_VALUES, 1};
    return sata_pass_thru(fd, cmd, buff);
}

static int sata_probe(const int fd) {
    int bus_num;
    unsigned char cmd[4] = {WIN_IDENTIFY, 0, 0, 1};
    unsigned char identify[512];
    /* should be 36 for unsafe devices (like USB mass storage stuff)
       otherwise they can lock up! SPC sections 7.4 and 8.6 */
    unsigned char buf[36];

    /* SATA disks are difficult to detect as they answer to both ATA
     * and SCSI commands */

    /* First check that the device is accessible through SCSI */
    if (ioctl(fd, SCSI_IOCTL_GET_BUS_NUMBER, &bus_num))
        return 0;

    /* Get SCSI name and verify it starts with "ATA " */
    if (scsi_inquiry(fd, buf, sizeof(buf)))
        return 0;
    else if (strncmp((char *) buf + 8, "ATA ", 4) != 0)
        return 0;

    /* Verify that it supports ATA pass thru */
    if (sata_pass_thru(fd, cmd, identify) != 0)
        return 0;
    else
        return 1;
}

static void sata_model(const int fd, char *buf, const size_t len) {
    unsigned char cmd[4] = {WIN_IDENTIFY, 0, 0, 1};
    unsigned char identify[512];

    if (sata_pass_thru(fd, cmd, identify)) {
        strncpy(buf, "unknown", len);
    } else {
        hd_fixstring(identify + 54, 24, 1);
        strncpy(buf, (char *) identify + 54, len);
    }
}

static const unsigned char *
sata_search_temperature(const unsigned char *smart_data,
                        const unsigned char attribute_id) {
    smart_data += 3;
    for (int i = 0; i < 30; i++) {
#ifndef NDEBUG
        syslog(LOG_DEBUG, "SMART field(%d) = %d", *smart_data,
               *(smart_data + 3));
#endif
        if ((*smart_data) == attribute_id) {
            return smart_data + 3;
        }
        smart_data += 12;
    }

    return NULL;
}

static unsigned char
sata_get_temperature(const int fd, const unsigned char attr_id) {
    const unsigned char *field;
    unsigned char temp = 0;
    unsigned char values[512];

    /* get SMART values */
    if (sata_get_smart_values(fd, values) == 0) {
        uint16_t *p = (uint16_t *) values;
        for (int i = 0; i < 256; i++) {
            *p = bswap_16(*p);
            p++;
        }

        /* temperature */
        field = sata_search_temperature(values, attr_id);
        if (field != NULL) {
            temp = *field;
        }
    } else {
        nas_log_error();
    }

    return temp;
}

struct nas_disk_info {
    const char *name;
    const char *model;
    int fd;
    unsigned char attr_id;
    unsigned char temp;
};

static int nas_disk_count = 0;
static struct nas_disk_info *nas_disk_list = NULL;

void nas_disk_free(void) {
    if (nas_disk_list != NULL) {
        for (int i = 0; i < nas_disk_count; i++) {
            if (nas_disk_list[i].fd >= 0) {
                nas_safe_close(nas_disk_list[i].fd);
            }
            if (nas_disk_list[i].name != NULL) {
                free((void *) nas_disk_list[i].name);
            }
            if (nas_disk_list[i].model != NULL) {
                free((void *) nas_disk_list[i].model);
            }
        }
        free(nas_disk_list);
    }
}

static int nas_sata_filter(const struct dirent *ent) {
    return (ent->d_type == DT_BLK) &&
           (ent->d_name[0] == 's') && (ent->d_name[1] == 'd') &&
           (ent->d_name[2] >= 'a') && (ent->d_name[2] <= 'z') &&
           (ent->d_name[3] == '\0') ? 1 : 0;
}

void nas_disk_init(void) {
    struct dirent **namelist;
    int count;

    count = scandir("/dev", &namelist, nas_sata_filter, alphasort);
    if (count < 0) {
        syslog(LOG_ERR, "failed to open device dir to scan disk");
        exit(EXIT_FAILURE);
    }

    if ((nas_disk_list = calloc(sizeof(*nas_disk_list),
                                (size_t) count)) == NULL) {
        syslog(LOG_ERR, "failed to allocate memory for disk list");
        exit(EXIT_FAILURE);
    }

    nas_disk_count = 0;
    for (int i = 0; i < count; free(namelist[i++])) {
        char name[strlen(namelist[i]->d_name) + 5];

        strcpy(name, "/dev/");
        strcpy(name + 5, namelist[i]->d_name);

        if ((nas_disk_list[i].fd = open(name, O_RDONLY)) < 0) {
            syslog(LOG_ERR, "skip open failed disk device file: %s", name);
            continue;
        }

#ifndef NDEBUG
        syslog(LOG_DEBUG, "probe disk device: %s", name);
#endif
        if (sata_probe(nas_disk_list[i].fd) != 1) {
            syslog(LOG_INFO, "skip non-SMART device: %s", name);
            nas_safe_close(nas_disk_list[i].fd);
            continue;
        }

        if ((nas_disk_list[i].name = strdup(name)) == NULL) {
            syslog(LOG_ERR, "failed to save disk name");
            exit(EXIT_FAILURE);
        }

        char buf[128];
        sata_model(nas_disk_list[i].fd, buf, sizeof(buf));
        nas_disk_list[i].model = strdup(buf);
#ifndef NDEBUG
        syslog(LOG_DEBUG, "found device: %s %s", name, buf);
#endif

        /* enable SMART */
        if (sata_enable_smart(nas_disk_list[i].fd) != 0) {
            if (errno == EIO) {
                syslog(LOG_INFO, "%s: S.M.A.R.T. not available, skip", name);
                nas_safe_close(nas_disk_list[i].fd);
                continue;
            } else {
                nas_log_error();
                exit(EXIT_FAILURE);
            }
        }

        int j = 0;
        for (; j < sizeof(temp_attr_ids) / sizeof(temp_attr_ids[0]); j++) {
            nas_disk_list[i].temp = sata_get_temperature(nas_disk_list[i].fd,
                                                         temp_attr_ids[j]);

            if (nas_disk_list[i].temp > 0) {
                syslog(LOG_INFO, "%s: %s, temperature %dC",
                       nas_disk_list[i].name,
                       nas_disk_list[i].model, nas_disk_list[i].temp);
                nas_disk_list[i].attr_id = temp_attr_ids[j];
                nas_disk_count++;
                break;
            }
        }

        nas_safe_close(nas_disk_list[i].fd);

        if (j >= sizeof(temp_attr_ids) / sizeof(temp_attr_ids[0])) {
            syslog(LOG_WARNING, "%s: can not read temperature", name);
        }
    }

    free(namelist);
    atexit(nas_disk_free);
}

int nas_disk_update(time_t now) {
    static time_t last_tick = 0;
    enum e_powermode mode;
    int err = 0;

    if (now - last_tick < smart_update_interval) {
        return err;
    }

    temp_high = 0;
    for (int i = 0; i < nas_disk_count; i++) {
        nas_disk_list[i].fd = open(nas_disk_list[i].name, O_RDONLY);
        if (nas_disk_list[i].fd < 0) {
            char buf[256];
            strerror_r(errno, buf, sizeof(buf));
            syslog(LOG_ERR, "failed to open disk device file %s: %s",
                   nas_disk_list[i].name, buf);
            exit(EXIT_FAILURE);
        }

        mode = ata_get_powermode(nas_disk_list[i].fd);

        if ((mode != PWM_STANDBY) && (mode != PWM_SLEEPING)) {
            nas_disk_list[i].temp = sata_get_temperature(
                    nas_disk_list[i].fd, nas_disk_list[i].attr_id);
#ifndef NDEBUG
            syslog(LOG_DEBUG, "%s: %s, temperature %dC",
                   nas_disk_list[i].name, nas_disk_list[i].model,
                   nas_disk_list[i].temp);
#endif

            if (nas_disk_list[i].temp > temp_high) {
                temp_high = nas_disk_list[i].temp;
            }

            if (nas_disk_list[i].temp >= temp_warn) {
                syslog(LOG_WARNING, "%s: high temperature %dC",
                       nas_disk_list[i].name, nas_disk_list[i].temp);

                if (nas_disk_list[i].temp >= temp_halt) {
                    syslog(LOG_ALERT,
                           "%s: temperature too high, need to shutdown",
                           nas_disk_list[i].name);
                    err++;
                }
            }
        } else {
            nas_disk_list[i].temp = 0;
        }

        nas_safe_close(nas_disk_list[i].fd);
    }
    last_tick = now;

    return err;
}

int nas_disk_get_pwm(void) {
    int pwm = (int) (255.0 * (temp_high - temp_notice) / (temp_halt - temp_notice));
    if (pwm < 0) {
        pwm = 0;
    } else if (pwm > 255) {
        pwm = 255;
    }

#ifndef NDEBUG
    syslog(LOG_DEBUG, "disk temp: %.2f, pwm output %d", temp_high, pwm);
#endif

    return pwm;
}

int nas_disk_item_show(const int off) {
    static int id = -1;

    id = id >= 0 ? (nas_disk_count + id + off) % nas_disk_count : 0;

    lcd_printf(1, "%s", nas_disk_list[id].model);
    lcd_printf(2, "%s: %d C", nas_disk_list[id].name, nas_disk_list[id].temp);

    return id;
}

static void nas_disk_group_show(const int line, const int off) {
    switch (nas_disk_count - off) {
        case 1:
            lcd_printf(line, "HD: %dC N/A N/A", nas_disk_list[off].temp);
            break;
        case 2:
            lcd_printf(line, "HD: %dC %dC N/A", nas_disk_list[off].temp,
                       nas_disk_list[off + 1].temp);
            break;
        default:
            lcd_printf(line, "HD: %dC %dC %dC", nas_disk_list[off].temp,
                       nas_disk_list[off + 1].temp,
                       nas_disk_list[off + 2].temp);
            break;
    }
}

void nas_disk_summary_show(void) {
    nas_disk_group_show(1, 0);
    nas_disk_group_show(2, 3);
}

int nas_disk_to_json(char *buf, const size_t len) {
    int count = 0;
    for (int i = 0; i < nas_disk_count; i++) {
        if (i != 0) {
            buf[count++] = ',';
        }
        const struct nas_disk_info *p = nas_disk_list + i;
        count += snprintf(buf + count, len - count,
                          "\"%s\":{\"Model\":\"%s\",\"Temp\":%d}",
                          p->name, p->model, p->temp);
    }
    return count;
}