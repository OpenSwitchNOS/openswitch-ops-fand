/*
 * Copyright Mellanox Technologies, Ltd. 2001-2016.
 * This software product is licensed under Apache version 2, as detailed in
 * the COPYING file.
 */

#include <physfan.h>
#include <errno.h>
#include "config-yaml.h"
#include "openvswitch/vlog.h"

VLOG_DEFINE_THIS_MODULE(i2c_plugin);

static struct locl_subsystem * __subsystem_alloc(void);
static int __subsystem_construct(struct locl_subsystem *);
static void __subsystem_destruct(struct locl_subsystem *);
static void __subsystem_dealloc(struct locl_subsystem *);
static int __subsystem_led_state_set(const struct locl_subsystem *subsystem_,
                                     enum fanstatus state);

static struct locl_fan * __fan_alloc(void);
static int __fan_construct(struct locl_fan *);
static void __fan_destruct(struct locl_fan *);
static void __fan_dealloc(struct locl_fan *);
static int __speed_get(const struct locl_fan *, enum fanspeed *);
static int __speed_set(struct locl_fan *, enum fanspeed);
static int __status_get(const struct locl_fan *, enum fanstatus *);
static int __direction_get(const struct locl_fan *, enum fandirection *);
static int __rpm_get(const struct locl_fan *, uint32_t *);

static struct locl_fru *__fru_alloc(void);
static int __fru_construct(struct locl_fru *fru);
static void __fru_destruct(struct locl_fru *fru);
static void __fru_dealloc(struct locl_fru *fru);
static int __fru_led_state_set(const struct locl_fru *fru,
                               enum fanstatus state);
static int __fru_presence_get(const struct locl_fru *fru, bool *present);

static int __i2c_led_set(const struct locl_subsystem *subsystem,
                         i2c_bit_op *led, const enum fanstatus status);

static const struct fand_subsystem_class fand_sybsystem = {
    .fand_subsystem_alloc = __subsystem_alloc,
    .fand_subsystem_construct = __subsystem_construct,
    .fand_subsystem_destruct = __subsystem_destruct,
    .fand_subsystem_dealloc = __subsystem_dealloc,
    .fand_subsystem_led_state_set = __subsystem_led_state_set,
};

const struct fand_fan_class fand_fan = {
    .fand_fan_alloc = __fan_alloc,
    .fand_fan_construct = __fan_construct,
    .fand_fan_destruct = __fan_destruct,
    .fand_fan_dealloc = __fan_dealloc,
    .fand_speed_get = __speed_get,
    .fand_speed_set = __speed_set,
    .fand_status_get = __status_get,
    .fand_direction_get = __direction_get,
    .fand_rpm_get = __rpm_get,
};

static const struct fand_fru_class sysfs_fru_class = {
    .fand_fru_alloc          = __fru_alloc,
    .fand_fru_construct      = __fru_construct,
    .fand_fru_destruct       = __fru_destruct,
    .fand_fru_dealloc        = __fru_dealloc,
    .fand_fru_led_state_set  = __fru_led_state_set,
    .fand_fru_presence_get   = __fru_presence_get,
};

/**
 * Get fand subsystem class.
 */
const struct fand_subsystem_class *
fand_subsystem_class_get(void)
{
    return &fand_sybsystem;
}

/**
 * Get fand fan class.
 */
const struct fand_fan_class *
fand_fan_class_get(void)
{
    return &fand_fan;
}

/**
 * Get fand fru class.
 */
const struct fand_fru_class *fand_fru_class_get(void)
{
    return &sysfs_fru_class;
}

void
fand_plugin_init(void)
{
    VLOG_INFO("Initializing yaml i2c plugin");
}

void
fand_plugin_deinit(void)
{
    VLOG_INFO("De-Initializing yaml i2c plugin");
}

void
fand_plugin_run()
{
}

void fand_plugin_wait()
{
}

static struct locl_subsystem *
__subsystem_alloc(void)
{
    return xzalloc(sizeof(struct locl_subsystem));
}

static int
__subsystem_construct(struct locl_subsystem *subsystem_)
{
    return 0;
}

static void
__subsystem_destruct(struct locl_subsystem *subsystem_)
{
}

static void
__subsystem_dealloc(struct locl_subsystem *subsystem_)
{
    free(subsystem_);
}

static int __subsystem_led_state_set(const struct locl_subsystem *subsystem_,
                                     enum fanstatus state)
{
    const YamlFanInfo *fan_info = subsystem_->info;

    if (!fan_info->global_led) {
        /* global fan led is not available. */
        return 0;
    }

    return __i2c_led_set(subsystem_, fan_info->global_led, state);
}

static struct locl_fan *
__fan_alloc(void)
{
    return xzalloc(sizeof(struct locl_fan));
}

static int
__fan_construct(struct locl_fan *fan_)
{
    return 0;
}

static void
__fan_destruct(struct locl_fan *fan_)
{
}

static void
__fan_dealloc(struct locl_fan *fan_)
{
    free(fan_);
}

static int
__speed_get(const struct locl_fan *fan_, enum fanspeed *speed)
{
    *speed = fan_->subsystem->speed;

    return 0;
}

static int
__speed_set(struct locl_fan *fan_, enum fanspeed speed)
{
    const YamlFanInfo        *const fan_info = fan_->subsystem->info;
    char *const              subsys_name = fan_->subsystem->name;
    unsigned char            hw_speed_val = 0;
    uint32_t                 dword = 0;
    int                      rc = 0;
    i2c_bit_op               *reg_op = NULL;

    reg_op = fan_info->fan_speed_control;
    if (!reg_op) {
        VLOG_ERR("Subsystem %s, fan %s: no fan speed control", subsys_name,
                 fan_->name);
        return ENOENT;
    }

    /* translate the speed */
    switch (speed) {
    case FAND_SPEED_SLOW:
        hw_speed_val = fan_info->fan_speed_settings.slow;
        VLOG_DBG("Subsystem %s, fan %s: setting fan speed control "
                 "register to SLOW: 0x%x",
                 subsys_name, fan_->name,
                 hw_speed_val);
        break;

    case FAND_SPEED_MEDIUM:
        hw_speed_val = fan_info->fan_speed_settings.medium;
        VLOG_DBG("Subsystem %s, fan %s: setting fan speed control "
                 "register to MEDIUM: 0x%x", subsys_name, fan_->name,
                 hw_speed_val);
        break;

    case FAND_SPEED_FAST:
        hw_speed_val = fan_info->fan_speed_settings.fast;
        VLOG_DBG("Subsystem %s, fan %s: setting fan speed control "
                 "register to FAST: 0x%x", subsys_name, fan_->name,
                 hw_speed_val);
        break;

    case FAND_SPEED_MAX:
        hw_speed_val = fan_info->fan_speed_settings.max;
        VLOG_DBG("Subsystem %s, fan %s: setting fan speed control "
                 "register to MAX: 0x%x", subsys_name, fan_->name,
                 hw_speed_val);
        break;

    case FAND_SPEED_NORMAL:
    default:
        hw_speed_val = fan_info->fan_speed_settings.normal;
        VLOG_DBG(
            "Subsystem %s, fan %s: setting fan speed control register to NORMAL: 0x%x",
            subsys_name,
            fan_->name,
            hw_speed_val);
        break;
    }

    VLOG_DBG("Subsystem %s, fan %s: executing write operation to device %s",
             fan_->name, subsys_name, reg_op->device);

    dword = hw_speed_val;
    rc = i2c_reg_write(fan_->subsystem->yaml_handle, subsys_name, reg_op, dword);

    if (rc != 0) {
        VLOG_WARN("Subsystem %s, fan %s: unable to set fan speed control "
                 "register, rc=%d)", subsys_name, fan_->name, rc);
        return EIO;
    }

    return 0;
}

static int
__status_get(const struct locl_fan *fan_, enum fanstatus *status)
{
    const YamlFan *const    yamlFan = fan_->yaml_fan;
    char *const             subsystem_name = fan_->subsystem->name;
    i2c_bit_op              *status_op = NULL;
    int                     rc = 0;
    uint32_t                value = 0;

    VLOG_DBG("Subsystem %s, fan %s: getting status",
             subsystem_name, fan_->name);

    status_op = yamlFan->fan_fault;

    rc = i2c_reg_read(fan_->subsystem->yaml_handle,
                      subsystem_name,
                      status_op,
                      &value);
    if (rc != 0) {
        VLOG_ERR("Subsystem %s, fan %s: unable to read fan status rc=%d",
                 subsystem_name,
                 fan_->name,
                 rc);
        return EIO;
    }

    VLOG_DBG("Subsystem %s, fan %s: status is %08x (%08x)",
             subsystem_name, fan_->name,
             value, status_op->bit_mask);

    if (value != 0) {
        *status = FAND_STATUS_FAULT;
    } else {
        *status = FAND_STATUS_OK;
    }

    return 0;
}

static int
__direction_get(const struct locl_fan *fan_, enum fandirection *dir)
{
    const YamlFanInfo *const fan_info = fan_->subsystem->info;
    char *const              subsystem_name = fan_->subsystem->name;
    const YamlFanFru *const  fan_fru = fan_->fru->yaml_fru;
    i2c_bit_op               *direction_op = fan_fru->fan_direction_detect;
    int                      rc = 0;
    uint32_t                 value = 0;

    rc = i2c_reg_read(fan_->subsystem->yaml_handle,
                      subsystem_name,
                      direction_op,
                      &value);
    if (rc != 0) {
        VLOG_ERR(
            "Subsystem %s, fan %s: unable to read fan fru %d direction rc=%d",
            subsystem_name,
            fan_->name,
            fan_fru->number,
            rc);
        *dir = FAND_DIRECTION_F2B;
        return 0;
    }

    /* OPS_TODO: code assumption: the value is a single bit that indicates
     *  direction as either front-to-back or back-to-front. It would be better
     *  if we had an absolute value, but the i2c ops don't have bit shift values,
     *  so we can't do a direct comparison. */
    if (value != 0) {
        if (fan_info->direction_values.f2b != 0) {
            *dir = FAND_DIRECTION_F2B;
        } else {
            *dir = FAND_DIRECTION_B2F;
        }
    } else {
        if (fan_info->direction_values.f2b != 0) {
            *dir = FAND_DIRECTION_B2F;
        } else {
            *dir = FAND_DIRECTION_F2B;
        }
    }

    return 0;
}

static int
__rpm_get(const struct locl_fan *fan_, uint32_t *rpm)
{
    const YamlFan *const yamlFan = fan_->yaml_fan;
    char *const          subsystem_name = fan_->subsystem->name;
    i2c_bit_op *const    rpm_op = yamlFan->fan_speed;
    uint32_t             dword = 0;
    int                  rc = 0;

    rc = i2c_reg_read(fan_->subsystem->yaml_handle,
                      subsystem_name,
                      rpm_op,
                      &dword);
    if (rc != 0) {
        VLOG_WARN("Subsystem %s: unable to read fan %s rpm, rc=%d",
                  subsystem_name, fan_->name, rc);
        return EIO;
    }

    *rpm = dword;

    return 0;
}

static struct locl_fru *
__fru_alloc(void)
{
    return xzalloc(sizeof(struct locl_fru));
}

static int
__fru_construct(struct locl_fru *fru_)
{
    return 0;
}

static void
__fru_destruct(struct locl_fru *fru_)
{
}

static void
__fru_dealloc(struct locl_fru *fru_)
{
    free(fru_);
}

static int __fru_led_state_set(const struct locl_fru *fru_,
                               enum fanstatus state)
{
    const YamlFanFru *fan_fru = fru_->yaml_fru;

    if (!fan_fru->fan_leds) {
        /* FRU fan led is not available. */
        return 0;
    }

    return __i2c_led_set(fru_->subsystem, fan_fru->fan_leds, state);
}

static int __fru_presence_get(const struct locl_fru *fru_, bool *present)
{
    *present = true;

    return 0;
}

static int __i2c_led_set(const struct locl_subsystem *subsystem,
                         i2c_bit_op *led, const enum fanstatus status)
{
    const char *ledval_str = NULL;
    uint32_t ledval = 0;

    switch(status) {
        case FAND_STATUS_UNINITIALIZED:
            ledval_str = subsystem->info->fan_led_values.off;
            break;
        case FAND_STATUS_OK:
            ledval_str = subsystem->info->fan_led_values.good;
            break;
        case FAND_STATUS_FAULT:
            ledval_str = subsystem->info->fan_led_values.fault;
            break;
        default:
            VLOG_ERR("Got insufficient LED status %d", status);
            return -1;
    }

    if (!ledval_str || !sscanf(ledval_str, "%x", &ledval)) {
        VLOG_ERR("Led values do not provide coorect value %s", ledval_str ? : "NULL");
        return -1;
    }

    return i2c_reg_write(subsystem->yaml_handle, subsystem->name, led, ledval);
}
