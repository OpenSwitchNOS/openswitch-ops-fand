/*
 *  (c) Copyright 2015 Hewlett Packard Enterprise Development LP
 *
 *  Licensed under the Apache License, Version 2.0 (the "License"); you may
 *  not use this file except in compliance with the License. You may obtain
 *  a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
 *  WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. See the
 *  License for the specific language governing permissions and limitations
 *  under the License.
 */

/************************************************************************//**
 * @ingroup fand
 *
 * @file
 * Source file for set set fan speed functions.
 ***************************************************************************/

#include "openvswitch/vlog.h"
#include "config-yaml.h"
#include "fanspeed.h"
#include "fandirection.h"
#include "fand-locl.h"
#include "eventlog.h"

VLOG_DEFINE_THIS_MODULE(physfan);

/* global yaml config handle */
extern YamlConfigHandle yaml_handle;

static struct locl_fan *get_local_fan(struct locl_subsystem *subsystem,
                                      const char *name)
{
    struct shash_node *node, *next;
    struct locl_fan *fan = NULL;
    char fullname[128];

    snprintf(fullname, sizeof(fullname), "%s-%s",
             subsystem->name, name);

    SHASH_FOR_EACH_SAFE(node, next, &subsystem->subsystem_fans) {
        fan = (struct locl_fan *)node->data;
        if (strcmp(fan->name, fullname) == 0)
            break;
    }

    return fan;
}

static int fand_set_led(struct locl_subsystem *subsystem,
                        const YamlFanInfo *fan_info,
                        i2c_bit_op *led, const enum fanstatus status)
{
    unsigned char ledval = 0;

    switch(status) {
    case FAND_STATUS_UNINITIALIZED:
        ledval = fan_info->fan_led_values.off;
        break;
    case FAND_STATUS_OK:
        ledval = fan_info->fan_led_values.good;
        break;
    case FAND_STATUS_FAULT:
    default:
        ledval = fan_info->fan_led_values.fault;
        break;
    }
    return i2c_reg_write(yaml_handle, subsystem->name, led, ledval);
 }

void fand_set_fanleds(struct locl_subsystem *subsystem)
{
    const YamlFanInfo *fan_info;
    enum fanstatus aggr_status = FAND_STATUS_UNINITIALIZED;
    int rc = 0;

    fan_info = yaml_get_fan_info(yaml_handle, subsystem->name);
    if (fan_info == NULL) {
        VLOG_DBG("subsystem %s has no fan info", subsystem->name);
        return;
    }

    for (size_t idx = 0; idx < fan_info->number_fan_frus; idx++) {
        enum fanstatus status = FAND_STATUS_UNINITIALIZED;
        const YamlFanFru *fru = yaml_get_fan_fru(yaml_handle,
                                                 subsystem->name, idx);
        for (size_t fan_idx = 0; fru->fans[fan_idx]; fan_idx++) {
            const YamlFan *fan = fru->fans[fan_idx];
            struct locl_fan *lfan = get_local_fan(subsystem, fan->name);
            if (lfan && (lfan->status > status)) {
                status = lfan->status;
            }
        }
        if (status > aggr_status)
            aggr_status = status;

        if (fru->fan_leds == NULL)
            continue;

        rc = fand_set_led(subsystem, fan_info, fru->fan_leds, status);
        if (rc) {
            VLOG_DBG("Unable to set subsystem %s fan fru %d status LED",
                     subsystem->name, fru->number);
        }
    }

    if (fan_info->fan_led) {
        rc = fand_set_led(subsystem, fan_info,
                          fan_info->fan_led, aggr_status);
        if (rc) {
            VLOG_DBG("Unable to set subsystem %s fan status LED",
                     subsystem->name);
        }
    }
}

void
fand_set_fanspeed(struct locl_subsystem *subsystem)
{
    unsigned char hw_speed_val;
    const YamlFanInfo *fan_info = NULL;
    enum fanspeed speed = subsystem->fan_speed_override;

    /* use override if it exists, unless the sensors think the speed should be
       "max" (potential overtemp situation). */
    if (speed == FAND_SPEED_NONE || subsystem->fan_speed == FAND_SPEED_MAX) {
        speed = subsystem->fan_speed;
    }

    if (speed == FAND_SPEED_NONE) {
        speed = FAND_SPEED_NORMAL;
    }

    /* set the speed value for record-keeping */
    subsystem->speed = speed;

    /* get the fan speed control i2c operation */
    fan_info = yaml_get_fan_info(yaml_handle, subsystem->name);

    if (fan_info == NULL) {
        VLOG_DBG("subsystem %s has no fan info", subsystem->name);
        return;
    }

    /* translate the speed */
    switch (speed) {
        case FAND_SPEED_NORMAL:
        default:
            hw_speed_val = fan_info->fan_speed_settings.normal;
            VLOG_DBG("subsystem %s: setting fan speed control register to NORMAL: 0x%x",
                subsystem->name,
                hw_speed_val);
            log_event("FAN_SPEED", EV_KV("subsystem", "%s", subsystem->name),
                EV_KV("speedval", "%s", "NORMAL"),
                EV_KV("value", "0x%x", hw_speed_val));
            break;
        case FAND_SPEED_SLOW:
            hw_speed_val = fan_info->fan_speed_settings.slow;
            VLOG_DBG("subsystem %s: setting fan speed control register to SLOW: 0x%x",
                subsystem->name,
                hw_speed_val);
            log_event("FAN_SPEED", EV_KV("subsystem", "%s", subsystem->name),
                EV_KV("speedval", "%s", "SLOW"),
                EV_KV("value", "0x%x", hw_speed_val));
            break;
        case FAND_SPEED_MEDIUM:
            hw_speed_val = fan_info->fan_speed_settings.medium;
            VLOG_DBG("subsystem %s: setting fan speed control register to MEDIUM: 0x%x",
                subsystem->name,
                hw_speed_val);
            log_event("FAN_SPEED", EV_KV("subsystem", "%s", subsystem->name),
                EV_KV("speedval", "%s", "MEDIUM"),
                EV_KV("value", "0x%x", hw_speed_val));
            break;
        case FAND_SPEED_FAST:
            hw_speed_val = fan_info->fan_speed_settings.fast;
            VLOG_DBG("subsystem %s: setting fan speed control register to FAST: 0x%x",
                subsystem->name,
                hw_speed_val);
            log_event("FAN_SPEED", EV_KV("subsystem", "%s", subsystem->name),
                EV_KV("speedval", "%s", "FAST"),
                EV_KV("value", "0x%x", hw_speed_val));
            break;
        case FAND_SPEED_MAX:
            hw_speed_val = fan_info->fan_speed_settings.max;
            VLOG_DBG("subsystem %s: setting fan speed control register to MAX: 0x%x",
                subsystem->name,
                hw_speed_val);
            log_event("FAN_SPEED", EV_KV("subsystem", "%s", subsystem->name),
                EV_KV("speedval", "%s", "MAX"),
                EV_KV("value", "0x%x", hw_speed_val));
            break;
    }

    /* Fan speed may have one control per subsystem, per fru, or per fan. */
    if (fan_info->fan_speed_control_type == SINGLE) {
        if (fan_info->fan_speed_control == NULL) {
            VLOG_DBG("subsystem %s has no fan speed control", subsystem->name);
            return;
        }
        i2c_reg_write(yaml_handle, subsystem->name,
                      fan_info->fan_speed_control, hw_speed_val);
        VLOG_DBG("FAN speed set to %#x", hw_speed_val);
    } else {
        for (size_t idx = 0; idx < fan_info->number_fan_frus; idx++) {
            const YamlFanFru *fru = yaml_get_fan_fru(yaml_handle,
                                                     subsystem->name, idx);
            if (fan_info->fan_speed_control_type == PER_FRU) {
                if (fru->fan_speed_control == NULL) {
                  VLOG_DBG("fan fru %d has no fan speed control", fru->number);
                  continue;
                }
                i2c_reg_write(yaml_handle, subsystem->name,
                              fru->fan_speed_control, hw_speed_val);
            } else if (fan_info->fan_speed_control_type == PER_FAN) {
               for (size_t fan_idx = 0; fru->fans[fan_idx]; fan_idx++) {
                    const YamlFan *fan = fru->fans[fan_idx];
                    if (fan->fan_speed_control == NULL) {
                        VLOG_DBG("fan %s has no fan speed control", fan->name);
                        continue;
                    }
                    i2c_reg_write(yaml_handle, subsystem->name,
                                  fan->fan_speed_control, hw_speed_val);
               }
            } else {
                VLOG_WARN("subsystem %s: invalid fan speed control type (%d)",
                          subsystem->name,
                          fan_info->fan_speed_control_type);
                return;
            }
        }
    }
}

static int
fand_read_rpm(const char *subsystem_name, const YamlFan *fan)
{
    uint32_t dword = 0;
    uint32_t rpm;
    int rc;

    rc = i2c_reg_read(yaml_handle, subsystem_name, fan->fan_speed, &dword);

    if (rc != 0) {
        VLOG_WARN("subsystem %s: unable to read fan %s rpm (%d)",
            subsystem_name,
            fan->name,
            rc);
        return(0);
    }

    /* Least significant byte */
    rpm = dword;

    if (fan->fan_speed_msb) {
        rc = i2c_reg_read(yaml_handle, subsystem_name,
                          fan->fan_speed_msb, &dword);

        if (rc != 0) {
            VLOG_WARN("subsystem %s: unable to read fan %s rpm MSB (%d)",
                      subsystem_name,
                      fan->name,
                      rc);
            return(0);
        }

        /* Most significant byte */
        rpm += dword << 8;
    }

    return (int)rpm;
}

static enum fanstatus
fand_read_status(const char *subsystem_name, const YamlFan *fan)
{
    i2c_bit_op *status_op;
    int rc;
    uint32_t value = 0;

    status_op = fan->fan_fault;

    rc = i2c_reg_read(yaml_handle, subsystem_name, status_op, &value);

    if (rc != 0) {
        VLOG_WARN("subsystem %s: unable to read fan %s status (%d)",
            subsystem_name,
            fan->name,
            rc);
        return(0);
    }
    VLOG_DBG("status is %08x (%08x)", value, status_op->bit_mask);

    if (value != 0) {
        VLOG_DBG("status is fault");
        return FAND_STATUS_FAULT;
    }
    return FAND_STATUS_OK;
}

enum fandirection
fand_read_fan_fru_direction(
    const char *subsystem_name,
    const YamlFanFru *fru,
    const YamlFanInfo *info)
{
    i2c_bit_op *direction_op;
    int rc;
    uint32_t value;

    direction_op = fru->fan_direction_detect;

    rc = i2c_reg_read(yaml_handle, subsystem_name, direction_op, &value);

    if (rc != 0) {
        VLOG_WARN("subsystem %s: unable to read fan fru %d direction (%d)",
            subsystem_name,
            fru->number,
            rc);
        return(FAND_DIRECTION_F2B);
    }

    VLOG_DBG("direction is %08x (%08x)", value, direction_op->bit_mask);

    /* OPS_TODO: code assumption: the value is a single bit that indicates
       direction as either front-to-back or back-to-front. It would be better
       if we had an absolute value, but the i2c ops don't have bit shift values,
       so we can't do a direct comparison. */
    if (value != 0) {
        if (info->direction_values.f2b != 0) {
            return FAND_DIRECTION_F2B;
        } else {
            return FAND_DIRECTION_B2F;
        }
    } else {
        if (info->direction_values.f2b != 0) {
            return FAND_DIRECTION_B2F;
        } else {
            return FAND_DIRECTION_F2B;
        }
    }
}

static const YamlFanFru *
fan_fru_get(const char *subsystem_name, const YamlFan *fan)
{
    int idx;
    int count;

    count = yaml_get_fan_fru_count(yaml_handle, subsystem_name);

    for (idx = 0; idx < count; idx++) {
        const YamlFanFru *fru;
        int fan_idx;

        fru = yaml_get_fan_fru(yaml_handle, subsystem_name, idx);

        for (fan_idx = 0; fru->fans[fan_idx] != NULL; fan_idx++) {
            if (fan == fru->fans[fan_idx]) {
                return fru;
            }
        }
    }
    return(NULL);
}

static const char *
fand_read_direction(const char *subsystem_name, const YamlFan *fan)
{
    const YamlFanFru *fan_fru;
    const YamlFanInfo *fan_info;
    enum fandirection fan_direction = FAND_DIRECTION_F2B;

    fan_fru = fan_fru_get(subsystem_name, fan);

    if (fan_fru == NULL) {
        return(fan_direction_enum_to_string(fan_direction));
    }

    fan_info = yaml_get_fan_info(yaml_handle, subsystem_name);

    if (fan_fru->fan_direction_detect != NULL) {
        fan_direction = fand_read_fan_fru_direction(
                subsystem_name,
                fan_fru,
                fan_info);
    }

    return(fan_direction_enum_to_string(fan_direction));
}

static int
fand_read_present(const char *subsystem_name, const YamlFanFru *fru)
{
    int rc;
    uint32_t present;

    if (!fru->fan_present)
        present = 1;
    else {
        rc = i2c_reg_read(yaml_handle, subsystem_name,
                          fru->fan_present, &present);
        if (rc < 0) {
            VLOG_WARN("subsystem %s: unable to read FRU %d present (%d)",
                      subsystem_name,
                      fru->number,
                      rc);
            present = 0;
        }
    }
    return (present != 0);
}

void
fand_read_fan_status(struct locl_fan *fan)
{
    const YamlFanFru *fan_fru;

    fan->direction = fand_read_direction(fan->subsystem->name, fan->yaml_fan);

    fan_fru = fan_fru_get(fan->subsystem->name, fan->yaml_fan);
    if (!fand_read_present(fan->subsystem->name, fan_fru)) {
        fan->status = FAND_STATUS_FAULT;
        fan->rpm = 0;
        return;
    }

    fan->rpm = fand_read_rpm(fan->subsystem->name, fan->yaml_fan);
    if (fan->subsystem->multiplier)
        fan->rpm *= fan->subsystem->multiplier;
    else if (fan->subsystem->numerator) {
        if (fan->rpm)
          fan->rpm = fan->subsystem->numerator / fan->rpm;
        else
          fan->rpm = 0;
    }
    else {
        VLOG_WARN("subsystem %s: No valid fan speed calculation found.",
                  fan->subsystem->name);
    }

    fan->status = fand_read_status(fan->subsystem->name, fan->yaml_fan);
}
