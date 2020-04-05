/*
 * Created by benstone on 4/4/20.
 */

#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <syslog.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "nasmon.h"

static int fd = -1;

void nas_stssrv_free(void) {
    if (fd > 0) {
        close(fd);
        fd = -1;
    }
}

int nas_stssrv_init(const short port) {
    if ((fd = socket(AF_INET, SOCK_STREAM, IPPROTO_IP)) == -1) {
        syslog(LOG_WARNING, "failed create TCP server socket");
        nas_log_error();
        exit(EXIT_FAILURE);
    }

    atexit(nas_stssrv_free);

    int optval = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval));

    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    server_addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

    syslog(LOG_INFO, "start status server at port %d\n", port);

    if (bind(fd, (struct sockaddr *) &server_addr, sizeof(server_addr)) != 0) {
        syslog(LOG_ERR, "failed bind address and port");
        nas_log_error();
        exit(EXIT_FAILURE);
    }

    if (listen(fd, 2) != 0) {
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
    buf[count++] = '\0';
    assert(count < len);
    return count;
}

void nas_stssrv_export(void) {
    struct sockaddr_in accept_addr;
    unsigned int accept_addr_len = sizeof(accept_addr);

    int client_fd = accept(fd, (struct sockaddr *) &accept_addr, &accept_addr_len);
    if (client_fd < 0) {
        syslog(LOG_WARNING, "failed accept client connection");
        nas_log_error();
        return;
    }

    char buf[2048];
    int count = nas_stssrv_to_json(buf, sizeof(buf));
    if (write(client_fd, buf, count) != count) {
        syslog(LOG_WARNING, "failed write status to socket");
    }

    close(client_fd);
}
