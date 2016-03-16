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

static void
i2c_debug(
    const char *sub,
    const YamlDevice *device,
    i2c_op **cmds)
{
    int i;

    VLOG_DBG("I2C: sub name: %s", sub);
    VLOG_DBG("I2C: device name: %s", device->name);

    for (i = 0; cmds[i] != NULL; i++) {
        VLOG_DBG("I2C[%d]: direction: %s", i, cmds[i]->direction == READ ? "read" : "write");
        VLOG_DBG("I2C[%d]: device: %s", i, cmds[i]->device);
        VLOG_DBG("I2C[%d]: byte count: %d", i, cmds[i]->byte_count);
        VLOG_DBG("I2C[%d]: address: %d", i, cmds[i]->register_address);
    }
}

void
fand_set_fanspeed(struct locl_subsystem *subsystem)
{
    unsigned char hw_speed_val;
    i2c_bit_op *reg_op;
    i2c_op op;
    i2c_op *cmds[2];
    unsigned char byte;
    unsigned short word;
    unsigned long dword;
    int rc;
    const YamlFanInfo *fan_info = NULL;
    const YamlDevice *device = NULL;
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
                EV_KV("speed", "%s", "NORMAL"),
                EV_KV("value", "0x%x", hw_speed_val));
            break;
        case FAND_SPEED_SLOW:
            hw_speed_val = fan_info->fan_speed_settings.slow;
            VLOG_DBG("subsystem %s: setting fan speed control register to SLOW: 0x%x",
                subsystem->name,
                hw_speed_val);
            log_event("FAN_SPEED", EV_KV("subsystem", "%s", subsystem->name),
                EV_KV("speed", "%s", "SLOW"),
                EV_KV("value", "0x%x", hw_speed_val));
            break;
        case FAND_SPEED_MEDIUM:
            hw_speed_val = fan_info->fan_speed_settings.medium;
            VLOG_DBG("subsystem %s: setting fan speed control register to MEDIUM: 0x%x",
                subsystem->name,
                hw_speed_val);
            log_event("FAN_SPEED", EV_KV("subsystem", "%s", subsystem->name),
                EV_KV("speed", "%s", "MEDIUM"),
                EV_KV("value", "0x%x", hw_speed_val));
            break;
        case FAND_SPEED_FAST:
            hw_speed_val = fan_info->fan_speed_settings.fast;
            VLOG_DBG("subsystem %s: setting fan speed control register to FAST: 0x%x",
                subsystem->name,
                hw_speed_val);
            log_event("FAN_SPEED", EV_KV("subsystem", "%s", subsystem->name),
                EV_KV("speed", "%s", "FAST"),
                EV_KV("value", "0x%x", hw_speed_val));
            break;
        case FAND_SPEED_MAX:
            hw_speed_val = fan_info->fan_speed_settings.max;
            VLOG_DBG("subsystem %s: setting fan speed control register to MAX: 0x%x",
                subsystem->name,
                hw_speed_val);
            log_event("FAN_SPEED", EV_KV("subsystem", "%s", subsystem->name),
                EV_KV("speed", "%s", "MAX"),
                EV_KV("value", "0x%x", hw_speed_val));
            break;
    }

    /* get the device */
    device = yaml_find_device(yaml_handle, subsystem->name, reg_op->device);

    if (device == NULL) {
        VLOG_WARN("subsystem %s: unable to find fan speed control device %s",
                subsystem->name,
                reg_op->device);
        return;
    }

    VLOG_DBG("subsystem %s: executing read operation to device %s",
        subsystem->name,
        reg_op->device);
    /* we're going to do a read/modify/write: read the data */
    op.direction = READ;
    op.device = reg_op->device;
    op.register_address = reg_op->register_address;
    op.byte_count = reg_op->register_size;
    switch (reg_op->register_size) {
        case 1:
            op.data = (unsigned char *)&byte;
            break;
        case 2:
            op.data = (unsigned char *)&word;
            break;
        case 4:
            op.data = (unsigned char *)&dword;
            break;
        default:
            VLOG_WARN("subsystem %s: invalid fan speed control register size (%d)",
                subsystem->name,
                reg_op->register_size);
            return;
    }
    op.set_register = false;
    op.negative_polarity = false;
    cmds[0] = &op;
    cmds[1] = NULL;

    i2c_debug(subsystem->name, device, cmds);
    rc = i2c_execute(yaml_handle, subsystem->name, device, cmds);

    if (rc != 0) {
        VLOG_WARN("subsystem %s: unable to read fan speed control register (%d)",
            subsystem->name,
            rc);
        return;
    }

    VLOG_DBG("subsystem %s: executing write operation to device %s",
        subsystem->name,
        reg_op->device);
    /* now we write the data */
    op.direction = WRITE;
    switch (reg_op->register_size) {
        case 1:
            byte &= ~reg_op->bit_mask;
            byte |= hw_speed_val;
            break;
        case 2:
            word &= ~reg_op->bit_mask;
            word |= hw_speed_val;
            break;
        case 4:
            dword &= ~reg_op->bit_mask;
            dword |= hw_speed_val;
            break;
        default:
            VLOG_WARN("subsystem %s: invalid fan speed control register size (%d)",
                subsystem->name,
                reg_op->register_size);
            return;
    }

    i2c_debug(subsystem->name, device, cmds);
    rc = i2c_execute(yaml_handle, subsystem->name, device, cmds);

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
    i2c_op op;
    i2c_op *cmds[2];
    unsigned char byte = 0;
    unsigned short word = 0;
    unsigned long dword = 0;
    const YamlDevice *device;
    int rc;

    cmds[0] = &op;
    cmds[1] = NULL;

    rpm_op = fan->fan_speed;

    op.direction = READ;
    op.device = rpm_op->device;
    op.byte_count = rpm_op->register_size;
    op.set_register = false;
    op.register_address = rpm_op->register_address;
    switch (op.byte_count) {
        case 1:
            op.data = &byte;
            break;
        case 2:
            op.data = (unsigned char *)&word;
            break;
        case 4:
            op.data = (unsigned char *)&dword;
            break;
        default:
            VLOG_WARN("Invalid register size %d accessing %s-%s",
                op.byte_count,
                subsystem_name,
                fan->name);
            op.byte_count = 1;
            op.data = &byte;
            break;
    }
    op.negative_polarity = false;

    device = yaml_find_device(yaml_handle, subsystem_name, rpm_op->device);

    i2c_debug(subsystem_name, device, cmds);
    rc = i2c_execute(yaml_handle, subsystem_name, device, cmds);

    if (rc != 0) {
        VLOG_WARN("subsystem %s: unable to read fan %s rpm (%d)",
            subsystem_name,
            fan->name,
            rc);
        return(0);
    }

    switch (op.byte_count) {
        case 1:
        default:
            VLOG_DBG("speed data is %02x", byte);
            return (byte & (rpm_op->bit_mask));
            break;
        case 2:
            VLOG_DBG("speed data is %04x", word);
            return (word & (rpm_op->bit_mask));
            break;
        case 4:
            VLOG_DBG("speed data is %08lx", dword);
            return (dword & (rpm_op->bit_mask));
            break;
    }
}

static enum fanstatus
fand_read_status(const char *subsystem_name, const YamlFan *fan)
{
    i2c_bit_op *status_op;
    i2c_op op;
    i2c_op *cmds[2];
    unsigned char byte = 0;
    unsigned short word = 0;
    unsigned long dword = 0;
    const YamlDevice *device;
    int rc;
    int value;

    cmds[0] = &op;
    cmds[1] = NULL;

    status_op = fan->fan_fault;

    op.direction = READ;
    op.device = status_op->device;
    op.byte_count = status_op->register_size;
    op.set_register = false;
    op.register_address = status_op->register_address;
    switch (op.byte_count) {
        case 1:
            op.data = &byte;
            break;
        case 2:
            op.data = (unsigned char *)&word;
            break;
        case 4:
            op.data = (unsigned char *)&dword;
            break;
        default:
            VLOG_WARN("Invalid register size %d accessing %s-%s",
                op.byte_count,
                subsystem_name,
                fan->name);
            op.byte_count = 1;
            op.data = &byte;
            break;
    }
    op.negative_polarity = false;

    device = yaml_find_device(yaml_handle, subsystem_name, status_op->device);

    i2c_debug(subsystem_name, device, cmds);
    rc = i2c_execute(yaml_handle, subsystem_name, device, cmds);

    if (rc != 0) {
        VLOG_WARN("subsystem %s: unable to read fan %s status (%d)",
            subsystem_name,
            fan->name,
            rc);
        return(0);
    }

    switch (op.byte_count) {
        case 1:
        default:
            VLOG_DBG("status data is %02x", byte);
            value = (byte & (status_op->bit_mask));
            break;
        case 2:
            VLOG_DBG("status data is %04x", word);
            value = (word & (status_op->bit_mask));
            break;
        case 4:
            VLOG_DBG("status data is %08lx", dword);
            value = (dword & (status_op->bit_mask));
            break;
    }
    VLOG_DBG("status is %08x (%08x)", value, status_op->bit_mask);

    if (status_op->negative_polarity) {
        value = (value == 0 ? 1 : 0);
        VLOG_DBG("status is reversed %08x", value);
    }

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
    i2c_op op;
    i2c_op *cmds[2];
    unsigned char byte = 0;
    unsigned short word = 0;
    unsigned long dword = 0;
    const YamlDevice *device;
    int rc;
    int value;

    cmds[0] = &op;
    cmds[1] = NULL;

    direction_op = fru->fan_direction_detect;

    op.direction = READ;
    op.device = direction_op->device;
    op.byte_count = direction_op->register_size;
    op.set_register = false;
    op.register_address = direction_op->register_address;
    switch (op.byte_count) {
        case 1:
            op.data = &byte;
            break;
        case 2:
            op.data = (unsigned char *)&word;
            break;
        case 4:
            op.data = (unsigned char *)&dword;
            break;
        default:
            VLOG_WARN("Invalid register size %d accessing fan fru %s-%d",
                op.byte_count,
                subsystem_name,
                fru->number);
            op.byte_count = 1;
            op.data = &byte;
            break;
    }
    op.negative_polarity = false;

    device = yaml_find_device(yaml_handle, subsystem_name, direction_op->device);

    i2c_debug(subsystem_name, device, cmds);
    rc = i2c_execute(yaml_handle, subsystem_name, device, cmds);

    if (rc != 0) {
        VLOG_WARN("subsystem %s: unable to read fan fru %d direction (%d)",
            subsystem_name,
            fru->number,
            rc);
        return(FAND_DIRECTION_F2B);
    }

    switch (op.byte_count) {
        case 1:
        default:
            VLOG_DBG("direction data is %02x", byte);
            log_event("FAN_DIRECTION", EV_KV("value", "%02x", byte));
            value = (byte & (direction_op->bit_mask));
            break;
        case 2:
            VLOG_DBG("direction data is %04x", word);
            log_event("FAN_DIRECTION", EV_KV("value", "%04x", byte));
            value = (word & (direction_op->bit_mask));
            break;
        case 4:
            VLOG_DBG("direction data is %08lx", dword);
            log_event("FAN_DIRECTION", EV_KV("value", "%08lx", byte));
            value = (dword & (direction_op->bit_mask));
            break;
    }
    VLOG_DBG("direction is %08x (%08x)", value, direction_op->bit_mask);
    log_event("FAN_DIRECTION", EV_KV("value", "%08x", value));

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
