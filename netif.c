/*
 * Created by benstone on 2019/10/6.
 */

#include <sys/ioctl.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <net/if.h>
#include <ifaddrs.h>
#include <string.h>
#include <syslog.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <assert.h>

#include "nasmon.h"

static int ifs_count;
static const char **ifs_list = NULL;
static int inet_sock = -1;

void nas_ifs_parse(const char *ifs) {
    const char *p1, *p2;

    /* get number of interfaces */
    ifs_count = 0;
    p1 = ifs;
    while ((p1 = strchr(p1, ',')) != NULL) {
        p1++;
        if (*p1 == '\0') {
            break;
        }
        ifs_count++;
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
    if (*p1 != '\0') {
        ifs_list[ifs_count++] = strdup(p1);
    }
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
    if (inet_sock >= 0) {
        nas_safe_close(inet_sock);
    }
}

void nas_ifs_init(void) {
    inet_sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (inet_sock < 0) {
        syslog(LOG_ERR, "open AF_INET socket failed");
    }

    atexit(nas_ifs_free);
}

static int nas_ifs_get_ipv4(const char *ifname, char *buf, const size_t len) {
    assert(len > INET_ADDRSTRLEN);

    struct ifreq ifr;
    memset(&ifr, 0, sizeof(ifr));
    ifr.ifr_addr.sa_family = AF_INET;
    strncpy(ifr.ifr_name, ifname, IFNAMSIZ - 1);

    ioctl(inet_sock, SIOCGIFADDR, &ifr);
    struct in_addr *sin = &(((struct sockaddr_in *) &ifr.ifr_addr)->sin_addr);
    if (sin->s_addr) {
        inet_ntop(AF_INET, sin, buf, len);
    } else {
        buf[0] = '\0';
    }
    return strlen(buf);
}

static int nas_ifs_get_ipv6(const char *ifname, char *buf, const size_t len,
                            const struct ifaddrs *ifa) {
    assert(len > INET6_ADDRSTRLEN);

    int count = 0;
    const char *const sep = "\",\"";
    buf[0] = '\0';
    while (ifa != NULL) {
        if ((ifa->ifa_addr != NULL) &&
            (ifa->ifa_addr->sa_family == AF_INET6) &&
            (strcmp(ifa->ifa_name, ifname) == 0)) {
            if (count != 0) {
                strncpy(buf + count, sep, len - count);
                count += strlen(sep);
            }
            inet_ntop(AF_INET6,
                      &(((struct sockaddr_in6 *) (ifa->ifa_addr))->sin6_addr),
                      buf + count, len - count);
            count += strlen(buf + count);
        }
        ifa = ifa->ifa_next;
    }
    return count;
}

static void nas_ifs_show_ipv4(const char *ifname, const int line) {
    char ip_buf[INET_ADDRSTRLEN];
    nas_ifs_get_ipv4(ifname, ip_buf, sizeof(ip_buf));
    lcd_printf(line, ip_buf);
}

int nas_ifs_item_show(const int off) {
    static int id = -1;

    id = id >= 0 ? (ifs_count + id + off) % ifs_count : 0;

    lcd_printf(1, "%s:", ifs_list[id]);
    nas_ifs_show_ipv4(ifs_list[id], 2);

    return id;
}

void nas_ifs_summary_show(void) {
    nas_ifs_show_ipv4(ifs_list[0], 1);
    nas_ifs_show_ipv4(ifs_list[1], 2);
}

int nas_ifs_to_json(char *buf, const size_t len) {
    int count = 0;
    struct ifaddrs *ifa = NULL;
    if (getifaddrs((&ifa)) == 0) {
        for (int i = 0; i < ifs_count; i++) {
            if (i != 0) {
                buf[count++] = ',';
            }
            buf[count++] = '"';
            const char *ifname = ifs_list[i];
            strncpy(buf + count, ifname, len - count);
            count += strlen(ifs_list[i]);
            const char *const ipv4_hdr = "\":{\"ipv4\":\"";
            strncpy(buf + count, ipv4_hdr, len - count);
            count += strlen(ipv4_hdr);
            count += nas_ifs_get_ipv4(ifname, buf + count, len - count);
            const char *const ipv6_hdr = "\",\"ipv6\":[\"";
            strncpy(buf + count, ipv6_hdr, len - count);
            count += strlen(ipv6_hdr);
            count += nas_ifs_get_ipv6(ifname, buf + count, len - count, ifa);
            const char *const tail = "\"]}";
            strncpy(buf + count, tail, len - count);
            count += strlen(tail);
        }

        freeifaddrs(ifa);
    } else {
        syslog(LOG_WARNING, "can not list network interfaces");
    }
    return count;
}
