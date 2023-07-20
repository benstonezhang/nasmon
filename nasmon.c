/*
 * Created by benstone on 2019/9/23.
 */

#include <linux/input.h>
#include <sys/stat.h>
#include <sys/time.h>

#include <fcntl.h>
#include <unistd.h>
#include <syslog.h>
#include <errno.h>
#include <signal.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <getopt.h>

#include "nasmon.h"

#define SYS_BUTTON_POWER    0x74

#define FP_BUTTON_UP    0x67
#define FP_BUTTON_DOWN  0x6C
#define FP_BUTTON_LEFT  0x69
#define FP_BUTTON_RIGHT 0x6A
#define FP_BUTTON_OK    0x160

#define makestr(s)  #s

enum lcd_ctrl_type {
    LCD_INFO_SENSOR,
    LCD_INFO_DISK,
    LCD_INFO_SYSLOAD,
    LCD_INFO_IFS,
    LCD_INFO_CLOCK,
    LCD_INFO_SUMMARY,
    LCD_CPU_FREQ,
    LCD_CTRL_TOTAL,
};

#define LCD_INFO_COUNT  LCD_INFO_SUMMARY

volatile int keep_running = 1;

static const time_t nas_hw_scan_interval = 5;
static const int poweroff_event_timeout = 10;
static const int present_timeout = 30;

static time_t pwr_ts = 0;
static time_t present_ts = 0;
static int pwr_repeats = 0;
static int info_major_index = LCD_INFO_SUMMARY;

static const char *model = NULL;
static const char *power_event_device = NULL;
static const char *button_event_device = NULL;
static const char *nic_list = NULL;
static const char *sensors_conf = NULL;
static const char *fan_device = NULL;

static time_t sts_last_ts = 0;

static void print_event(const struct input_event *restrict pe) {
    syslog(LOG_DEBUG,
           "Event: time=%ld.%06ld, type=0x%hX, code=0x%hX, value=0x%X\n",
           pe->time.tv_sec, pe->time.tv_usec, pe->type, pe->code, pe->value);
}

static void nas_power_off(void) {
    keep_running = 0;

    lcd_printf(1, model);
    lcd_printf(2, ">>> shutdown <<<");

    if (fork() == 0) {
        const char *shutdown = "/sbin/shutdown";
        setsid();
        nas_close_all_files();
        execl(shutdown, shutdown, "-h", "-P", "now", (char *) NULL);
    }
}

static void nas_power_event(const struct input_event *restrict pe) {
    const static int poweroff_event_count = 3;

#ifndef NDEBUG
    print_event(pe);
#endif

    if (pe->code == SYS_BUTTON_POWER && pe->value != 0) {
        if (pe->time.tv_sec - pwr_ts > poweroff_event_timeout) {
            pwr_ts = pe->time.tv_sec;
            pwr_repeats = 1;
            syslog(LOG_NOTICE, "Power button pressed to request poweroff");
        } else {
            pwr_repeats += 1;
            syslog(LOG_NOTICE, "Power button pressed again");
        }

        if (!lcd_is_on()) {
            lcd_on();
        }
        lcd_printf(1, ">>> PowerOff <<<");
        lcd_printf(2, "Confirm: %d/%d", pwr_repeats, poweroff_event_count);
    }

    present_ts = pe->time.tv_sec;

    if (pwr_repeats >= poweroff_event_count) {
        syslog(LOG_WARNING, "System PowerOff is confirmed");
        nas_power_off();
    }
}

static void nas_show_clock(void) {
    struct timeval tv;
    struct tm tm;

    gettimeofday(&tv, NULL);
    localtime_r(&(tv.tv_sec), &tm);

    lcd_printf(1, model);
    lcd_printf(2, "%04d-%02d-%02d %02d:%02d", tm.tm_year + 1900, tm.tm_mon + 1,
               tm.tm_mday, tm.tm_hour, tm.tm_min);
}

static void show_summary_info(const int off) {
    static int id = 0;

    id = (LCD_INFO_COUNT + id + off) % LCD_INFO_COUNT;
    switch (id) {
        case LCD_INFO_SENSOR:
            nas_sensor_summary_show();
            break;
        case LCD_INFO_DISK:
            nas_disk_summary_show();
            break;
        case LCD_INFO_SYSLOAD:
            nas_sysload_summary_show();
            break;
        case LCD_INFO_IFS:
            nas_ifs_summary_show();
            break;
        case LCD_INFO_CLOCK:
            nas_show_clock();
        default:
            break;
    }
}

static void nas_handle_event(const int page_switch, const int off) {
    switch (info_major_index) {
        case LCD_INFO_SENSOR:
            nas_sensor_item_show(off);
            break;
        case LCD_INFO_DISK:
            nas_disk_item_show(off);
            break;
        case LCD_INFO_SYSLOAD:
            nas_sysload_item_show(off);
            break;
        case LCD_INFO_IFS:
            nas_ifs_item_show(off);
            break;
        case LCD_INFO_SUMMARY:
            show_summary_info(off);
            break;
        case LCD_CPU_FREQ:
            cpu_freq_select(page_switch, off);
            break;
        default:
            nas_show_clock();
            break;
    }
}

static void nas_front_panel_event(const struct input_event *restrict pe) {
#ifndef NDEBUG
    print_event(pe);
#endif

    if (pe->value == 0) {
        /* ignore release event */
        return;
    }

    if (pe->code == FP_BUTTON_OK) {
        if (lcd_is_on() && (info_major_index != LCD_CPU_FREQ)) {
            lcd_off();
            return;
        } else {
            lcd_on();
        }
    }

    int page_switch = 0;
    int off = 0;
    switch (pe->code) {
        case FP_BUTTON_UP:
            info_major_index = (info_major_index + LCD_CTRL_TOTAL -
                                (unsigned char) 1) % LCD_CTRL_TOTAL;
            page_switch = 1;
            break;
        case FP_BUTTON_DOWN:
            info_major_index = (info_major_index + (unsigned char) 1) %
                               LCD_CTRL_TOTAL;
            page_switch = -1;
            break;
        case FP_BUTTON_LEFT:
            off = -1;
            break;
        case FP_BUTTON_RIGHT:
            off = 1;
            break;
        case FP_BUTTON_OK:
            break;
        default:
            syslog(LOG_WARNING, "unknown button event received");
            print_event(pe);
            break;
    }

    if (lcd_is_on()) {
        nas_handle_event(page_switch, off);
        present_ts = pe->time.tv_sec;
    }
}

static void usage(const char *restrict name) {
    printf("Usage: %s options...\n"
           "\t--usage\t\tprint help\n"
           "\t--nodaemon\trun in background\n"
           "\t--port=PORT\tTCP port to listen for nas status request\n"
           "\t--model=MODEL\tmodel of the NAS\n"
           "\t--power=DEV\tpower event device (/dev/input/event?)\n"
           "\t--buttons=DEV\tfront board buttons event device (/dev/input/event?)\n"
           "\t--sensors=FILE\tsensors config file>\n"
           "\t--fan=DEV\tsystem fan device\n"
           "\t--nics=NIC1,...,NICn\tnetwork interfaces (comma separated names)\n"
           "\t--temp_cpu_notice=TEMP\tfan bump temperature(C) for CPU (default: %.0f)\n"
           "\t--temp_cpu_high=TEMP\thalt temperature(C) for CPU (default: %.0f)\n"
           "\t--temp_sys_notice=TEMP\tfan bump temperature(C) for mother board (default: %.0f)\n"
           "\t--temp_hdd_notice=TEMP\tfan bump temperature(C) for hard disk (default: %d)\n"
           "\t--temp_hdd_high=TEMP\thalt temperature(C) for hard disk (default: %d)\n"
           "\t--temp_ssd_notice=TEMP\tfan bump temperature(C) for SSD (default: %d)\n"
           "\t--temp_ssd_high=TEMP\thalt temperature(C) for SSD (default: %d)\n",
           name, cpu_temp_notice, cpu_temp_halt, sys_temp_notice,
           hdd_temp_notice, hdd_temp_halt, ssd_temp_notice, ssd_temp_halt);
    exit(EXIT_FAILURE);
}

static void signal_handler(int sig_num) {
    if (sig_num == SIGTERM) {
        keep_running = 0;
    } else if (sig_num == SIGHUP) {
        /* do nothing */
    }
}

/*
 * model: head -n1 /proc/readynas/model
 * pwr_event: grep '^P: Phys' /proc/bus/input/devices | nl -pv 0 |
 *            grep LNXPWRBN | awk '{ print $1 }'
 * fb_event: grep '^N: Name=' /proc/bus/input/devices | nl -pv 0 |
 *           grep fb_button | awk '{ print $1 }'
 * nics: `cd /proc/sys/net/ipv4/conf && ls -d -m en* | sed 's/ //g'`
 * sensors config: /etc/sensors.d/RN626x.conf
 * fan: /sys/class/hwmon/hwmon1/pwm1
 */
int main(const int argc, char *const argv[]) {
    const char *prog_name = nas_get_filename(argv[0]);
    short listen_port = -1;
    int daemon = 1;

    while (1) {
        static struct option long_options[] = {
                {"usage",            no_argument,       0, '?'},
                {"nodaemon",         no_argument,       0, 'D'},
                {"port",             required_argument, 0, 'o'},
                {"model",            required_argument, 0, 'm'},
                {"power",            required_argument, 0, 'p'},
                {"button",           required_argument, 0, 'b'},
                {"sensors",          required_argument, 0, 's'},
                {"fan",              required_argument, 0, 'f'},
                {"nics",             required_argument, 0, 'n'},
                {"temp_cpu_notice",  required_argument, 0, 'c'},
                {"temp_cpu_high",    required_argument, 0, 'd'},
                {"temp_sys_notice",  required_argument, 0, 'e'},
                {"temp_hdd_notice",  required_argument, 0, 'g'},
                {"temp_hdd_high",    required_argument, 0, 'h'},
                {"temp_ssd_notice",  required_argument, 0, 'G'},
                {"temp_ssd_high",    required_argument, 0, 'H'},
                {0,                  0,                 0, 0}
        };
        /* getopt_long stores the option index here. */
        int option_index = 0;

        int c = getopt_long(argc, argv, "?m:p:b:s:f:n:c:d:e:g:h:",
                            long_options, &option_index);

        /* Detect the end of the options. */
        if (c == -1) {
            break;
        }

        switch (c) {
            case 'D':
                daemon = 0;
                break;
            case 'o':
                listen_port = strtol(optarg, NULL, 10);
                break;
            case 'm':
                model = optarg;
                break;
            case 'p':
                power_event_device = optarg;
                break;
            case 'b':
                button_event_device = optarg;
                break;
            case 's':
                sensors_conf = optarg;
                break;
            case 'f':
                fan_device = optarg;
                break;
            case 'n':
                nic_list = optarg;
                break;
            case 'c':
                cpu_temp_notice = strtol(optarg, NULL, 10);
                break;
            case 'd':
                cpu_temp_halt = strtol(optarg, NULL, 10);
                break;
            case 'e':
                sys_temp_notice = strtol(optarg, NULL, 10);
                break;
            case 'g':
                hdd_temp_notice = strtol(optarg, NULL, 10);
                break;
            case 'h':
                hdd_temp_halt = strtol(optarg, NULL, 10);
                break;
            case 'G':
                ssd_temp_notice = strtol(optarg, NULL, 10);
                break;
            case 'H':
                ssd_temp_halt = strtol(optarg, NULL, 10);
                break;
            case '?':
            default:
                usage(prog_name);
                break;
        }
    }

    if ((model == NULL) ||
        (power_event_device == NULL) ||
        (button_event_device == NULL) ||
        (nic_list == NULL) ||
        (sensors_conf == NULL) ||
        (fan_device == NULL)) {
        usage(prog_name);
    }

    /* parse list of interfaces */
    nas_ifs_parse(nic_list);

    /* get the model name for later usage */
    //nas_get_model();

    /* Change the current working directory */
    if ((chdir("/tmp/")) < 0) {
        perror("change the current working directory failed");
        exit(EXIT_FAILURE);
    }

    /* Change File Mask */
    umask(022);

    int pwr_fd, fb_fd, sts_fd;
    pid_t pid, sid;

    if (daemon) {
        /* Fork off the parent process */
        if ((pid = fork()) < 0) {
            exit(EXIT_FAILURE);
        }

        /* If we got a good PID, then we can exit the parent process. */
        if (pid > 0) {
            // create pid file
            nas_create_pid_file(prog_name, pid);
            exit(EXIT_SUCCESS);
        }

        /* Create a new SID for the child process */
        if ((sid = setsid()) < 0) {
            perror("setsid failed");
            exit(EXIT_FAILURE);
        }

        /* Close all open file descriptors */
        nas_close_all_files();
    }

    /* Open the log file */
    //setlogmask(LOG_UPTO(LOG_NOTICE));
    openlog(prog_name, LOG_PID | LOG_CONS | LOG_NDELAY, LOG_DAEMON);
    syslog(LOG_INFO, "launch to handle readynas special hardware events");

    if ((pwr_fd = open(power_event_device, O_RDONLY)) < 0) {
        syslog(LOG_ERR, "Open power button event device failed: %d", errno);
        exit(EXIT_FAILURE);
    }

    if ((fb_fd = open(button_event_device, O_RDONLY)) < 0) {
        syslog(LOG_ERR, "Open front panel event device failed: %d", errno);
    }

    nas_sysload_update();
    nas_sensor_init(sensors_conf);
    nas_ifs_init();
    nas_fan_init(fan_device);
    nas_disk_init();
    cpu_freq_init();
    sts_fd = nas_stssrv_init(listen_port);

    syslog(LOG_INFO, "start hardware monitor");
    lcd_on();
    lcd_clear();
    lcd_printf(1, model);
    lcd_printf(2, "Gentoo GNU/Linux");

    signal(SIGTERM, signal_handler);
    signal(SIGHUP, signal_handler);

    struct input_event e;
    fd_set rfds;
    struct timeval tv;
    int ready_fds;

    while (keep_running != 0) {
        gettimeofday(&tv, NULL);

        FD_ZERO(&rfds);
        FD_SET(pwr_fd, &rfds);
        if (fb_fd >= 0)
            FD_SET(fb_fd, &rfds);
        if (tv.tv_sec - sts_last_ts >= nas_hw_scan_interval) {
            FD_SET(sts_fd, &rfds);
        }

        tv.tv_sec = (nas_hw_scan_interval - 1) -
                    (tv.tv_sec % nas_hw_scan_interval);
        tv.tv_usec = 1000000 - tv.tv_usec;
        ready_fds = select(sts_fd + 1, &rfds, NULL, NULL, &tv);

        if (ready_fds == 0) {
            gettimeofday(&tv, NULL);

            if ((nas_sensor_update(tv.tv_sec) != 0) ||
                (nas_disk_update(tv.tv_sec) != 0)) {
                nas_power_off();
                break;
            }

            nas_fan_update(nas_sensor_get_pwm(), nas_disk_get_pwm());

            if (lcd_is_on()) {
                if ((pwr_repeats != 0) &&
                    (tv.tv_sec - pwr_ts > poweroff_event_timeout)) {
                    pwr_repeats = 0;
                }
                if ((pwr_repeats == 0) &&
                    (tv.tv_sec - present_ts > present_timeout)) {
                    lcd_off();
                    info_major_index = LCD_INFO_SUMMARY;
                }
            }
        } else if (ready_fds > 0) {
            memset(&e, 0, sizeof(e));

            if (fb_fd >= 0 && FD_ISSET(fb_fd, &rfds)) {
                if (read(fb_fd, &e, sizeof(e)) < 0) {
                    syslog(LOG_ERR, "read front board button failed");
                    break;
                }
                nas_front_panel_event(&e);
            } else if (FD_ISSET(pwr_fd, &rfds)) {
                if (read(pwr_fd, &e, sizeof(e)) < 0) {
                    syslog(LOG_ERR, "read power button failed");
                    break;
                }
                nas_power_event(&e);
            } else if (FD_ISSET(sts_fd, &rfds)) {
                gettimeofday(&tv, NULL);
                sts_last_ts = tv.tv_sec;
                nas_stssrv_export();
            }
        } else if (errno != EINTR) {
            syslog(LOG_ERR, "select on file descriptors failed");
            break;
        }
    }

    syslog(LOG_INFO, "cleanup and exit");
    lcd_close();

    nas_safe_close(pwr_fd);
    if (fb_fd >= 0)
        nas_safe_close(fb_fd);

    closelog();

    return EXIT_SUCCESS;
}
