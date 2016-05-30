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

void
fand_set_fanspeed(struct locl_subsystem *subsystem)
{
    unsigned char hw_speed_val;
    i2c_bit_op *reg_op;
    uint32_t dword;
    int rc;
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

    reg_op = fan_info->fan_speed_control;

    if (reg_op == NULL) {
        VLOG_DBG("subsystem %s has no fan speed control", subsystem->name);
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

    VLOG_DBG("subsystem %s: executing write operation to device %s",
        subsystem->name,
        reg_op->device);

    dword = hw_speed_val;
    rc = i2c_reg_write(yaml_handle, subsystem->name, reg_op, dword);

    if (rc != 0) {
        VLOG_WARN("subsystem %s: unable to set fan speed control register (%d)",
            subsystem->name,
            rc);
        return;
    }
}

static int
fand_read_rpm(const char *subsystem_name, const YamlFan *fan)
{
    i2c_bit_op *rpm_op;
    uint32_t dword = 0;
    int rc;

    rpm_op = fan->fan_speed;

    rc = i2c_reg_read(yaml_handle, subsystem_name, rpm_op, &dword);

    if (rc != 0) {
        VLOG_WARN("subsystem %s: unable to read fan %s rpm (%d)",
            subsystem_name,
            fan->name,
            rc);
        return(0);
    }

    return dword;
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

void
fand_read_fan_status(struct locl_fan *fan)
{
    fan->rpm = fand_read_rpm(fan->subsystem->name, fan->yaml_fan);
    fan->rpm *= fan->subsystem->multiplier;

    fan->status = fand_read_status(fan->subsystem->name, fan->yaml_fan);
    fan->direction = fand_read_direction(fan->subsystem->name, fan->yaml_fan);
}
