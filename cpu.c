/*
 * Created by benstone on 2021/4/12.
 */

#include <unistd.h>
#include <syslog.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#include "nasmon.h"

static int cpu_core_count = 1;
static int64_t cpu_freq[NAS_CPU_FREQ_COUNT] = {0};
static int spec = NAS_CPU_FREQ_MIN;

void cpu_freq_init(void) {
    char buf[16];

    if (nas_read_file("/sys/devices/system/cpu/kernel_max", buf, sizeof(buf)) < 0) {
        perror("read cores of CPU failed");
        exit(EXIT_FAILURE);
    }
    cpu_core_count = strtol(buf, NULL, 10) + 1;

    if (nas_read_file("/sys/devices/system/cpu/cpufreq/policy0/cpuinfo_min_freq", buf, sizeof(buf)) < 0) {
        perror("read CPU minimum freq failed");
        exit(EXIT_FAILURE);
    }
    cpu_freq[NAS_CPU_FREQ_MIN] = strtol(buf, NULL, 10);

    if (nas_read_file("/sys/devices/system/cpu/cpufreq/policy0/cpuinfo_max_freq", buf, sizeof(buf)) < 0) {
        perror("read CPU maximum freq failed");
        exit(EXIT_FAILURE);
    }
    cpu_freq[NAS_CPU_FREQ_MAX] = strtol(buf, NULL, 10);

    if (cpu_freq[NAS_CPU_FREQ_MAX] > cpu_freq[NAS_CPU_FREQ_MIN] * 3) {
        cpu_freq[NAS_CPU_FREQ_LOW] = cpu_freq[NAS_CPU_FREQ_MIN] * 2;
        cpu_freq[NAS_CPU_FREQ_HIGH] = cpu_freq[NAS_CPU_FREQ_MIN] * 3;
    } else if (cpu_freq[NAS_CPU_FREQ_MAX] > cpu_freq[NAS_CPU_FREQ_MIN] * 2) {
        cpu_freq[NAS_CPU_FREQ_LOW] = cpu_freq[NAS_CPU_FREQ_MIN] * 3 / 2;
        cpu_freq[NAS_CPU_FREQ_HIGH] = cpu_freq[NAS_CPU_FREQ_MIN] * 2;
    } else {
        cpu_freq[NAS_CPU_FREQ_LOW] = cpu_freq[NAS_CPU_FREQ_MIN] +
                (cpu_freq[NAS_CPU_FREQ_MAX] - cpu_freq[NAS_CPU_FREQ_MIN]) * 2 / 3;
        cpu_freq[NAS_CPU_FREQ_HIGH] = cpu_freq[NAS_CPU_FREQ_MIN] +
                (cpu_freq[NAS_CPU_FREQ_MAX] - cpu_freq[NAS_CPU_FREQ_MIN]) / 3;
    }

    syslog(LOG_INFO, "Total %d CPUs, Freq %d/%d/%d/%d MHz",
           cpu_core_count,
           (int) (cpu_freq[NAS_CPU_FREQ_MIN] / 1000),
           (int) (cpu_freq[NAS_CPU_FREQ_LOW] / 1000),
           (int) (cpu_freq[NAS_CPU_FREQ_HIGH] / 1000),
           (int) (cpu_freq[NAS_CPU_FREQ_MAX] / 1000));
}

int cpu_freq_select(const int page_switch, const int off) {
    int err = 0;
    int64_t freq_in_khz;
    int freq_in_mhz;

    spec = (NAS_CPU_FREQ_COUNT + spec + off) % NAS_CPU_FREQ_COUNT;
    freq_in_khz = cpu_freq[spec];
    freq_in_mhz = (int)(freq_in_khz / 1000);
    lcd_printf(1, "CPU Freq");
    lcd_printf(2, "%d MHz", freq_in_mhz);

    if ((page_switch == 0)  && (off == 0)) {
        /* OK button pressed */
        char freq_str[16];
        char name[80];
        syslog(LOG_INFO, "Max CPU Freq set to %dMHz", freq_in_mhz);
        sprintf(freq_str, "%ld", freq_in_khz);
        for (int i=0; i<cpu_core_count; i++) {
            sprintf(name, "/sys/devices/system/cpu/cpu%d/cpufreq/scaling_max_freq", i);
            if (nas_write_file(name, freq_str, strlen(freq_str)) < 0) {
                err = -1;
            }
        }
    }
    return err;
}
