/*
 * Copyright Mellanox Technologies, Ltd. 2001-2016.
 * This software product is licensed under Apache version 2, as detailed in
 * the COPYING file.
 */

#include <physfan.h>
#include <fanspeed.h>
#include <errno.h>
#include "config-yaml.h"
#include "openvswitch/vlog.h"

#define HWDESC_FILE_LINK "/etc/openswitch/hwdesc"

VLOG_DEFINE_THIS_MODULE(yaml_i2c_plugin);

struct i2c_subsystem {
    struct locl_subsystem subsystem;
    const YamlFanInfo    *fan_info;
    const YamlDevice     *device;
    YamlConfigHandle      yaml_handle;
    struct shash          fan_data;
    enum fanspeed         speed;
};
struct i2c_fan {
    struct locl_fan fan;
    YamlFan        *yaml_fan;
};

static struct locl_subsystem * __subsystem_alloc(void);
static int __subsystem_construct(struct locl_subsystem *);
static void __subsystem_destruct(struct locl_subsystem *);
static void __subsystem_dealloc(struct locl_subsystem *);
static int __enumerate_devices(const struct locl_subsystem *, struct sset *);

static struct locl_fan * __fan_alloc(void);
static int __fan_construct(struct locl_fan *);
static void __fan_destruct(struct locl_fan *);
static void __fan_dealloc(struct locl_fan *);
static int __speed_get(const struct locl_fan *, enum fanspeed *);
static int __speed_set(struct locl_fan *, enum fanspeed);
static int __status_get(const struct locl_fan *, enum fanstatus *);
static int __direction_get(const struct locl_fan *, enum fandirection *);
static int __direction_set(struct locl_fan *, enum fandirection);
static int __rpm_get(const struct locl_fan *, uint32_t *);
static int __rpm_set(const struct locl_fan *, uint32_t);

static inline struct i2c_subsystem * __i2c_subsystem_cast(const struct locl_subsystem *);
static inline struct i2c_fan * __i2c_fan_cast(const struct locl_fan *);
static const YamlFanFru * fan_fru_get(struct i2c_subsystem *, const YamlFan *);
static int fand_read_fan_fru_direction(const char *, const char *,
                                       YamlConfigHandle,
                                       const YamlFanFru *, const YamlFanInfo *,
                                       enum fandirection *);

static const struct fand_subsystem_class fand_sybsystem = {
    .fand_subsystem_alloc = __subsystem_alloc,
    .fand_subsystem_construct = __subsystem_construct,
    .fand_subsystem_destruct = __subsystem_destruct,
    .fand_subsystem_dealloc = __subsystem_dealloc,
    .fand_enumerate_devices = __enumerate_devices
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
    .fand_direction_set = __direction_set,
    .fand_rpm_get = __rpm_get,
    .fand_rpm_set = __rpm_set
};

/**
 * Get fand subsystem class.
 */
const struct fand_subsystem_class *
fand_subsystem_class_get(void)
{
    VLOG_INFO(__FUNCTION__);
    return &fand_sybsystem;
}

/**
 * Get fand fan class.
 */
const struct fand_fan_class *
fand_fan_class_get(void)
{
    VLOG_INFO(__FUNCTION__);
    return &fand_fan;
}

/**
 * Initialize ops-fand platform support plugin. Must be implemented in plugin.
 */
void
fand_plugin_init(void)
{
    VLOG_INFO("Initializing yaml i2c plugin");
}

/**
 * Deinitialize ops-fand platform support plugin. Must be implemented in
 * plugin.
 */
void
fand_plugin_deinit(void)
{
    VLOG_INFO("De-Initializing yaml i2c plugin");
}

static struct locl_subsystem *
__subsystem_alloc(void)
{
    struct i2c_subsystem *result = NULL;

    VLOG_INFO("Allocating new subsystem of yaml i2c plugin");

    result = (struct i2c_subsystem *)xzalloc(sizeof(*result));

    return &result->subsystem;
}

static int
__subsystem_construct(struct locl_subsystem *subsystem_)
{
    struct i2c_subsystem *subsystem = __i2c_subsystem_cast(subsystem_);
    int                rc = 0;
    char const* const  dir = HWDESC_FILE_LINK;
    const YamlFanInfo *fan_info = NULL;

    VLOG_INFO("Adding new subsystem %s", subsystem_->name);

    shash_init(&subsystem->fan_data);

    subsystem->yaml_handle = yaml_new_config_handle();
    if (!subsystem->yaml_handle) {
        VLOG_ERR("Error creating yaml handler for subsystem %s",
                 subsystem_->name);
        return ENOMEM;
    }

    rc = yaml_add_subsystem(subsystem->yaml_handle, subsystem_->name, dir);
    if (rc != 0) {
        VLOG_ERR("Error getting h/w description information for subsystem %s",
                 subsystem_->name);
        return EIO;
    }

    rc = yaml_parse_devices(subsystem->yaml_handle, subsystem_->name);
    if (rc != 0) {
        VLOG_ERR("Unable to parse subsystem %s devices file (in %s)",
                 subsystem_->name, dir);
        return EIO;
    }

    rc = yaml_parse_fans(subsystem->yaml_handle, subsystem_->name);
    if (rc != 0) {
        VLOG_ERR("Unable to parse subsystem %s fan file (in %s)",
                 subsystem_->name, dir);
        return EIO;
    }

    subsystem->fan_info = yaml_get_fan_info(subsystem->yaml_handle, subsystem_->name);
    if (!subsystem->fan_info) {
        VLOG_ERR("Subsystem %s has no fan info", subsystem_->name);
        return EIO;
    }

    subsystem->device = yaml_find_device(subsystem->yaml_handle, subsystem_->name,
                        fan_info->fan_speed_control->device);
    if (!subsystem->device) {
        VLOG_ERR("Unable to parse subsystem %s devices file (in %s)",
                 subsystem_->name, dir);
        return EIO;
    }

    return 0;
}

static void
__subsystem_destruct(struct locl_subsystem *subsystem_)
{
    /*TODO later: closing yaml handle return by yaml_new_config_handle().
     *  No Yaml API exits now. */
    VLOG_INFO("Destructing subsystem %s", subsystem_->name);
}

static void
__subsystem_dealloc(struct locl_subsystem *subsystem_)
{
    struct i2c_subsystem *subsystem = __i2c_subsystem_cast(subsystem_);

    VLOG_INFO("Destructing subsystem %s", subsystem_->name);

    free(subsystem);
}

static int
__enumerate_devices(const struct locl_subsystem *subsystem_, struct sset *fans)
{
    int               rc = 0;
    int               idx = 0;
    int               fan_idx = 0;
    int               fan_fru_count = 0;
    const YamlFanFru *fan_fru = NULL;
    struct i2c_subsystem *subsystem = __i2c_subsystem_cast(subsystem_);

    VLOG_INFO("Enumerating devices of subsystem %s", subsystem_->name);

    fan_fru_count = yaml_get_fan_fru_count(subsystem->yaml_handle, subsystem_->name);
    if (fan_fru_count <= 0) {
        VLOG_ERR("Failed to enumerating devices of subsystem %s",
                 subsystem_->name);
        return ENODATA;
    }
    VLOG_DBG("There are %d fan FRUS in subsystem %s",
             fan_fru_count,
             subsystem_->name);

    for (idx = 0; idx < fan_fru_count; idx++) {
        fan_fru = yaml_get_fan_fru(subsystem->yaml_handle, subsystem_->name, idx);
        if (NULL == fan_fru) {
            VLOG_ERR("Failed to get fans of FRU %d while enumerating devices "
                     "of subsystem %s", idx, subsystem_->name);
            return ENODATA;
        }
        /* each FanFru has one or more fans */
        for (fan_idx = 0; NULL != fan_fru->fans[fan_idx]; fan_idx++) {
            if (!sset_add(fans, fan_fru->fans[fan_idx]->name)) {
                /* Fan with the same name already exists */
                VLOG_WARN("Fan duplicate %s in FRU %d while enumerating"
                          " devices of subsystem %s",
                          fan_fru->fans[fan_idx]->name,
                          idx, subsystem_->name);
                rc = EEXIST;
            } else {
                shash_add(&subsystem->fan_data, fan_fru->fans[fan_idx]->name,
                          (void*)fan_fru->fans[fan_idx]);
            }
        }
    }

    return rc;
}

static struct locl_fan *
__fan_alloc(void)
{
    struct i2c_fan *new_fan = NULL;

    VLOG_INFO("Allocating new fan");
    new_fan = (struct i2c_fan  *)xzalloc(sizeof(struct i2c_fan ));

    return &new_fan->fan;
}

static int
__fan_construct(struct locl_fan *fan_)
{
    struct i2c_fan *fan = __i2c_fan_cast(fan_);
    struct i2c_subsystem *subsystem = __i2c_subsystem_cast(fan->fan.subsystem);
    YamlFan *yamlFan = shash_find_data(&subsystem->fan_data, fan_->name);

    VLOG_INFO("Subsystem %s, fan %s: Constructing new fan",
              subsystem->subsystem.name, fan_->name);

    fan->yaml_fan = yamlFan;

    return 0;
}

static void
__fan_destruct(struct locl_fan *fan_)
{
    struct i2c_fan *fan= __i2c_fan_cast(fan_);
    struct i2c_subsystem *subsystem = __i2c_subsystem_cast(fan->fan.subsystem);

    VLOG_INFO("Subsystem %s, fan %s: de-structing fan",
              subsystem->subsystem.name, fan_->name);
    shash_delete(&subsystem->fan_data, shash_find(&subsystem->fan_data, fan_->name));
}

static void
__fan_dealloc(struct locl_fan *fan_)
{
    struct i2c_fan *fan= __i2c_fan_cast(fan_);
    struct i2c_subsystem *subsystem = __i2c_subsystem_cast(fan->fan.subsystem);

    VLOG_INFO("Subsystem %s, fan %s: de-allocating fan",
              subsystem->subsystem.name, fan_->name);
    free(fan);
}

static int
__speed_get(const struct locl_fan *fan_, enum fanspeed *speed)
{
    struct i2c_fan *fan= __i2c_fan_cast(fan_);
    struct i2c_subsystem *subsystem = __i2c_subsystem_cast(fan->fan.subsystem);

    VLOG_INFO("Subsystem %s, fan %s: getting speed %s",
              subsystem->subsystem.name, fan_->name,
              fan_speed_enum_to_string(subsystem->subsystem.speed));

    return *speed = subsystem->subsystem.speed;
}

static int
__speed_set(struct locl_fan *fan_, enum fanspeed speed)
{
    struct i2c_fan *fan= __i2c_fan_cast(fan_);
    struct i2c_subsystem *subsystem = __i2c_subsystem_cast(fan->fan.subsystem);
    const YamlFanInfo *const fan_info = subsystem->fan_info;
    const YamlDevice *const  device = subsystem->device;
    char *const        subsys_name = subsystem->subsystem.name;
    unsigned char      hw_speed_val = 0;
    unsigned char      byte = 0;
    unsigned short     word = 0;
    unsigned long      dword = 0;
    int                rc = 0;
    i2c_bit_op        *reg_op = NULL;
    i2c_op             op = {};
    i2c_op            *cmds[2] = { };

    VLOG_INFO("Subsystem %s, fan %s: setting speed %s",
              subsystem->subsystem.name, fan_->name,
              fan_speed_enum_to_string(speed));

    /* set the speed value for record-keeping */
    subsystem->speed = speed;

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

    VLOG_DBG("Subsystem %s, fan %s:  executing read operation to device %s",
             subsys_name, fan_->name, reg_op->device);

    /* we're going to do a read/modify/write: read the data */
    op.direction = READ;
    op.device = reg_op->device;
    op.register_address = reg_op->register_address;
    op.byte_count = reg_op->register_size;
    switch (reg_op->register_size) {
    case 1:
        op.data = (unsigned char*)&byte;
        break;

    case 2:
        op.data = (unsigned char*)&word;
        break;

    case 4:
        op.data = (unsigned char*)&dword;
        break;

    default:
        VLOG_ERR("Subsystem %s, fan %s: invalid fan speed control "
                 "register size %d", subsys_name, fan_->name,
                 reg_op->register_size);
        return EINVAL;
    }
    op.set_register = false;
    op.negative_polarity = false;
    cmds[0] = &op;
    cmds[1] = NULL;

    rc = i2c_execute(subsystem->yaml_handle, subsys_name, device, cmds);
    if (rc != 0) {
        VLOG_ERR("Subsystem %s, fan %s: unable to read fan speed control "
                 "register, rc=%d", subsys_name, fan_->name, rc);
        return EIO;
    }

    VLOG_DBG("Subsystem %s, fan %s: executing write operation to device %s",
             fan_->name, subsys_name, reg_op->device);
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
        VLOG_ERR("Subsystem %s, fan %s: invalid fan speed control "
                 "register size (%d)", subsys_name, fan_->name,
                 reg_op->register_size);
        return EINVAL;
    }

    rc = i2c_execute(subsystem->yaml_handle, subsys_name, device, cmds);
    if (rc != 0) {
        VLOG_ERR("Subsystem %s, fan %s: unable to set fan speed control "
                 "register, rc=%d)", subsys_name, fan_->name, rc);
        return EIO;
    }

    return 0;
}

static int
__status_get(const struct locl_fan *fan_, enum fanstatus *status)
{
    struct i2c_fan *fan = __i2c_fan_cast(fan_);
    struct i2c_subsystem *subsystem = __i2c_subsystem_cast(fan->fan.subsystem);
    const YamlDevice *const device = subsystem->device;
    const YamlFan *const    yamlFan = fan->yaml_fan;
    char *const       subsystem_name = subsystem->subsystem.name;
    i2c_bit_op       *status_op = NULL;
    i2c_op            op = { };
    i2c_op           *cmds[2] = {&op, NULL};
    unsigned char     byte = 0;
    unsigned short    word = 0;
    unsigned long     dword = 0;
    int               rc = 0;
    int               value = 0;

    VLOG_INFO("Subsystem %s, fan %s: getting status",
              subsystem->subsystem.name, fan_->name);

    status_op = yamlFan->fan_fault;
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
        op.data = (unsigned char*)&word;
        break;

    case 4:
        op.data = (unsigned char*)&dword;
        break;

    default:
        VLOG_WARN("Subsystem %s, fan %s: invalid register size %d",
                  subsystem_name,
                  fan_->name,
                  op.byte_count);
        op.byte_count = 1;
        op.data = &byte;
        break;
    }
    op.negative_polarity = false;

    rc = i2c_execute(subsystem->yaml_handle, subsystem_name, device, cmds);
    if (rc != 0) {
        VLOG_ERR("Subsystem %s, fan %s: unable to read fan status rc=%d",
                 subsystem_name,
                 fan_->name,
                 rc);
        return EIO;
    }

    switch (op.byte_count) {
    case 1:
    default:
        VLOG_DBG("Subsystem %s, fan %s: status data is %02x",
                 subsystem_name, fan_->name, byte);
        value = (byte & (status_op->bit_mask));
        break;

    case 2:
        VLOG_DBG("Subsystem %s, fan %s: status data is %04x",
                 subsystem_name, fan_->name, word);
        value = (word & (status_op->bit_mask));
        break;

    case 4:
        VLOG_DBG("Subsystem %s fan %s: status data is %08lx",
                 subsystem_name, fan_->name, dword);
        value = (dword & (status_op->bit_mask));
        break;
    }
    VLOG_DBG("Subsystem %s, fan %s: status is %08x (%08x)",
             subsystem_name, fan_->name,
             value, status_op->bit_mask);

    if (status_op->negative_polarity) {
        value = (value == 0 ? 1 : 0);
        VLOG_DBG("Subsystem %s, fan %s: status is reversed %08x",
                 subsystem_name, fan_->name, value);
    }

    if (value != 0) {
        VLOG_ERR("Subsystem %s, fan %s: error while getting fan status",
                   subsystem_name, fan_->name);
        *status = FAND_STATUS_FAULT;
        return EFAULT;
    } else {
        *status = FAND_STATUS_OK;
    }

    return 0;
}

static int
__direction_get(const struct locl_fan *fan_, enum fandirection *dir)
{
    struct i2c_fan *fan = __i2c_fan_cast(fan_);
    struct i2c_subsystem *subsystem = __i2c_subsystem_cast(fan->fan.subsystem);
    char *const        subsystem_name = subsystem->subsystem.name;
    const YamlFanFru *const  fan_fru = fan_fru_get(subsystem, fan->yaml_fan);

    VLOG_INFO("Subsystem %s, fan %s: getting direction",
              subsystem_name, fan_->name);

    if (!fan_fru) {
        VLOG_ERR("Subsystem %s, fan %s: failed to get fan FRU",
                   subsystem_name, fan_->name);
        return EFAULT;
    }

    if (!(fan_fru->fan_direction_detect &&
          !fand_read_fan_fru_direction(subsystem_name, fan_->name, subsystem->yaml_handle, fan_fru,
                                       subsystem->fan_info, dir))) {
        VLOG_ERR("Subsystem %s, fan %s: failed to get fan direction",
                 subsystem_name, fan_->name);
        return EFAULT;
    }

    return 0;
}

static int
__direction_set(struct locl_fan *fan, enum fandirection speed)
{
    VLOG_INFO(__FUNCTION__);
    return 0;
}

static int
__rpm_get(const struct locl_fan *fan_, uint32_t *rpm)
{
    struct i2c_fan *fan = __i2c_fan_cast(fan_);
    struct i2c_subsystem *subsystem = __i2c_subsystem_cast(fan->fan.subsystem);
    const YamlDevice *const device = subsystem->device;
    const YamlFan *const    yamlFan = fan->yaml_fan;
    char *const       subsystem_name = subsystem->subsystem.name;
    i2c_bit_op *const rpm_op = yamlFan->fan_speed;
    i2c_op            op = { };
    i2c_op           *cmds[2] = {&op, NULL};
    unsigned char     byte = 0;
    unsigned short    word = 0;
    unsigned long     dword = 0;
    int               rc = 0;

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
        op.data = (unsigned char*)&word;
        break;

    case 4:
        op.data = (unsigned char*)&dword;
        break;

    default:
        VLOG_WARN("Subsystem %s, fan %s: invalid register size %d",
                  subsystem_name, fan_->name, op.byte_count);
        op.byte_count = 1;
        op.data = &byte;
        break;
    }
    op.negative_polarity = false;

    rc = i2c_execute(subsystem->yaml_handle, subsystem_name, device, cmds);
    if (rc != 0) {
        VLOG_WARN("Subsystem %s: unable to read fan %s rpm, rc=%d",
                  subsystem_name, fan_->name, rc);
        return EIO;
    }

    switch (op.byte_count) {
    case 1:
    default:
        VLOG_DBG("speed data is %02x", byte);
        *rpm = (uint32_t)(byte & (rpm_op->bit_mask));
        break;

    case 2:
        VLOG_DBG("speed data is %04x", word);
        *rpm = (uint32_t)(word & (rpm_op->bit_mask));
        break;

    case 4:
        VLOG_DBG("speed data is %08lx", dword);
        *rpm = (uint32_t)(dword & (rpm_op->bit_mask));
        break;
    }

    return 0;
}

static int
__rpm_set(const struct locl_fan *fan, uint32_t rpm)
{
    VLOG_INFO(__FUNCTION__);
    return 0;
}

static inline struct i2c_subsystem *
__i2c_subsystem_cast(const struct locl_subsystem *locl_subsystem_)
{
    ovs_assert(locl_subsystem_);
    return CONTAINER_OF(locl_subsystem_, struct i2c_subsystem, subsystem);
}

static inline struct i2c_fan *
__i2c_fan_cast(const struct locl_fan*locl_fan_)
{
    ovs_assert(locl_fan_);
    return CONTAINER_OF(locl_fan_, struct i2c_fan, fan);
}

static const YamlFanFru *
fan_fru_get(struct i2c_subsystem *subsystem, const YamlFan *fan)
{
    int               idx = 0;
    int               count = 0;
    int               fan_idx = 0;
    const YamlFanFru *fru = NULL;

    count = yaml_get_fan_fru_count(subsystem->yaml_handle, subsystem->subsystem.name);

    for (; idx < count; idx++) {
        fru = yaml_get_fan_fru(subsystem->yaml_handle, subsystem->subsystem.name, idx);

        for (fan_idx = 0; fru->fans[fan_idx] != NULL; fan_idx++) {
            if (fan == fru->fans[fan_idx]) {
                return fru;
            }
        }
    }
    return NULL;
}

static int
fand_read_fan_fru_direction(const char        *subsystem_name,
                            const char        *fan_name,
                            YamlConfigHandle  yaml_handle,
                            const YamlFanFru  *fru,
                            const YamlFanInfo *info,
                            enum fandirection *dir)
{
    i2c_bit_op       *direction_op = fru->fan_direction_detect;
    i2c_op            op = { };
    i2c_op           *cmds[2] = {&op, NULL};
    unsigned char     byte = 0;
    unsigned short    word = 0;
    unsigned long     dword = 0;
    const YamlDevice *device = NULL;
    int               rc = 0;
    int               value = 0;

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
        op.data = (unsigned char*)&word;
        break;

    case 4:
        op.data = (unsigned char*)&dword;
        break;

    default:
        VLOG_WARN(
            "Subsystem %s, fan %s: invalid register size %d accessing fan fru %d",
            subsystem_name,
            fan_name,
            op.byte_count,
            fru->number);
        op.byte_count = 1;
        op.data = &byte;
        break;
    }
    op.negative_polarity = false;

    device =
        yaml_find_device(yaml_handle, subsystem_name, direction_op->device);

    rc = i2c_execute(yaml_handle, subsystem_name, device, cmds);

    if (rc != 0) {
        VLOG_ERR(
            "Subsystem %s, fan %s: unable to read fan fru %d direction rc=%d",
            subsystem_name,
            fan_name,
            fru->number,
            rc);
        return -1;
    }

    switch (op.byte_count) {
    case 1:
    default:
        VLOG_DBG("Subsystem %s, fan %s: direction data is %02x",
                 subsystem_name,
                 fan_name,
                 byte);
        value = (byte & (direction_op->bit_mask));
        break;

    case 2:
        VLOG_DBG("Subsystem %s, fan %s: direction data is %04x",
                 subsystem_name,
                 fan_name,
                 word);
        value = (word & (direction_op->bit_mask));
        break;

    case 4:
        VLOG_DBG("Subsystem %s, fan %s: direction data is %08lx",
                 subsystem_name,
                 fan_name,
                 dword);
        value = (dword & (direction_op->bit_mask));
        break;
    }
    VLOG_DBG("Subsystem %s, fan %s: direction is %08x (%08x)",
             subsystem_name,
             fan_name,
             value,
             direction_op->bit_mask);

    /* OPS_TODO: code assumption: the value is a single bit that indicates
     *  direction as either front-to-back or back-to-front. It would be better
     *  if we had an absolute value, but the i2c ops don't have bit shift values,
     *  so we can't do a direct comparison. */
    if (value != 0) {
        if (info->direction_values.f2b != 0) {
            *dir = FAND_DIRECTION_F2B;
        } else {
            *dir = FAND_DIRECTION_B2F;
        }
    } else {
        if (info->direction_values.f2b != 0) {
            *dir = FAND_DIRECTION_B2F;
        } else {
            *dir = FAND_DIRECTION_F2B;
        }
    }

    return 0;
}
