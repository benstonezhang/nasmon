/*
 * Created by benstone on 2/1/20.
 */

#include <unistd.h>
#include <syslog.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>
#include <stdlib.h>
#include <stdio.h>
#include <math.h>

#include "nasmon.h"

static const int pwm_update_threshold = 4;
static const int pwm_skip_max = 36;

static char *pwm_enable_dev = NULL;
static char default_pwm_enable = -1;
static unsigned char default_pwm_output = 0;
static int pwm_fd = -1;
static int pwm_last = 0;

static void nas_fan_set_enable(int enable, int save) {
    int pwm_enable_fd = open(pwm_enable_dev, O_RDWR);
    if (pwm_enable_fd < 0) {
        syslog(LOG_ERR, "open pwm_enable device failed: %d", errno);
        exit(EXIT_FAILURE);
    }

    char pwm_enable[3];

    if (save != 0) {
        if (read(pwm_enable_fd, pwm_enable, 1) <= 0) {
            syslog(LOG_ERR, "read pwm_enable device failed: %d", errno);
            exit(EXIT_FAILURE);
        }
        default_pwm_enable = (char) (pwm_enable[0] - '0');
    }

    if (default_pwm_enable != (char) enable) {
        pwm_enable[0] = (char) ('0' + enable);
        pwm_enable[1] = '\n';
        pwm_enable[2] = '\0';

        if (write(pwm_enable_fd, pwm_enable, 3) != 3) {
            syslog(LOG_ERR, "write pwm_enable device failed: %d", errno);
        }
    }

    nas_safe_close(pwm_enable_fd);
}

static void nas_fan_output(int value) {
    char pwm_buf[6];
    sprintf(pwm_buf, "%d\n", value);
    if (write(pwm_fd, pwm_buf, strlen(pwm_buf)) < 0) {
        syslog(LOG_ERR, "pwm output file write failed: %d", errno);
    }
}

void nas_fan_free(void) {
    if (pwm_fd >= 0) {
        syslog(LOG_INFO, "restore initial pwm output: %d", default_pwm_output);
        if (default_pwm_output != (unsigned char) pwm_last) {
            nas_fan_output(default_pwm_output);
        }

        nas_safe_close(pwm_fd);
        pwm_fd = -1;
    }
    if (pwm_enable_dev != NULL) {
        if ((default_pwm_enable >= 0) && (default_pwm_enable != 1)) {
            nas_fan_set_enable(default_pwm_enable, 0);
        }
        free(pwm_enable_dev);
    }
}

void nas_fan_init(const char *dev) {
    size_t fan_dev_len = strlen(dev);
    pwm_enable_dev = malloc(fan_dev_len + 8);
    if (pwm_enable_dev == NULL) {
        syslog(LOG_ERR, "allocate memory for PWM_enable device failed: %d",
               errno);
        exit(EXIT_FAILURE);
    }

    memcpy(pwm_enable_dev, dev, fan_dev_len);
    strncpy(pwm_enable_dev + fan_dev_len, "_enable", 8);

    syslog(LOG_INFO, "Fan device: %s, %s", pwm_enable_dev, dev);

    pwm_fd = open(dev, O_RDWR);
    if (pwm_fd < 0) {
        syslog(LOG_ERR, "open Fan PWM device failed: %d", errno);
        exit(EXIT_FAILURE);
    }
    atexit(nas_fan_free);

    char pwm_buf[4];
    memset(pwm_buf, 0, sizeof(pwm_buf));
    if (read(pwm_fd, pwm_buf, 3) < 0) {
        syslog(LOG_ERR, "pwm file read failed: %d", errno);
    }
    for (int i = 0; i < sizeof(pwm_buf); i++) {
        if (pwm_buf[i] < '0' || pwm_buf[i] > '9') {
            pwm_buf[i] = '\0';
            break;
        }
    }
    pwm_last = (int) strtol(pwm_buf, NULL, 10);
    default_pwm_output = (unsigned char) pwm_last;
    syslog(LOG_INFO, "initial pwm output: %d", pwm_last);

    nas_fan_set_enable(1, 1);
}

void nas_fan_update(const int sensor, const int disk) {
    static int pwm_skip_count = 0;
    int pwm = sensor > disk ? sensor : disk;

    if ((abs(pwm_last - pwm) > pwm_update_threshold) ||
        (pwm_skip_count > pwm_skip_max)) {
        nas_fan_output(pwm);
        pwm_last = pwm;
        pwm_skip_count = 0;
    } else {
        pwm_skip_count++;
    }
}
