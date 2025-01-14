/*
 * Created by benstone on 4/4/20.
 */

#include <sys/socket.h>
#include <netinet/in.h>
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>
#include <syslog.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>

#include "nasmon.h"

static int fd = -1;
static int http_hdr_len;
static char *http_hdr = "HTTP/1.1 200\r\n"
                        "Connection: Keep-Alive\r\n"
                        "Keep-Alive: timeout=60, max=180\r\n"
                        "Content-Type: application/json\r\n"
                        "Content-Length: ";
static char send_buf[2048];

void nas_stssrv_free(void) {
    if (fd >= 0) {
        nas_safe_close(fd);
        fd = -1;
    }
}

int nas_stssrv_init(const short port) {
    http_hdr_len = strlen(http_hdr);

    if ((fd = socket(AF_INET, SOCK_STREAM, IPPROTO_IP)) == -1) {
        syslog(LOG_WARNING, "failed create TCP server socket");
        nas_log_error();
        exit(EXIT_FAILURE);
    }

    atexit(nas_stssrv_free);

    int optval = 1;
    if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval)) < 0) {
        nas_log_error();
        exit(EXIT_FAILURE);
    }

    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    server_addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

    syslog(LOG_INFO, "start status server at port %d\n", port);

    if (bind(fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) != 0) {
        syslog(LOG_ERR, "failed bind address and port");
        nas_log_error();
        exit(EXIT_FAILURE);
    }

    if (listen(fd, 8) != 0) {
        syslog(LOG_ERR, "failed listen the socket");
        nas_log_error();
        exit(EXIT_FAILURE);
    }

    syslog(LOG_INFO, "status server start success");
    return fd;
}

int nas_stssrv_to_json(char *buf, const size_t len) {
    int count = 0;
    const char *const sysload_hdr = "{\"Sysload\":{";
    strncpy(buf + count, sysload_hdr, len - count);
    count += strlen(sysload_hdr);
    count += nas_sysload_to_json(buf + count, len - count);
    const char *const sensor_hdr = "},\"Sensors\":{";
    strncpy(buf + count, sensor_hdr, len - count);
    count += strlen(sensor_hdr);
    count += nas_sensor_to_json(buf + count, len - count);
    const char *const disk_hdr = "},\"Disks\":{";
    strncpy(buf + count, disk_hdr, len - count);
    count += strlen(disk_hdr);
    count += nas_disk_to_json(buf + count, len - count);
    const char *const ifs_hdr = "},\"NICs\":{";
    strncpy(buf + count, ifs_hdr, len - count);
    count += strlen(ifs_hdr);
    count += nas_ifs_to_json(buf + count, len - count);
    strncpy(buf + count, "}}", len - count);
    count += 2;
    assert(count < len);
    return count;
}

int nas_sts_skt_read(int fd, char *buf, int count) {
    int rc, off = 0, retry = 0;
    while (off < count) {
        rc = read(fd, buf + off, count - off);
        if (rc <= 0) {
            if (errno == EINTR)
                continue;
            if ((errno == EAGAIN) || (errno == EWOULDBLOCK)) {
                retry++;
                if (retry > 3)
                    return rc;
                struct timespec ts;
                ts.tv_sec = 0;
                ts.tv_nsec = 1000000;
                nanosleep(&ts, NULL);
                continue;
            }
            return rc;
        }
        off += rc;
    }
    return count;

}

int nas_sts_skt_write(int fd, char *buf, int count) {
    int rc, off = 0, retry = 0;
    while (off < count) {
        rc = write(fd, buf + off, count - off);
        if (rc <= 0) {
            if (errno == EINTR)
                continue;
            if ((errno == EAGAIN) || (errno == EWOULDBLOCK)) {
                retry++;
                if (retry > 3)
                    return rc;
                struct timespec ts;
                ts.tv_sec = 0;
                ts.tv_nsec = 1000000;
                nanosleep(&ts, NULL);
                continue;
            }
            return rc;
        }
        off += rc;
    }
    return count;
}

void nas_stssrv_export(void) {
    struct sockaddr_in accept_addr;
    unsigned int accept_addr_len = sizeof(accept_addr);
    int http = 0;
    int offset = 0;
    char buf[1920];

    int client_fd = accept(fd, (struct sockaddr *)&accept_addr, &accept_addr_len);
    if (client_fd < 0) {
        syslog(LOG_WARNING, "failed accept client connection");
        nas_log_error();
        return;
    }

    /* Set socket to be nonblocking. */
    int optval = fcntl(client_fd, F_GETFL, 0);
    if (fcntl(client_fd, F_SETFL, optval | O_NONBLOCK) < 0) {
        nas_log_error();
        return;
    }

    if ((nas_sts_skt_read(client_fd, buf, 4) == 4) &&
        (strncmp(buf, "GET ", 4) == 0)) {
        http = 1;
    }

    int count = nas_stssrv_to_json(buf, sizeof(buf));
    if (http) {
        strncpy(send_buf, http_hdr, http_hdr_len);
        offset = http_hdr_len;
        offset += snprintf(send_buf + offset, sizeof(send_buf) - offset, "%d\r\n\r\n", count);
    }
    strncpy(send_buf + offset, buf, sizeof(send_buf) - offset);
    offset += count;

    if (nas_sts_skt_write(client_fd, send_buf, offset) != offset) {
        syslog(LOG_WARNING, "failed write status to socket");
    }

    nas_safe_close(client_fd);
}
