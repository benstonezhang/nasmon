/*
 * Created by benstone on 2019/10/6.
 */

#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <syslog.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>

#include "nasmon.h"

const char *nas_get_model(void) {
    const static char *model_file = "/proc/readynas/model";
    char model[64];

    int model_fd = open(model_file, O_RDONLY);
    if (model_fd < 0) {
        perror("open NAS model file failed");
        exit(EXIT_FAILURE);
    }
    if (read(model_fd, model, sizeof(model) - 1) < 0) {
        perror("read NAS model failed");
        exit(EXIT_FAILURE);
    }
    nas_safe_close(model_fd);

    model[sizeof(model) - 1] = '\0';
    for (int i = 0; i < sizeof(model); i++) {
        if ((model[i] == '\n') || (model[i] == '\r')) {
            model[i] = '\0';
            break;
        }
    }
#ifndef NDEBUG
    printf("NAS: %s\n", model);
#endif

    return strdup(model);
}

const char *nas_get_filename(const char *path) {
    char *sep = strrchr(path, '/');
    if (sep != NULL) {
        return sep + 1;
    }
    return path;
}

void nas_create_pid_file(const char *name, const pid_t pid) {
    char pid_file[64];
    snprintf(pid_file, sizeof(pid_file), "/run/%s.pid", name);
    int pid_fd = open(pid_file, O_WRONLY | O_CREAT,
                      S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
    if (pid_fd < 0) {
        perror("can not create pid file");
        exit(EXIT_FAILURE);
    }
    dprintf(pid_fd, "%d\n", pid);
    nas_safe_close(pid_fd);
}

void nas_safe_close(const int fd) {
    while (close(fd) == EINTR) {}
}

/* Close all open file descriptors */
void nas_close_all_files(void) {
    for (int x = (int) sysconf(_SC_OPEN_MAX); x >= 0; x--) {
        nas_safe_close(x);
    }
}

void nas_log_error(void) {
    char nas_error_msg[256];
    strerror_r(errno, nas_error_msg, sizeof(nas_error_msg));
    syslog(LOG_ERR, nas_error_msg);
}

int nas_safe_read(const int fd, char *buf, int count) {
    int ret;
    int offset = 0;
    while(1) {
        ret = read(fd, buf + offset, count);
        if ((ret <= 0) && (errno != EINTR)) {
            break;
        }
        offset += ret;
        count -= ret;
    }
    return offset;
}

int nas_read_file(const char *name, char *buf, const int count) {
    int ret = -1;
    int fd = open(name, O_RDONLY);
    if (fd >= 0) {
        ret = nas_safe_read(fd, buf, count);
        nas_safe_close(fd);
    }
    if (ret < 0) {
        nas_log_error();
    }
    return ret;
}

int nas_safe_write(const int fd, const char *buf, int count) {
    int ret;
    int offset = 0;
    while(1) {
        ret = write(fd, buf + offset, count);
        if ((ret <= 0) && (errno != EINTR)) {
            break;
        }
        offset += ret;
        count -= ret;
    }
    return offset;
}

int nas_write_file(const char *name, const char *buf, const int count) {
    int ret = -1;
    int fd = open(name, O_WRONLY);
    if (fd >= 0) {
        ret = nas_safe_write(fd, buf, count);
        nas_safe_close(fd);
    }
    if (ret < 0) {
        nas_log_error();
    }
    return ret;
}
