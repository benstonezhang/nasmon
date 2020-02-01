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

#include "nasfp.h"

#define TEMP_BUF_LEN 6

static char *pwm_enable_dev = NULL;
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

static void nas_fan_set_enable(int enable) {
    int pwm_enable_fd = open(pwm_enable_dev, O_RDWR);
    if (pwm_enable_fd < 0) {
        syslog(LOG_ERR, "open pwm_enable device failed: %d", errno);
        exit(EXIT_FAILURE);
    }

    char pwm_enable[3];
    pwm_enable[0] = (char) (enable != 0 ? '1' : '0');
    pwm_enable[1] = '\n';
    pwm_enable[2] = '\0';

    if (write(pwm_enable_fd, pwm_enable, 3) != 3) {
        syslog(LOG_ERR, "write pwm_enable device failed: %d", errno);
    }
    close(pwm_enable_fd);
}

void nas_fan_free(void) {
    if (pwm_fd > 0) {
        close(pwm_fd);
        pwm_fd = -1;
    }
    if (pwm_enable_dev != NULL) {
        nas_fan_set_enable(0);
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
    syslog(LOG_INFO, "initial pwm output: %d", pwm_last);

    nas_fan_set_enable(1);
    nas_fan_set_init_value(-1.0);
}

void nas_fan_update(double t) {
    int pwm = 0;
    char pwm_buf[6];

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
    t = 0.05 * temp_buf[0] + 0.075 * temp_buf[1] + 0.125 * temp_buf[2] +
        0.2 * temp_buf[3] + 0.25 * temp_buf[4] + 0.3 * t;

    pwm = (int) (255.0 * (t - temp_min) / (temp_max - temp_min));
    if (pwm < 0) {
        pwm = 0;
    } else if (pwm > 255) {
        pwm = 255;
    }

#ifndef NDEBUG
    syslog(LOG_DEBUG, "temp: %.2f, pwm output %d", t, pwm);
#endif

    if (pwm_last != pwm) {
        sprintf(pwm_buf, "%d\n", pwm);
        if (write(pwm_fd, pwm_buf, strlen(pwm_buf)) < 0) {
            syslog(LOG_ERR, "pwm output file write failed: %d", errno);
        }
        pwm_last = pwm;
    }
}
