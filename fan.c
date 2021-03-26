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

#define TEMP_BUF_LEN 6

static const int pwm_update_threshold = 2;
static const int pwm_skip_max = 180 / NAS_HW_SCAN_INTERVAL;

static char *pwm_enable_dev = NULL;
static char default_pwm_enable = -1;
static unsigned char default_pwm_output = 0;
static int pwm_fd = -1;
static int pwm_last = 0;
static double temp_min = 40.0;
static double temp_max = 70.0;
static double temp_buf[TEMP_BUF_LEN];

double nas_fan_get_temp_min(void) {
    return temp_min;
}

double nas_fan_get_temp_max(void) {
    return temp_max;
}

void nas_fan_set_temp_min(double t) {
    temp_min = t;
}

void nas_fan_set_temp_max(double t) {
    temp_max = t;
}

static void nas_fan_set_init_value(double t) {
    for (int i = 0; i < TEMP_BUF_LEN; i++) {
        temp_buf[i] = t;
    }
}

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
    nas_fan_set_init_value(-1.0);
}

void nas_fan_update(double t) {
    double wt;
    int pwm;
    static int pwm_skip_count = 0;

    /* update temperature buffer */
    if (temp_buf[0] < 0.0) {
        nas_fan_set_init_value(t);
    } else {
        int i = 0, j = 1;
        while (j < TEMP_BUF_LEN) {
            temp_buf[i] = temp_buf[j];
            i = j;
            j++;
        }
        temp_buf[i] = t;
    }

    /* calculate weighted temperature */
    wt = 0.05 * temp_buf[0] + 0.075 * temp_buf[1] + 0.1125 * temp_buf[2] +
         0.1688 * temp_buf[3] + 0.2532 * temp_buf[4] + 0.3405 * t;

    /* if temperature is decreasing, no need to speed fan */
    if (wt > t) {
        wt = t;
    }

    pwm = (int) (255.0 * (wt - temp_min) / (temp_max - temp_min));
    if (pwm < 0) {
        pwm = 0;
    } else if (pwm > 255) {
        pwm = 255;
    }

#ifndef NDEBUG
    syslog(LOG_DEBUG, "temp: %.2f, pwm output %d", wt, pwm);
#endif

    if ((abs(pwm_last - pwm) > pwm_update_threshold) ||
        (pwm_skip_count > pwm_skip_max)) {
        nas_fan_output(pwm);
        pwm_last = pwm;
        pwm_skip_count = 0;
    } else {
        pwm_skip_count++;
    }
}
