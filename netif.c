/*
 * Created by benstone on 2019/10/6.
 */

#include <sys/ioctl.h>
#include <arpa/inet.h>
#include <net/if.h>
#include <netinet/in.h>
#include <string.h>
#include <syslog.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>

#include "nasfp.h"

static int ifs_count;
static const char **ifs_list = NULL;
static int inet_sock = -1;

void nas_ifs_parse(const char *ifs) {
    const char *p1, *p2;

    /* get number of interfaces */
    ifs_count = 0;
    p1 = ifs;
    while ((p1 = strchr(p1, ',')) != NULL) {
        ifs_count++;
        p1++;
    }
    ifs_count++;

    if ((ifs_list = (const char **) calloc(sizeof(const char *),
                                           (size_t) ifs_count)) == NULL) {
        perror("malloc for interface names");
        exit(EXIT_FAILURE);
    }

    /* parse list to array */
    ifs_count = 0;
    p1 = ifs;
    while ((p2 = strchr(p1, ',')) != NULL) {
        ifs_list[ifs_count++] = strndup(p1, p2 - p1);
        p1 = p2 + 1;
    }
    ifs_list[ifs_count++] = strdup(p1);
}

void nas_ifs_free(void) {
    if (ifs_list != NULL) {
        for (int i = 0; i < ifs_count; i++) {
            if (ifs_list[i] != NULL) {
                free((void *) ifs_list[i]);
            }
        }
        free(ifs_list);
    }
    if (inet_sock > 0) {
        close(inet_sock);
    }
}

void nas_ifs_init(void) {
    inet_sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (inet_sock < 0) {
        syslog(LOG_ERR, "open AF_INET socket failed");
    }

    atexit(nas_ifs_free);
}

static void nas_ifs_show_addr(const char *ifname, const int line) {
    struct ifreq ifr;
    char ip_buf[INET_ADDRSTRLEN];

    memset(&ifr, 0, sizeof(ifr));
    ifr.ifr_addr.sa_family = AF_INET;
    strncpy(ifr.ifr_name, ifname, IFNAMSIZ - 1);

    ioctl(inet_sock, SIOCGIFADDR, &ifr);
    inet_ntop(AF_INET, &(((struct sockaddr_in *) &ifr.ifr_addr)->sin_addr),
              ip_buf, (socklen_t) sizeof(ip_buf));
    lcd_printf(line, ip_buf);
}

int nas_ifs_item_show(const int off) {
    static int id = -1;

    id = id >= 0 ? (ifs_count + id + off) % ifs_count : 0;

    lcd_printf(1, "%s:", ifs_list[id]);
    nas_ifs_show_addr(ifs_list[id], 2);

    return id;
}

void nas_ifs_summary_show(void) {
    nas_ifs_show_addr(ifs_list[0], 1);
    nas_ifs_show_addr(ifs_list[1], 2);
}
