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
 * Header file for fan status functions.
 ***************************************************************************/

#ifndef _FANSTATUS_H_
#define _FANSTATUS_H_

/* enums for fan status */
enum fanstatus
{
    FAND_STATUS_UNINITIALIZED = 0,
    FAND_STATUS_OK = 1,
    FAND_STATUS_FAULT = 2
};

/* conversion functions */
enum fanstatus fan_status_string_to_enum(const char *name);
const char *fan_status_enum_to_string(enum fanstatus status);

#endif  /* _FANSTATUS_H_ */
