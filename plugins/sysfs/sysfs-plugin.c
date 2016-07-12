/*
 * Copyright Mellanox Technologies, Ltd. 2001-2016.
 * This software product is licensed under Apache version 2, as detailed in
 * the COPYING file.
 */

#include <sensors/sensors.h>
#include <sensors/error.h>

#include "physfan.h"
#include "openvswitch/vlog.h"

#define PWM_SLOW 64
#define PWM_NORMAL 102
#define PWM_MEDIUM 166
#define PWM_FAST 204
#define PWM_MAX 255

#define CHECK_RC(rc, msg, args...)                            \
    do {                                                      \
        if (rc) {                                             \
            VLOG_ERR("%s. " msg, sensors_strerror(rc), args); \
            goto exit;                                        \
        }                                                     \
    } while (0);

VLOG_DEFINE_THIS_MODULE(fand_sysfs);

struct sysfs_fan {
    struct locl_fan up;
    const struct sensors_chip_name *chip;
    const struct sensors_subfeature *input;
    const struct sensors_subfeature *pwm;
    const struct sensors_subfeature *fault;
};

static inline struct sysfs_fan *sysfs_fan_cast(const struct locl_fan *);
static struct locl_subsystem *__subsystem_alloc(void);
static int __subsystem_construct(struct locl_subsystem *subsystem);
static void __subsystem_destruct(struct locl_subsystem *subsystem);
static void __subsystem_dealloc(struct locl_subsystem *subsystem);
static int __enumerate_devices(const struct locl_subsystem *subsystem,
                               struct sset *fans);

static struct locl_fan * __fan_alloc(void);
static int __fan_construct(struct locl_fan *fan);
static void __fan_destruct(struct locl_fan *fan);
static void __fan_dealloc(struct locl_fan *fan);
static int __speed_get(const struct locl_fan *fan, enum fanspeed *speed);
static int __speed_set(struct locl_fan *fan, enum fanspeed speed);
static int __status_get(const struct locl_fan *fan, enum fanstatus *status);
static int __rpm_get(const struct locl_fan *fan, uint32_t *rpm);

static const struct fand_subsystem_class sysfs_sybsystem_class = {
    .fand_subsystem_alloc     = __subsystem_alloc,
    .fand_subsystem_construct = __subsystem_construct,
    .fand_subsystem_destruct  = __subsystem_destruct,
    .fand_subsystem_dealloc   = __subsystem_dealloc,
    .fand_enumerate_devices   = __enumerate_devices
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

static inline struct sysfs_fan *
sysfs_fan_cast(const struct locl_fan *fan_)
{
    ovs_assert(fan_);

    return CONTAINER_OF(fan_, struct sysfs_fan, up);
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
    return xzalloc(sizeof(struct locl_subsystem));
}

static int
__subsystem_construct(struct locl_subsystem *subsystem)
{
    return 0;
}

static void
__subsystem_destruct(struct locl_subsystem *subsystem)
{
}

static void
__subsystem_dealloc(struct locl_subsystem *subsystem)
{
    free(subsystem);
}

static int
__enumerate_devices(const struct locl_subsystem *subsystem, struct sset *fans)
{
    int chip_num = 0, feature_num = 0;
    const sensors_chip_name *chip = NULL;
    const sensors_feature *feature = NULL;

    chip_num = 0;
    while ((chip = sensors_get_detected_chips(NULL, &chip_num))) {
        feature_num = 0;
        while ((feature = sensors_get_features(chip, &feature_num))) {
            if (feature->type == SENSORS_FEATURE_FAN) {
                sset_add(fans, feature->name);
            }
        }
    }

    return 0;
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

    sscanf(fan_->name, "fan%d", &fan_num);

    chip_num = 0;
    while ((chip = sensors_get_detected_chips(NULL, &chip_num))) {
        feature_num = 0;
        while ((feature = sensors_get_features(chip, &feature_num))) {
            if ((sscanf(feature->name, "fan%d", &num) == 1) && num == fan_num) {
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
