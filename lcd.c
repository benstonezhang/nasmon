/*
 * Created by benstone on 2019/10/6.
 */

#include <unistd.h>
#include <syslog.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>

#include "nasfp.h"

#define LCD_LINE_CHARS  16

static int lcd_fd;
static char lcd_buf[40];

void lcd_open(void) {
    const static char *lcd_proc_file = "/proc/LCD";

    lcd_fd = open(lcd_proc_file, O_WRONLY);
    if (lcd_fd < 0) {
        syslog(LOG_ERR, "Open LCD proc file failed: %d", errno);
        exit(EXIT_FAILURE);
    }
}

static void lcd_cmd(const int cmd) {
    lcd_buf[0] = (char) ('0' + cmd);
    lcd_buf[1] = '\0';
    if (write(lcd_fd, lcd_buf, 2) != 2) {
        syslog(LOG_ERR, "LCD proc file write failed");
    }
}

void lcd_clear(void) {
    lcd_cmd(0);
}

void lcd_on(void) {
    lcd_cmd(3);
}

void lcd_off(void) {
    lcd_cmd(4);
}

void lcd_printf(const int line, const char *restrict fmt, ...) {
    size_t len = 0;

    lcd_buf[len++] = (char) ('0' + line);
    lcd_buf[len++] = ' ';
    lcd_buf[len++] = '"';

    va_list args;
    va_start(args, fmt);
    vsnprintf(lcd_buf + len, LCD_LINE_CHARS + 1, fmt, args);
    va_end(args);

    len = strlen(lcd_buf);
    /* prefix is 3 bytes, the text is 16 bytes */
    while (len < LCD_LINE_CHARS + 3) {
        lcd_buf[len++] = ' ';
    }
    lcd_buf[len++] = '"';
    lcd_buf[len++] = '\0';
#ifndef NDEBUG
    syslog(LOG_DEBUG, "LCD: %s", lcd_buf);
#endif

    if (write(lcd_fd, lcd_buf, len) != len) {
        syslog(LOG_ERR, "LCD proc file write failed");
    }
}

void lcd_close(void) {
    close(lcd_fd);
}
