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
 * Header file for fan direction functions.
 ***************************************************************************/

#ifndef _FANDIRECTION_H_
#define _FANDIRECTION_H_

/* enums for fan direction settings/status */
enum fandirection
{
    FAND_DIRECTION_F2B = 0,
    FAND_DIRECTION_B2F = 1
};

/* convert string to fandirection enum */
enum fandirection fan_direction_string_to_enum(const char *name);
/* convert fandirection enum to string */
const char *fan_direction_enum_to_string(enum fandirection fan_direction);

#endif  /* _FANDIRECTION_H_ */
