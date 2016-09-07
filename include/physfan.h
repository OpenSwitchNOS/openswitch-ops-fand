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
 * @ingroup ops-fand
 *
 * @file
 * Header file for hardware access plugin API.
 ***************************************************************************/

#ifndef _PHYSFAN_H_
#define _PHYSFAN_H_

#include "config-yaml.h"
#include "fandirection.h"
#include "fand-locl.h"

struct fand_subsystem_class {
    /**
     * Allocation of fan subsystem on adding to ovsdb. Implementation should
     * define its own struct that contains parent struct locl_subsystem, and
     * return pointer to parent.
     *
     * @return pointer to allocated subsystem.
     */
    struct locl_subsystem *(*fand_subsystem_alloc)(void);

    /**
     * Construction of fan subsystem on adding to ovsdb. Implementation should
     * initialize all fields in derived structure from locl_subsystem.
     *
     * @param[out] subsystem - pointer to subsystem.
     *
     * @return 0     on success.
     * @return errno on failure.
     */
    int (*fand_subsystem_construct)(struct locl_subsystem *subsystem);

    /**
     * Destruction of fan subsystem on removing from ovsdb. Implementation
     * should deinitialize all fields in derived structure from locl_subsystem.
     *
     * @param[in] subsystem - pointer to subsystem.
     */
    void (*fand_subsystem_destruct)(struct locl_subsystem *subsystem);

    /**
     * Deallocation of fan subsystem on removing from ovsdb. Implementation
     * should free memory from derived structure.
     *
     * @param[in] subsystem - pointer to subsystem.
     */
    void (*fand_subsystem_dealloc)(struct locl_subsystem *subsystem);

    /**
     * Set global led state.
     *
     * @param[in] subsystem     - pointer to subsystem.
     * @param[in] state   - state value to set.
     * @param[in] is_good - whether state is good or bad.
     *
     * @return 0     on success.
     * @return errno on failure.
     */
    int (*fand_subsystem_led_state_set)(const struct locl_subsystem *subsystem,
                                        enum fanstatus state);
};

struct fand_fan_class {
    /**
     * Allocation of fan. Implementation should define its own struct that
     * contains parent struct locl_subsystem, and return pointer to parent.
     *
     * @return pointer to allocated fan.
     */
    struct locl_fan *(*fand_fan_alloc)(void);

    /**
     * Construction of fan fan on adding to ovsdb. Implementation should
     * initialize all fields in derived structure from locl_fan.
     *
     * @param[out] fan - pointer to fan.
     *
     * @return 0     on success.
     * @return errno on failure.
     */
    int (*fand_fan_construct)(struct locl_fan *fan);

    /**
     * Destruction of fan fan. Implementation should deinitialize all fields in
     * derived structure from locl_fan.
     *
     * @param[in] fan - pointer to fan.
     */
     void (*fand_fan_destruct)(struct locl_fan *fan);

    /**
     * Deallocation of fan. Implementation should free memory from derived
     * structure.
     *
     * @param[in] fan - pointer to fan.
     */
    void (*fand_fan_dealloc)(struct locl_fan *fan);

    /**
     * Get fan speed.
     *
     * @param[in] fan - pointer to fan.
     * @param[out] speed - pointer to speed variable.
     *
     * @return 0     on success.
     * @return errno on failure.
     */
    int (*fand_speed_get)(const struct locl_fan *fan, enum fanspeed *speed);

    /**
     * Set fan speed.
     *
     * @param[in] fan - pointer to fan.
     * @param[in] speed - speed value.
     *
     * @return 0     on success.
     * @return errno on failure.
     */
    int (*fand_speed_set)(struct locl_fan *fan, enum fanspeed speed);

    /**
     * Get fan status.
     *
     * @param[in] fan - pointer to fan.
     * @param[out] speed - pointer to status variable.
     *
     * @return 0     on success.
     * @return errno on failure.
     */
    int (*fand_status_get)(const struct locl_fan *fan, enum fanstatus *status);

    /**
     * Get fan direction. Optional.
     *
     * @param[in] fan - pointer to fan.
     * @param[out] dir - pointer to direction variable.
     *
     * @return 0     on success.
     * @return errno on failure.
     */
    int (*fand_direction_get)(const struct locl_fan *fan,
                              enum fandirection *dir);

    /**
     * Set fan direction. Optional.
     *
     * @param[in] fan - pointer to fan.
     * @param[in] dir - direction value.
     *
     * @return 0     on success.
     * @return errno on failure.
     */
    int (*fand_direction_set)(struct locl_fan *fan, enum fandirection speed);

    /**
     * Get fan rpm. Optional.
     *
     * @param[in] fan - pointer to fan.
     * @param[out] rpm - pointer to rpm variable.
     *
     * @return 0     on success.
     * @return errno on failure.
     */
    int (*fand_rpm_get)(const struct locl_fan *fan, uint32_t *rpm);

    /**
     * Set fan rpm. Optional.
     *
     * @param[in] fan - pointer to fan.
     * @param[out] rpm - rpm value.
     *
     * @return 0     on success.
     * @return errno on failure.
     */
    int (*fand_rpm_set)(const struct locl_fan *fan, uint32_t rpm);
};

struct fand_fru_class {
    /**
     * Allocation of fan fru on adding to ovsdb. Implementation should
     * define its own struct that contains parent struct locl_fru, and
     * return pointer to parent.
     *
     * @return pointer to allocated fru.
     */
    struct locl_fru *(*fand_fru_alloc)(void);

    /**
     * Construction of fan fru on adding to ovsdb. Implementation should
     * initialize all fields in derived structure from locl_fru.
     *
     * @param[out] fru - pointer to fru.
     *
     * @return 0     on success.
     * @return errno on failure.
     */
    int (*fand_fru_construct)(struct locl_fru *fru);

    /**
     * Destruction of fan fru on removing from ovsdb. Implementation
     * should deinitialize all fields in derived structure from locl_fru.
     *
     * @param[in] fru - pointer to fru.
     */
    void (*fand_fru_destruct)(struct locl_fru *fru);

    /**
     * Deallocation of fan fru on removing from ovsdb. Implementation
     * should free memory from derived structure.
     *
     * @param[in] fru - pointer to fru.
     */
    void (*fand_fru_dealloc)(struct locl_fru *fru);

    /**
     * Set led state.
     *
     * @param[in] fru     - pointer to fru.
     * @param[in] state   - state value to set.
     * @param[in] is_good - whether state is good or bad.
     *
     * @return 0     on success.
     * @return errno on failure.
     */
    int (*fand_fru_led_state_set)(const struct locl_fru *fru,
                                  enum fanstatus state);

    /**
     * Set get presence of FRU;
     *
     * @param[in] fru     - pointer to fru.
     * @param[in] present - pointer whether FRU is inserted or not.
     *
     * @return 0     on success.
     * @return errno on failure.
     */
    int (*fand_fru_presence_get)(const struct locl_fru *fru, bool *present);
};

#endif /* _PHYSFAN_H */
