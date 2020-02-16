/*
 * Created by benstone on 2019/10/6.
 */

#include <syslog.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#include <sensors/sensors.h>

#include "nasmon.h"

#define SENSOR_UPDATE_INTERVAL  60

/* sensors */
enum nas_sensors_ids {
    NAS_SENSOR_CPU,
    NAS_SENSOR_System,
    NAS_SENSOR_Fan,
    NAS_SENSOR_Vcore,
    NAS_SENSOR_V1_2,
    NAS_SENSOR_V3_3,
    NAS_SENSOR_V5_0,
    NAS_SENSOR_V12,
    NAS_SENSORS_COUNT
};
#define NAS_SENSOR_MIN NAS_SENSOR_CPU

struct nas_sensors_info {
    const sensors_feature_type feature_type;
    const sensors_subfeature_type subfeature_input;
    const sensors_subfeature_type subfeature_min;
    const sensors_subfeature_type subfeature_max;
    const char *label;
    const sensors_chip_name *chip;
    const sensors_feature *feature;
    int nr;
    double value;
    double min;
    double max;
};

static struct nas_sensors_info nas_sensors[NAS_SENSORS_COUNT] = {
    {
        .feature_type = SENSORS_FEATURE_TEMP,
        .subfeature_input = SENSORS_SUBFEATURE_TEMP_INPUT,
        .subfeature_min = SENSORS_SUBFEATURE_TEMP_MIN,
        .subfeature_max = SENSORS_SUBFEATURE_TEMP_MAX,
        .label = "CPU",
        .chip = NULL,
        .feature = NULL,
        .nr = -1,
    },
    {
        .feature_type = SENSORS_FEATURE_TEMP,
        .subfeature_input = SENSORS_SUBFEATURE_TEMP_INPUT,
        .subfeature_min = SENSORS_SUBFEATURE_TEMP_MIN,
        .subfeature_max = SENSORS_SUBFEATURE_TEMP_MAX,
        .label = "System",
        .chip = NULL,
        .feature = NULL,
        .nr = -1,
    },
    {
        .feature_type = SENSORS_FEATURE_FAN,
        .subfeature_input = SENSORS_SUBFEATURE_FAN_INPUT,
        .subfeature_min = SENSORS_SUBFEATURE_FAN_MIN,
        .subfeature_max = SENSORS_SUBFEATURE_FAN_MAX,
        .label = "Fan",
        .chip = NULL,
        .feature = NULL,
        .nr = -1,
    },
    {
        .feature_type = SENSORS_FEATURE_IN,
        .subfeature_input = SENSORS_SUBFEATURE_IN_INPUT,
        .subfeature_min = SENSORS_SUBFEATURE_IN_MIN,
        .subfeature_max = SENSORS_SUBFEATURE_IN_MAX,
        .label = "Vcore",
        .chip = NULL,
        .feature = NULL,
        .nr = -1,
    },
    {
        .feature_type = SENSORS_FEATURE_IN,
        .subfeature_input = SENSORS_SUBFEATURE_IN_INPUT,
        .subfeature_min = SENSORS_SUBFEATURE_IN_MIN,
        .subfeature_max = SENSORS_SUBFEATURE_IN_MAX,
        .label = "V1_2",
        .chip = NULL,
        .feature = NULL,
        .nr = -1,
    },
    {
        .feature_type = SENSORS_FEATURE_IN,
        .subfeature_input = SENSORS_SUBFEATURE_IN_INPUT,
        .subfeature_min = SENSORS_SUBFEATURE_IN_MIN,
        .subfeature_max = SENSORS_SUBFEATURE_IN_MAX,
        .label = "V3_3",
        .chip = NULL,
        .feature = NULL,
        .nr = -1,
    },
    {
        .feature_type = SENSORS_FEATURE_IN,
        .subfeature_input = SENSORS_SUBFEATURE_IN_INPUT,
        .subfeature_min = SENSORS_SUBFEATURE_IN_MIN,
        .subfeature_max = SENSORS_SUBFEATURE_IN_MAX,
        .label = "V5_0",
        .chip = NULL,
        .feature = NULL,
        .nr = -1,
    },
    {
        .feature_type = SENSORS_FEATURE_IN,
        .subfeature_input = SENSORS_SUBFEATURE_IN_INPUT,
        .subfeature_min = SENSORS_SUBFEATURE_IN_MIN,
        .subfeature_max = SENSORS_SUBFEATURE_IN_MAX,
        .label = "V+12",
        .chip = NULL,
        .feature = NULL,
        .nr = -1,
    },
};

void nas_sensor_free(void) {
    sensors_cleanup();
}

void nas_sensor_init(const char *conf) {
    FILE *fp = fopen(conf, "r");
    if (fp == NULL) {
        syslog(LOG_ERR, "Open sensors config file failed: %d", errno);
        exit(EXIT_FAILURE);
    }

    if (sensors_init(fp) != 0) {
        syslog(LOG_ERR, "Parse sensor config failed");
        fclose(fp);
        exit(EXIT_FAILURE);
    }
    fclose(fp);

    atexit(nas_sensor_free);

    const sensors_chip_name *chip;
    const sensors_feature *feature;
    const sensors_subfeature *subfeature;
    int nr = 0;
    int fnr, sfnr;
    char chip_name[64];
    char *label;
    double value;
    int err = 0;
    struct nas_sensors_info *pinfo;

    while ((chip = sensors_get_detected_chips(NULL, &nr)) != NULL) {
        sensors_snprintf_chip_name(chip_name, sizeof(chip_name), chip);
#ifndef NDEBUG
        syslog(LOG_DEBUG, "Chip(%d): %s\n", nr - 1, chip_name);
#endif

        fnr = 0;
        while ((feature = sensors_get_features(chip, &fnr)) != NULL) {
            label = sensors_get_label(chip, feature);
            if (label == NULL) {
                syslog(LOG_ERR, "can not get feature label for %s %s",
                       chip_name, feature->name);
                err = -1;
                break;
            }
#ifndef NDEBUG
            syslog(LOG_DEBUG, "feature(%d): %s(%s), number=%d, type=%d\n",
                   fnr - 1, label, feature->name, feature->number,
                   feature->type);
#endif

            pinfo = NULL;
            for (int i = 0; i < NAS_SENSORS_COUNT; i++) {
                if ((nas_sensors[i].feature_type == feature->type) &&
                    (strcmp(nas_sensors[i].label, label) == 0)) {
                    pinfo = nas_sensors + i;
                    pinfo->chip = chip;
                    pinfo->feature = feature;
                    break;
                }
            }

            sfnr = 0;
            while ((subfeature = sensors_get_all_subfeatures(
                chip, feature, &sfnr)) != NULL) {
                sensors_get_value(chip, sfnr - 1, &value);
#ifndef NDEBUG
                syslog(LOG_DEBUG, "subfeature(%d): %s, number=%d, type=%d, "
                                  "mapping=%d, flags=%u, value=%f\n",
                       sfnr - 1, subfeature->name, subfeature->number,
                       subfeature->type,
                       subfeature->mapping, subfeature->flags, value);
#endif

                if (pinfo != NULL) {
                    if (subfeature->type == pinfo->subfeature_input) {
                        pinfo->value = value;
                        pinfo->nr = subfeature->number;
                    } else if (subfeature->type == pinfo->subfeature_min) {
                        pinfo->min = value;
                    } else if (subfeature->type == pinfo->subfeature_max) {
                        pinfo->max = value;
                    }
                }
            }

            if (pinfo != NULL) {
                syslog(LOG_INFO,
                       "sensor %s, %s, subfeature %d, value=%f, min=%f, max=%f\n",
                       label, chip_name, pinfo->nr, pinfo->value, pinfo->min,
                       pinfo->max);
            }

            free(label);
        }
    }

    if (!err) {
        for (int i = 0; i < NAS_SENSORS_COUNT; i++) {
            if (nas_sensors[i].chip == NULL) {
                syslog(LOG_ERR, "sensor %s missed\n", nas_sensors[i].label);
                err--;
            }
        }
    }

    if (err != 0) {
        syslog(LOG_ERR, "sensors initialization failed\n");
        exit(EXIT_FAILURE);
    }
}

static int nas_sensor_check(struct nas_sensors_info *p) {
    int err = 0;
    sensors_get_value(p->chip, p->nr, &(p->value));
#ifndef NDEBUG
    syslog(LOG_DEBUG, "%s: value %.2f", p->label, p->value);
#endif

    if (p->value < p->min) {
        err++;
        syslog(LOG_ALERT, "sensor %s: value %.2f below low limit(%f)",
               p->label, p->value, p->min);
    } else if ((p->max != 0) && (p->value > p->max)) {
        err++;
        syslog(LOG_ALERT, "sensor %s: value %.2f beyond high limit(%f)",
               p->label, p->value, p->max);
    }

    return err;
}

int nas_sensor_update(time_t now) {
    static time_t last_tick = 0;

    int err = nas_sensor_check(&(nas_sensors[NAS_SENSOR_CPU]));
    nas_fan_update(nas_sensors[NAS_SENSOR_CPU].value);

    if (now - last_tick >= SENSOR_UPDATE_INTERVAL) {
        for (enum nas_sensors_ids id = NAS_SENSOR_MIN;
             id < NAS_SENSORS_COUNT; id++) {
            if (id != NAS_SENSOR_CPU) {
                err += nas_sensor_check(&(nas_sensors[id]));
            }
        }

        last_tick = now;
    }

    return err;
}

static const char *nas_sensor_fmt[NAS_SENSORS_COUNT][2] = {
    {"CPU Temp:",     "%.1f C"},
    {"System Temp:",  "%.1f C"},
    {"System Fan:",   "%.0f RPM"},
    {"Voltage Core:", "%.6f V"},
    {"Voltage V1.2:", "%.6f V"},
    {"Voltage V3.3:", "%.6f V"},
    {"Voltage V5.0:", "%.6f V"},
    {"Voltage V+12:", "%.6f V"},
};

int nas_sensor_item_show(const int off) {
    static int id = -1;

    id = id >= 0 ? (NAS_SENSORS_COUNT + id + off) % NAS_SENSORS_COUNT : 0;

    lcd_printf(1, nas_sensor_fmt[id][0]);
    lcd_printf(2, nas_sensor_fmt[id][1], nas_sensors[id].value);

    return id;
}

void nas_sensor_summary_show(void) {
    lcd_printf(1, "%.0fC %.0fC %.0fRPM",
               nas_sensors[NAS_SENSOR_CPU].value,
               nas_sensors[NAS_SENSOR_System].value,
               nas_sensors[NAS_SENSOR_Fan].value);
    lcd_printf(2, "%.2f %.2f %.2f", nas_sensors[NAS_SENSOR_Vcore].value,
               nas_sensors[NAS_SENSOR_V5_0].value,
               nas_sensors[NAS_SENSOR_V12].value);
}
