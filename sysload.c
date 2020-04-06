/*
 * Created by benstone on 2019/10/6.
 */

#include <sys/sysinfo.h>
#include <stdlib.h>
#include <stdio.h>

#include "nasmon.h"

static const double linux_loads_scale = 65536.0;
static struct sysinfo info;

static const char *nas_sysload_titles[] = {
    "Load Average:",
    "Processes:",
    "Memory InUse:",
    "Memory Shared:",
    "Memory Buffer:",
    "Swap InUse:"
};
static const char *nas_mem_load_fmt = "%lu/%lu";

void nas_sysload_update(void) {
    sysinfo(&info);
}

int nas_sysload_item_show(const int off) {
    static int id = -1;
    unsigned long mem_in_mb;

    id = id >= 0 ? (6 + id + off) % 6 : 0;

    sysinfo(&info);
    mem_in_mb = 1024 * 1024 / info.mem_unit;

    lcd_printf(1, nas_sysload_titles[id]);

    switch (id) {
        case 0:
            lcd_printf(2, "%.2f %.2f %.2f",
                       info.loads[0] / linux_loads_scale,
                       info.loads[1] / linux_loads_scale,
                       info.loads[2] / linux_loads_scale);

            break;
        case 1:
            lcd_printf(2, "%hu", info.procs);
            break;
        case 2:
            lcd_printf(2, nas_mem_load_fmt,
                       (info.totalram - info.freeram) / mem_in_mb,
                       info.totalram / mem_in_mb);
            break;
        case 3:
            lcd_printf(2, nas_mem_load_fmt,
                       info.sharedram / mem_in_mb,
                       info.totalram / mem_in_mb);
            break;
        case 4:
            lcd_printf(2, nas_mem_load_fmt,
                       info.bufferram / mem_in_mb,
                       info.totalram / mem_in_mb);
            break;
        case 5:
            lcd_printf(2, nas_mem_load_fmt,
                       (info.totalswap - info.freeswap) / mem_in_mb,
                       info.totalswap / mem_in_mb);
            break;
        default:
            break;
    }

    return id;
}

void nas_sysload_summary_show(void) {
    unsigned long mem_in_mb;

    sysinfo(&info);
    mem_in_mb = 1024 * 1024 / info.mem_unit;

    lcd_printf(1, "L: %.1f %.1f %.1f",
               ((double) info.loads[0]) / linux_loads_scale,
               ((double) info.loads[1]) / linux_loads_scale,
               ((double) info.loads[2]) / linux_loads_scale);
    lcd_printf(2, "%hu %lu/%lu",
               info.procs,
               (info.totalram - info.freeram) / mem_in_mb,
               info.totalram / mem_in_mb);
}

int nas_sysload_to_json(char *buf, const size_t len) {
    nas_sysload_update();
    return snprintf(buf, len,
                    "\"uptime\":%ld,\"load\":{\"1m\":%.2f,\"5m\":%.2f,\"15m\""
                    ":%.2f},\"procs\":%hu,\"memory\":{\"total\":%lu,\"free\":"
                    "%lu,\"shared\":%lu,\"buffer\":%lu},\"swap\":{\"total\":%"
                    "lu,\"free\":%lu}",
                    info.uptime, info.loads[0] / linux_loads_scale,
                    info.loads[1] / linux_loads_scale,
                    info.loads[2] / linux_loads_scale,
                    info.procs, info.totalram, info.freeram, info.sharedram,
                    info.bufferram, info.totalswap, info.freeswap);
}