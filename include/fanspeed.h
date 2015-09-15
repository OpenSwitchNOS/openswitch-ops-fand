/*
 *  Copyright (C) 2015 Hewlett-Packard Development Company, L.P.
 *  All Rights Reserved.
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
 * Header file for fan speed functions.
 ***************************************************************************/

#ifndef _FANSPEED_H_
#define _FANSPEED_H_

/* enums for fan speed settings
   this is the override value, with -1 meaning that no
   override has been specified, and that ops-fand should control
   fan speed based on thermal sensor status in the subsystem */
enum fanspeed
{
    FAND_SPEED_NONE = -1,
    FAND_SPEED_SLOW = 0,
    FAND_SPEED_NORMAL = 1,
    FAND_SPEED_MEDIUM = 2,
    FAND_SPEED_FAST = 3,
    FAND_SPEED_MAX = 4
};

/* convert override string to fanspeed enum */
enum fanspeed fan_speed_string_to_enum(const char *name);
const char *fan_speed_enum_to_string(enum fanspeed fan_speed);

#endif  /* _FANSPEED_H_ */
