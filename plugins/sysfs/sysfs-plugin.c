/*
 * Copyright Mellanox Technologies, Ltd. 2001-2016.
 * This software product is licensed under Apache version 2, as detailed in
 * the COPYING file.
 */

#include <sensors/sensors.h>
#include <sensors/error.h>

#include "physfan.h"
#include "openvswitch/vlog.h"
#include "config-yaml.h"

#define PWM_SLOW 155
#define PWM_NORMAL 170
#define PWM_MEDIUM 195
#define PWM_FAST 215
#define PWM_MAX 255

#define CHECK_RC(rc, msg, args...)                            \
    do {                                                      \
        if (rc) {                                             \
            VLOG_ERR("%s. " msg, sensors_strerror(rc), args); \
            goto exit;                                        \
        }                                                     \
    } while (0);

VLOG_DEFINE_THIS_MODULE(fand_sysfs);

struct sysfs_subsystem {
    struct locl_subsystem up;
    const struct sensors_chip_name *global_led_chip;
    const struct sensors_subfeature *global_led_out;
};

struct sysfs_fan {
    struct locl_fan up;
    const struct sensors_chip_name *chip;
    const struct sensors_subfeature *input;
    const struct sensors_subfeature *pwm;
    const struct sensors_subfeature *fault;
};

struct sysfs_fru {
    struct locl_fru up;
    const struct sensors_chip_name *led_chip;
    const struct sensors_subfeature *led_out;
    const struct sensors_chip_name *status_chip;
    const struct sensors_subfeature *status;
};

static inline struct sysfs_subsystem *sysfs_subsystem_cast(const struct locl_subsystem *);
static inline struct sysfs_fan *sysfs_fan_cast(const struct locl_fan *);
static inline struct sysfs_fru *sysfs_fru_cast(const struct locl_fru *);
static const char* __led_state_enum_to_string(const YamlFanInfo *info,
                                              enum fanstatus state);

static struct locl_subsystem *__subsystem_alloc(void);
static int __subsystem_construct(struct locl_subsystem *subsystem);
static void __subsystem_destruct(struct locl_subsystem *subsystem);
static void __subsystem_dealloc(struct locl_subsystem *subsystem);
static int __subsystem_led_state_set(const struct locl_subsystem *subsystem,
                                     enum fanstatus state);

static struct locl_fan * __fan_alloc(void);
static int __fan_construct(struct locl_fan *fan);
static void __fan_destruct(struct locl_fan *fan);
static void __fan_dealloc(struct locl_fan *fan);
static int __speed_get(const struct locl_fan *fan, enum fanspeed *speed);
static int __speed_set(struct locl_fan *fan, enum fanspeed speed);
static int __status_get(const struct locl_fan *fan, enum fanstatus *status);
static int __rpm_get(const struct locl_fan *fan, uint32_t *rpm);

static struct locl_fru *__fru_alloc(void);
static int __fru_construct(struct locl_fru *fru);
static void __fru_destruct(struct locl_fru *fru);
static void __fru_dealloc(struct locl_fru *fru);
static int __fru_led_state_set(const struct locl_fru *fru,
                               enum fanstatus state);
static int __fru_presence_get(const struct locl_fru *fru, bool *present);

static const struct fand_subsystem_class sysfs_sybsystem_class = {
    .fand_subsystem_alloc         = __subsystem_alloc,
    .fand_subsystem_construct     = __subsystem_construct,
    .fand_subsystem_destruct      = __subsystem_destruct,
    .fand_subsystem_dealloc       = __subsystem_dealloc,
    .fand_subsystem_led_state_set = __subsystem_led_state_set,
};

const struct fand_fan_class sysfs_fan_class = {
    .fand_fan_alloc     = __fan_alloc,
    .fand_fan_construct = __fan_construct,
    .fand_fan_destruct  = __fan_destruct,
    .fand_fan_dealloc   = __fan_dealloc,
    .fand_speed_get     = __speed_get,
    .fand_speed_set     = __speed_set,
    .fand_status_get    = __status_get,
    .fand_rpm_get       = __rpm_get,
};

static const struct fand_fru_class sysfs_fru_class = {
    .fand_fru_alloc          = __fru_alloc,
    .fand_fru_construct      = __fru_construct,
    .fand_fru_destruct       = __fru_destruct,
    .fand_fru_dealloc        = __fru_dealloc,
    .fand_fru_led_state_set  = __fru_led_state_set,
    .fand_fru_presence_get   = __fru_presence_get,
};

static inline struct sysfs_subsystem *
sysfs_subsystem_cast(const struct locl_subsystem *subsystem_)
{
    ovs_assert(subsystem_);

    return CONTAINER_OF(subsystem_, struct sysfs_subsystem, up);
}

static inline struct sysfs_fan *
sysfs_fan_cast(const struct locl_fan *fan_)
{
    ovs_assert(fan_);

    return CONTAINER_OF(fan_, struct sysfs_fan, up);
}

static inline struct sysfs_fru *
sysfs_fru_cast(const struct locl_fru *fru_)
{
    ovs_assert(fru_);

    return CONTAINER_OF(fru_, struct sysfs_fru, up);
}

static const char*
__led_state_enum_to_string(const YamlFanInfo *info,
                           enum fanstatus state)
{
    switch (state) {
        case FAND_STATUS_OK:
            return info->fan_led_values.good;
        case FAND_STATUS_FAULT:
            return info->fan_led_values.fault;
        case FAND_STATUS_UNINITIALIZED:
            return info->fan_led_values.off;
        default:
            VLOG_WARN("Invalid sysfs state %d", (int)state);
            return NULL;
    }
}

/**
 * Get fand subsystem class.
 */
const struct fand_subsystem_class *fand_subsystem_class_get(void)
{
    return &sysfs_sybsystem_class;
}

/**
 * Get fand fan class.
 */
const struct fand_fan_class *fand_fan_class_get(void)
{
    return &sysfs_fan_class;
}

/**
 * Get fand fru class.
 */
const struct fand_fru_class *fand_fru_class_get(void)
{
    return &sysfs_fru_class;
}

/**
 * Initialize ops-fand platform support plugin.
 */
void fand_plugin_init(void)
{
    /* We use default config file because nothing additional is needed here. */
    if (sensors_init(NULL)) {
        VLOG_ERR("Failed to initialize sensors library.");
    }
}

/**
 * Deinitialize ops-fand platform support plugin.
 * plugin.
 */
void fand_plugin_deinit(void)
{
    sensors_cleanup();
}

void
fand_plugin_run(void)
{
}

void fand_plugin_wait(void)
{
}

static struct locl_subsystem *
__subsystem_alloc(void)
{
    struct sysfs_subsystem *subsystem = xzalloc(sizeof(struct sysfs_subsystem));

    return &subsystem->up;
}

static int
__subsystem_construct(struct locl_subsystem *subsystem_)
{
    int chip_num = 0, feature_num = 0;
    const sensors_chip_name *chip = NULL;
    const sensors_feature *feature = NULL;
    struct sysfs_subsystem *subsystem = sysfs_subsystem_cast(subsystem_);

    if (subsystem_->info->global_led_name == NULL) {
        goto exit;
    }

    chip_num = 0;
    while ((chip = sensors_get_detected_chips(NULL, &chip_num))) {
        feature_num = 0;
        while ((feature = sensors_get_features(chip, &feature_num))) {
            if (!strcmp(subsystem_->info->global_led_name, feature->name)) {
                subsystem->global_led_out = sensors_get_subfeature(chip,
                                                                     feature,
                                                                     SENSORS_SUBFEATURE_LED_OUTPUT);
                subsystem->global_led_chip = chip;
                goto exit;
            }
        }
    }

exit:
    if ((subsystem_->info->global_led_name != NULL) &&
        (subsystem->global_led_out == NULL)) {
        VLOG_WARN("%s does not have led input subfeature.", subsystem_->name);
    }

    return 0;
}

static void
__subsystem_destruct(struct locl_subsystem *subsystem)
{
}

static void
__subsystem_dealloc(struct locl_subsystem *subsystem_)
{
    struct sysfs_subsystem *subsystem = sysfs_subsystem_cast(subsystem_);

    free(subsystem);
}

static int __subsystem_led_state_set(const struct locl_subsystem *subsystem_,
                                     enum fanstatus state)
{
    int rc = 0;
    struct sysfs_subsystem *subsystem = sysfs_subsystem_cast(subsystem_);
    const char *state_str = __led_state_enum_to_string(subsystem_->info,
                                                       state);

    if (!subsystem->global_led_out) {
        goto exit;
    }

    rc = sensors_set_char_value(subsystem->global_led_chip,
                                subsystem->global_led_out->number,
                                (char *)state_str);
    CHECK_RC(rc, "Failed to set global led for %s subsystem", subsystem_->name);

exit:
    return rc ? -1 : 0;
}

static struct locl_fan *
__fan_alloc(void)
{
    struct sysfs_fan *fan = xzalloc(sizeof(struct sysfs_fan));

    return &fan->up;
}

static int __fan_construct
(struct locl_fan *fan_)
{
    int chip_num = 0, feature_num = 0, num = 0, fan_num = 0;
    const sensors_chip_name *chip = NULL;
    const sensors_feature *feature = NULL;
    struct sysfs_fan *fan = sysfs_fan_cast(fan_);

    sscanf(fan_->yaml_fan->name, "fan%d", &fan_num);

    chip_num = 0;
    while ((chip = sensors_get_detected_chips(NULL, &chip_num))) {
        feature_num = 0;
        while ((feature = sensors_get_features(chip, &feature_num))) {
            if ((sscanf(feature->name, "fan%d", &num) == 1) && num == fan_num) {
                if (sensors_get_subfeature(chip, feature,
                                           SENSORS_SUBFEATURE_FAN_STATUS)) {
                    /* This feature is FRU, not fan. Skipping. */
                    continue;
                }
                fan->input = sensors_get_subfeature(chip, feature,
                                                    SENSORS_SUBFEATURE_FAN_INPUT);
                fan->fault = sensors_get_subfeature(chip, feature,
                                                    SENSORS_SUBFEATURE_FAN_FAULT);
                fan->chip = chip;
            }
            if ((sscanf(feature->name, "pwm%d", &num) == 1) && num == fan_num) {
                fan->pwm = sensors_get_subfeature(chip, feature,
                                                  SENSORS_SUBFEATURE_PWM_OUTPUT);
            }
        }
    }

    if (!fan->input) {
        VLOG_WARN("%s does not have input subfeature.", fan_->name);
    }
    if (!fan->fault) {
        VLOG_WARN("%s does not have fault subfeature.", fan_->name);
    }
    if (!fan->pwm) {
        VLOG_WARN("%s does not have pwm control.", fan_->name);
    }

    return 0;
}

static void
__fan_destruct(struct locl_fan *fan_)
{
}

static void
__fan_dealloc(struct locl_fan *fan_)
{
    struct sysfs_fan *fan = sysfs_fan_cast(fan_);

    free(fan);
}

static int
__speed_get(const struct locl_fan *fan_, enum fanspeed *speed)
{
    int rc = 0;
    double speed_val = 0.0;
    struct sysfs_fan *fan = sysfs_fan_cast(fan_);

    if (fan->pwm == NULL) {
        *speed = FAND_SPEED_NONE;
        goto exit;
    }

    rc = sensors_get_value(fan->chip, fan->pwm->number, &speed_val);
    CHECK_RC(rc, "Get speed for %s", fan_->name);

    switch ((int)speed_val) {
        case PWM_MAX:
            *speed = FAND_SPEED_MAX;
            break;
        case PWM_FAST:
            *speed = FAND_SPEED_FAST;
            break;
        case PWM_MEDIUM:
            *speed = FAND_SPEED_MEDIUM;
            break;
        case PWM_NORMAL:
            *speed = FAND_SPEED_NORMAL;
            break;
        case PWM_SLOW:
            *speed = FAND_SPEED_SLOW;
            break;
        default:
            *speed = FAND_SPEED_NONE;
    }

exit:
    return rc;
}

static int __speed_set(struct locl_fan *fan_, enum fanspeed speed)
{
    int rc = 0, pwm = 0;
    struct sysfs_fan *fan = sysfs_fan_cast(fan_);

    if (fan->pwm == NULL) {
        goto exit;
    }

    switch (speed) {
        case FAND_SPEED_SLOW:
            pwm = PWM_SLOW;
            break;
        case FAND_SPEED_MEDIUM:
            pwm = PWM_MEDIUM;
            break;
        case FAND_SPEED_NORMAL:
            pwm = PWM_NORMAL;
            break;
        case FAND_SPEED_FAST:
            pwm = PWM_FAST;
            break;
        case FAND_SPEED_MAX:
            pwm = PWM_MAX;
            break;
        default:
            break;
    }

    rc = sensors_set_value(fan->chip, fan->pwm->number, pwm);
    CHECK_RC(rc, "Set speed for %s", fan_->name);

exit:
    return rc;
}

static int
__status_get(const struct locl_fan *fan_, enum fanstatus *status)
{
    int rc = 0;
    double status_val = 0;
    struct sysfs_fan *fan = sysfs_fan_cast(fan_);

    if (fan->fault == NULL) {
        *status = FAND_STATUS_OK;
        goto exit;
    }

    rc = sensors_get_value(fan->chip, fan->fault->number, &status_val);
    CHECK_RC(rc, "Set speed for %s", fan_->name);

    if (status_val) {
        *status = FAND_STATUS_FAULT;
    } else {
        *status = FAND_STATUS_OK;
    }

exit:
    return rc;
}

static int
__rpm_get(const struct locl_fan *fan_, uint32_t *rpm)
{
    int rc = 0;
    double rpm_val = 0;
    struct sysfs_fan *fan = sysfs_fan_cast(fan_);

    if (fan->input == NULL) {
        goto exit;
    }

    rc = sensors_get_value(fan->chip, fan->input->number, &rpm_val);
    CHECK_RC(rc, "Get rpm for %s", fan_->name);

    *rpm = rpm_val;

exit:
    return rc;
}

static struct locl_fru *
__fru_alloc(void)
{
    struct sysfs_fru *fru = xzalloc(sizeof(struct sysfs_fru));

    return &fru->up;
}

static int
__fru_construct(struct locl_fru *fru_)
{
    int chip_num = 0, feature_num = 0;
    const sensors_chip_name *chip = NULL;
    const sensors_feature *feature = NULL;
    char fru_name[NAME_MAX] = { };
    struct sysfs_fru *fru = sysfs_fru_cast(fru_);

    /* Fan FRU is represented in sysfs as fan[1-*], but has status instead of
     * input and fault. */
    sprintf(fru_name, "fan%d", fru_->yaml_fru->number);

    chip_num = 0;
    while ((chip = sensors_get_detected_chips(NULL, &chip_num))) {
        feature_num = 0;
        while ((feature = sensors_get_features(chip, &feature_num))) {
            if (fru_->yaml_fru->led_name &&
                !strcmp(fru_->yaml_fru->led_name, feature->name)) {
                fru->led_out = sensors_get_subfeature(chip,
                                                      feature,
                                                      SENSORS_SUBFEATURE_LED_OUTPUT);
                fru->led_chip = chip;
            }
            if (!strcmp(fru_name, feature->name) && !fru->status) {
                fru->status = sensors_get_subfeature(chip,
                                                     feature,
                                                     SENSORS_SUBFEATURE_FAN_STATUS);
                fru->status_chip = chip;
            }
        }
    }

    if ((fru_->yaml_fru->led_name != NULL) &&
        (fru->led_out == NULL)) {
        VLOG_WARN("%s does not have led input subfeature.", fru_->name);
    }

    if (fru->status == NULL) {
        VLOG_WARN("%s does not have status subfeature.", fru_->name);
    }

    return 0;
}

static void
__fru_destruct(struct locl_fru *fru_)
{
}

static void
__fru_dealloc(struct locl_fru *fru_)
{
    struct sysfs_fru *fru = sysfs_fru_cast(fru_);

    free(fru);
}

static int __fru_led_state_set(const struct locl_fru *fru_,
                               enum fanstatus state)
{
    int rc = 0;
    struct sysfs_fru *fru = sysfs_fru_cast(fru_);
    const char *state_str = __led_state_enum_to_string(fru_->subsystem->info,
                                                       state);

    if (!fru->led_out) {
        goto exit;
    }

    rc = sensors_set_char_value(fru->led_chip,
                                fru->led_out->number,
                                (char *)state_str);
    CHECK_RC(rc, "Failed to set led for %s fru", fru_->name);

exit:
    return rc ? -1 : 0;
}

static int __fru_presence_get(const struct locl_fru *fru_, bool *present)
{
    int rc = 0;
    double value = 0.0;
    struct sysfs_fru *fru = sysfs_fru_cast(fru_);

    if (!fru->status) {
        /* If status is not available, treat FRU as integrated. */
        *present = true;
        goto exit;
    }

    rc = sensors_get_value(fru->status_chip, fru->status->number, &value);
    CHECK_RC(rc, "Failed to set status for %s fru", fru_->name);

    /* Status is 1 if FRU is present, 0 otherwise. */
    *present = (value == 1.0);

exit:
    return rc ? -1 : 0;
}
