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
 * @defgroup ops-fand Fan Daemon
 * This module is the platform daemon that processess and manages fan FRUs
 * for all subsystems in the switch that have fans.
 *
 * @file
 * Header for platform fan daemon
 *
 * @defgroup fand_public Public Interface
 * Public API for the platform fan daemon
 *
 * The platform fan daemon is responsible for managing and reporting status
 * for fan FRUs in any subsystem that has fan FRUs that can be managed or
 * reported.
 *
 * @{
 *
 * Public APIs
 *
 * Command line options:
 *
 *     usage: ops-fand [OPTIONS] [DATABASE]
 *     where DATABASE is a socket on which ovsdb-server is listening
 *           (default: "unix:/var/run/openvswitch/db.sock").
 *
 *     Active DATABASE connection methods:
 *          tcp:IP:PORT             PORT at remote IP
 *          ssl:IP:PORT             SSL PORT at remote IP
 *          unix:FILE               Unix domain socket named FILE
 *     PKI configuration (required to use SSL):
 *          -p, --private-key=FILE  file with private key
 *          -c, --certificate=FILE  file with certificate for private key
 *          -C, --ca-cert=FILE      file with peer CA certificate
 *          --bootstrap-ca-cert=FILE  file with peer CA certificate to read or create
 *
 *     Daemon options:
 *          --detach                run in background as daemon
 *          --no-chdir              do not chdir to '/'
 *          --pidfile[=FILE]        create pidfile (default: /var/run/openvswitch/ops-fand.pid)
 *          --overwrite-pidfile     with --pidfile, start even if already running
 *
 *     Logging options:
 *          -vSPEC, --verbose=SPEC   set logging levels
 *          -v, --verbose            set maximum verbosity level
 *          --log-file[=FILE]        enable logging to specified FILE
 *                                  (default: /var/log/openvswitch/ops-fand.log)
 *          --syslog-target=HOST:PORT  also send syslog msgs to HOST:PORT via UDP
 *
 *     Other options:
 *          --unixctl=SOCKET        override default control socket name
 *          -h, --help              display this help message
 *          -V, --version           display version information
 *
 *
 * ovs-apptcl options:
 *
 *      Support dump: ovs-appctl -t ops-fand ops-fand/dump
 *
 *
 * OVSDB elements usage
 *
 *     Creation: The following rows/cols are created by ops-fand
 *               rows in Fan table
 *               Fan:name
 *               Fan:speed
 *               Fan:direction
 *               Fan:rpm
 *               Fan:status
 *
 *     Written: The following cols are written by ops-fand
 *              Fan:speed
 *              Fan:direction
 *              Fan:rpm
 *              Fan:status
 *              daemon["ops-fand"]:cur_hw
 *              subsystem:fans
 *
 *     Read: The following cols are read by ledd
 *           subsystem:name
 *           subsystem:hw_desc_dir
 *           subsystem:other_config
 *           subsystem:temp_sensors
 *
 * Linux Files:
 *
 *     The following files are written by ops-fand
 *           /var/run/openvswitch/ops-fand.pid: Process ID for the ops-fand daemon
 *           /var/run/openvswitch/ops-fand.<pid>.ctl: unixctl socket for the ops-fand daemon
 *
 *
 * @}
 ***************************************************************************/

#ifndef _FAND_LOCL_H_
#define _FAND_LOCL_H_

#include <stdbool.h>
#include "shash.h"
#include "fanspeed.h"
#include "fanstatus.h"
#include "config-yaml.h"

/* define a local structure to hold subsystem-related data,
   including the fan speed override value */
struct locl_subsystem {
    char *name;
    bool marked;
    bool valid;
    struct locl_subsystem *parent_subsystem;
    enum fanspeed fan_speed;      /* from tempd results */
    enum fanspeed fan_speed_override; /* as configured by user */
    enum fanspeed speed;          /* result of fan_speed, fan_speed_override */
    int multiplier;               /* from fans.yaml info */
    struct shash subsystem_fans;  /* struct locl_fan */
};

struct locl_fan {
    char *name;
    struct locl_subsystem *subsystem;
    const YamlFan *yaml_fan;
    enum fanspeed speed;
    const char *direction;
    int rpm;
    enum fanstatus status;
};

#endif /* _FAND_LOCL_H_ */
