/*
 * Created by benstone on 2019/10/6.
 */

#ifndef NAS_FRONT_PANEL_H
#define NAS_FRONT_PANEL_H

#include <time.h>

#define TEMP_BUF_LEN 6

#ifdef NAS_DEBUG
#undef    LOG_EMERG
#undef    LOG_ALERT
#undef    LOG_CRIT
#undef    LOG_ERR
#undef    LOG_WARNING
#undef    LOG_NOTICE
#undef    LOG_INFO
#undef    LOG_DEBUG

#define    LOG_EMERG    stderr
#define    LOG_ALERT    stderr
#define    LOG_CRIT     stderr
#define    LOG_ERR      stderr
#define    LOG_WARNING  stderr
#define    LOG_NOTICE   stderr
#define    LOG_INFO     stderr
#define    LOG_DEBUG    stderr
#define syslog  fprintf
#endif

typedef enum {
    NAS_CPU_FREQ_MIN,
    NAS_CPU_FREQ_LOW,
    NAS_CPU_FREQ_HIGH,
    NAS_CPU_FREQ_MAX,
    NAS_CPU_FREQ_COUNT
} nas_cpu_freq_spec;

const char *nas_get_model(void);
const char *nas_get_filename(const char *path);
void nas_create_pid_file(const char *name, pid_t pid);
void nas_safe_close(int fd);
void nas_close_all_files(void);
void nas_log_error(void);
int nas_read_file(const char *name, char *buf, int count);
int nas_write_file(const char *name, const char *buf, int count);

/* LCD */
void lcd_open(void);
void lcd_clear(void);
void lcd_on(void);
void lcd_off(void);
void lcd_printf(int line, const char *restrict fmt, ...);
void lcd_close(void);
int lcd_is_on(void);

/* fan */
void nas_fan_init(const char *dev);
void nas_fan_update(int sensor, int disk);

/* sensor */
void nas_sensor_init(const char *conf);
int nas_sensor_update(time_t now);
int nas_sensor_item_show(int off);
void nas_sensor_summary_show(void);
int nas_sensor_to_json(char *buf, size_t len);
double nas_sensor_get_cpu_temp_min(void);
double nas_sensor_get_cpu_temp_max(void);
void nas_sensor_set_cpu_temp_min(double t);
void nas_sensor_set_cpu_temp_max(double t);
int nas_sensor_get_pwm(void);

/* S.M.A.R.T */
void nas_disk_init(void);
int nas_disk_update(time_t now);
int nas_disk_item_show(int off);
void nas_disk_summary_show(void);
int nas_disk_to_json(char *buf, size_t len);
int nas_disk_get_pwm(void);

/* system load and memory usage */
void nas_sysload_update(void);
int nas_sysload_item_show(int off);
void nas_sysload_summary_show(void);
int nas_sysload_to_json(char *buf, size_t len);

/* network interfaces */
void nas_ifs_parse(const char *ifs);
void nas_ifs_init(void);
int nas_ifs_item_show(int off);
void nas_ifs_summary_show(void);
int nas_ifs_to_json(char *buf, size_t len);

void cpu_freq_init(void);
int cpu_freq_select(int page_switch, int off);

int nas_stssrv_init(short port);
void nas_stssrv_export(void);
int nas_stssrv_to_json(char *buf, size_t len);

#endif
/* NAS_FRONT_PANEL_H */
