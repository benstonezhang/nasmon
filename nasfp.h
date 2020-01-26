/*
 * Created by benstone on 2019/10/6.
 */

#ifndef NAS_FRONT_PANEL_H
#define NAS_FRONT_PANEL_H

#include <time.h>

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

const char *nas_get_model(void);
const char *nas_get_filename(const char *path);
void nas_create_pid_file(const char *name, pid_t pid);
void nas_close_all_files(void);
void nas_log_error(void);

/* LCD */
void lcd_open(void);
void lcd_clear(void);
void lcd_on(void);
void lcd_off(void);
void lcd_printf(int line, const char *restrict fmt, ...);
void lcd_close(void);
int lcd_is_on(void);

/* sensor */
void nas_sensor_init(const char *conf);
int nas_sensor_update(time_t now);
int nas_sensor_item_show(int off);
int nas_sensor_summary_show(int off);

/* S.M.A.R.T */
void nas_disk_init(void);
int nas_disk_update(time_t now);
int nas_disk_item_show(int off);
int nas_disk_summary_show(int off);

/* system load and memory usage */
int nas_sysload_item_show(int off);
int nas_sysload_summary_show(int off);

/* network interfaces */
void nas_ifs_parse(const char *ifs);
void nas_ifs_init(void);
int nas_ifs_item_show(int off);
int nas_ifs_summary_show(int off);

#endif
/* NAS_FRONT_PANEL_H */
