/*
 *  (c) Copyright 2015 Hewlett Packard Enterprise Development LP
 *  Copyright (c) 2008, 2009, 2010, 2011, 2012, 2013, 2014 Nicira, Inc.
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
 * Source file for the platform fan daemon
 ***************************************************************************/

#define _GNU_SOURCE
#include <errno.h>
#include <getopt.h>
#include <limits.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>

#include "config.h"
#include "command-line.h"
#include "compiler.h"
#include "daemon.h"
#include "dirs.h"
#include "dummy.h"
#include "fatal-signal.h"
#include "ovsdb-idl.h"
#include "poll-loop.h"
#include "simap.h"
#include "stream-ssl.h"
#include "stream.h"
#include "svec.h"
#include "timeval.h"
#include "unixctl.h"
#include "util.h"
#include "dynamic-string.h"
#include "openvswitch/vconn.h"
#include "openvswitch/vlog.h"
#include "vswitch-idl.h"
#include "coverage.h"

#include "config-yaml.h"

#include "fanspeed.h"
#include "fanstatus.h"
#include "physfan.h"
#include "fand-locl.h"
#include "eventlog.h"

#define FAN_POLL_INTERVAL   5    /* OPS_TODO: should this be configurable? */
                                 /*             or should it be vendor spec? */
                                 /*             making it fixed, for now... */

#define MSEC_PER_SEC        1000

#define NAME_IN_DAEMON_TABLE "ops-fand"

VLOG_DEFINE_THIS_MODULE(ops_fand);

COVERAGE_DEFINE(fand_reconfigure);

static struct ovsdb_idl *idl;

static unsigned int idl_seqno;

static unixctl_cb_func fand_unixctl_dump;

static bool cur_hw_set = false;

/* define a shash (string hash) to hold the subsystems (by name) */
struct shash subsystem_data;
/* define a shash (string hash) to hold the fans (by name) */
struct shash fan_data;

/* global yaml config handle */
YamlConfigHandle yaml_handle;

/* initialize the subsystem data (and the fan data) dictionaries */
static void
init_subsystems(void)
{
    shash_init(&subsystem_data);
    shash_init(&fan_data);
}

struct ovsrec_fan *
lookup_fan(const char *name)
{
    const struct ovsrec_fan *fan;

    OVSREC_FAN_FOR_EACH(fan, idl) {
        if (strcmp(fan->name, name) == 0) {
            return((struct ovsrec_fan *)fan);
        }
    }

    return(NULL);
}

/* create a new subsystem structure and add all the dependent ports
   as a side-effect, create all fans in the database */
static struct locl_subsystem *
add_subsystem(const struct ovsrec_subsystem *ovsrec_subsys)
{
    struct locl_subsystem *result;
    int rc;
    int total_fans;
    unsigned int idx;
    struct ovsdb_idl_txn *txn;
    struct ovsrec_fan **fan_array;
    int total_fan_idx;
    unsigned int fan_fru_count;
    const char *dir;
    int fan_idx;
    const YamlFanInfo *fan_info;
    const char *override;
    enum fanspeed override_value = FAND_SPEED_NONE;

    VLOG_DBG("Adding new subsystem %s", ovsrec_subsys->name);
    result = (struct locl_subsystem *)malloc(sizeof(struct locl_subsystem));
    memset(result, 0, sizeof(struct locl_subsystem));
    (void)shash_add(&subsystem_data, ovsrec_subsys->name, (void *)result);
    result->name = strdup(ovsrec_subsys->name);
    result->marked = false;
    result->valid = false;
    result->parent_subsystem = NULL;  /* OPS_TODO: find parent subsystem */
    shash_init(&result->subsystem_fans);
    override = smap_get(&ovsrec_subsys->other_config, "fan_speed_override");
    if (override != NULL) {
        override_value = fan_speed_string_to_enum(override);
    }
    result->fan_speed_override = override_value;

    /* OPS_TODO: could check to see if the temp sensors have been populated
       with data and use that for the sensor speed when initializing the
       fan_speed value. */
    result->fan_speed = FAND_SPEED_NORMAL;

    /* use a default if the hw_desc_dir has not been populated */
    dir = ovsrec_subsys->hw_desc_dir;

    if (dir == NULL || strlen(dir) == 0) {
        VLOG_ERR("No h/w description directory for subsystem %s",
                 ovsrec_subsys->name);
        return(NULL);
    }

    /* since this is a new subsystem, load all of the hardware description
       information about devices and fans (just for this subsystem).
       parse fan and device data for subsystem */
    rc = yaml_add_subsystem(yaml_handle, ovsrec_subsys->name, dir);

    if (rc != 0) {
        VLOG_ERR("Error getting h/w description information for subsystem %s",
                 ovsrec_subsys->name);
        return(NULL);
    }

    rc = yaml_parse_devices(yaml_handle, ovsrec_subsys->name);

    if (rc != 0) {
        VLOG_ERR("Unable to parse subsystem %s devices file (in %s)",
                 ovsrec_subsys->name, dir);
        return(NULL);
    }

    rc = yaml_parse_fans(yaml_handle, ovsrec_subsys->name);

    if (rc != 0) {
        VLOG_ERR("Unable to parse subsystem %s fan file (in %s)",
                 ovsrec_subsys->name, dir);
        return(NULL);
    }

    fan_info = yaml_get_fan_info(yaml_handle, ovsrec_subsys->name);

    if (fan_info == NULL) {
        VLOG_INFO("subsystem %s has no fan info", ovsrec_subsys->name);
        return(NULL);
    }

    result->multiplier = fan_info->fan_speed_multiplier;

    /* count the total fans in the subsystem */
    total_fans = 0;
    total_fan_idx = 0;

    fan_fru_count = yaml_get_fan_fru_count(yaml_handle, ovsrec_subsys->name);

    VLOG_DBG("There are %d fan FRUS in subsystem %s", fan_fru_count, ovsrec_subsys->name);

    if (fan_fru_count <= 0) {
        return(NULL);
    }

    result->valid = true;

    for (idx = 0; idx < fan_fru_count; idx++) {
        const YamlFanFru *fan_fru = yaml_get_fan_fru(yaml_handle, ovsrec_subsys->name, idx);
        /* each FanFru has one or more fans */
        for (fan_idx = 0; fan_fru->fans[fan_idx] != NULL; fan_idx++) {
            ++total_fans;
        }
    }

    fan_array = (struct ovsrec_fan **)malloc(total_fans * sizeof(struct ovsrec_fan *));
    memset(fan_array, 0, total_fans * sizeof(struct ovsrec_fan *));

    txn = ovsdb_idl_txn_create(idl);

    VLOG_DBG("There are %d total fans in subsystem %s", total_fans, ovsrec_subsys->name);
    log_event("FAN_COUNT", EV_KV("count", "%d", total_fans),
        EV_KV("subsystem", "%s", ovsrec_subsys->name ));

    /* TODO walk through fans and add them to DB */
    for (idx = 0; idx < fan_fru_count; idx++) {
        const YamlFanFru *fan_fru = yaml_get_fan_fru(yaml_handle, ovsrec_subsys->name, idx);

        /* each FanFru has one or more fans */
        for (fan_idx = 0; fan_fru->fans[fan_idx] != NULL; fan_idx++) {
            struct ovsrec_fan *ovs_fan;
            char *fan_name = NULL;
            const YamlFan *fan = fan_fru->fans[fan_idx];
            struct locl_fan *new_fan;
            VLOG_DBG("Adding fan %s in subsystem %s",
                fan->name,
                ovsrec_subsys->name);

            asprintf(&fan_name, "%s-%s", ovsrec_subsys->name, fan->name);
            new_fan = (struct locl_fan *)malloc(sizeof(struct locl_fan));
            new_fan->name = fan_name;
            new_fan->subsystem = result;
            new_fan->yaml_fan = fan;

            shash_add(&result->subsystem_fans, fan_name, (void *)new_fan);
            shash_add(&fan_data, fan_name, (void *)new_fan);

            /* look for existing Fan rows */
            ovs_fan = lookup_fan(fan_name);

            if (ovs_fan == NULL) {
                ovs_fan = ovsrec_fan_insert(txn);
            }

            ovsrec_fan_set_name(ovs_fan, fan_name);
            ovsrec_fan_set_status(ovs_fan,
                fan_status_enum_to_string(FAND_STATUS_UNINITIALIZED));
            /* OPS_TODO: these have to be set, but "f2b" and "normal"
               may not be the right values for defaults. */
            ovsrec_fan_set_direction(ovs_fan, "f2b");
            ovsrec_fan_set_speed(ovs_fan, fan_speed_enum_to_string(FAND_SPEED_NORMAL));

            fan_array[total_fan_idx++] = ovs_fan;
        }
    }

    ovsrec_subsystem_set_fans(ovsrec_subsys, fan_array, total_fans);
    ovsdb_idl_txn_commit_block(txn);
    ovsdb_idl_txn_destroy(txn);
    free(fan_array);

    fand_set_fanspeed(result);

    return(result);
}

/* lookup a local subsystem structure
   if it's not found, create a new one and initialize it */
static struct locl_subsystem *
get_subsystem(const struct ovsrec_subsystem *ovsrec_subsys)
{
    void *ptr;
    struct locl_subsystem *result = NULL;

    ptr = shash_find_data(&subsystem_data, ovsrec_subsys->name);

    if (ptr == NULL) {
        /* this subsystem has not been added, yet. Do that now. */
        result = add_subsystem(ovsrec_subsys);
    } else {
        result = (struct locl_subsystem *)ptr;
        if (!result->valid) {
            result = NULL;
        }
    }

    return(result);
}

/* set the "marked" value for each subsystem to false. */
static void
fand_unmark_subsystems(void)
{
    struct shash_node *node;

    SHASH_FOR_EACH(node, &subsystem_data) {
        struct locl_subsystem *subsystem = (struct locl_subsystem *)node->data;
        subsystem->marked = false;
    }
}

/* delete all subsystems that haven't been marked
   this is a helper function for deleting subsystems that no longer exist
   in the DB */
static void
fand_remove_unmarked_subsystems(void)
{
    struct shash_node *node, *next;
    struct shash_node *fan_node, *fan_next;
    struct shash_node *global_node;

    SHASH_FOR_EACH_SAFE(node, next, &subsystem_data) {
        struct locl_subsystem *subsystem = node->data;

        if (subsystem->marked == false) {
            /* also, delete all fans in the subsystem */
            SHASH_FOR_EACH_SAFE(fan_node, fan_next, &subsystem->subsystem_fans) {
                struct locl_fan *fan = (struct locl_fan *)fan_node->data;
                /* delete the fan_data entry */
                global_node = shash_find(&fan_data, fan->name);
                shash_delete(&fan_data, global_node);
                /* delete the subsystem entry */
                shash_delete(&subsystem->subsystem_fans, fan_node);
                /* free the allocated data */
                free(fan->name);
                free(fan);
            }
            free(subsystem->name);
            free(subsystem);

            shash_delete(&subsystem_data, node);

            /* OPS_TODO: need to remove subsystem yaml data
                           verify that ovsdb has deleted the fans (automatic) */
        }
    }
}

/* perform general initialization, including registering for notifications */
static void
fand_init(const char *remote)
{
    int retval = 0;

    /* initialize subsystems */
    init_subsystems();

    /* initialize the yaml handle */
    yaml_handle = yaml_new_config_handle();

    idl = ovsdb_idl_create(remote, &ovsrec_idl_class, false, true);
    idl_seqno = ovsdb_idl_get_seqno(idl);
    ovsdb_idl_set_lock(idl, "ops_fand");
    ovsdb_idl_verify_write_only(idl);

    /* register interest in daemon table */
    ovsdb_idl_add_table(idl, &ovsrec_table_daemon);
    ovsdb_idl_add_column(idl, &ovsrec_daemon_col_name);
    ovsdb_idl_add_column(idl, &ovsrec_daemon_col_cur_hw);
    ovsdb_idl_omit_alert(idl, &ovsrec_daemon_col_cur_hw);

    /* register interest in all fan columns (but not in notifications,
       since this process sets the values) */
    ovsdb_idl_add_table(idl, &ovsrec_table_fan);
    ovsdb_idl_add_column(idl, &ovsrec_fan_col_name);
    ovsdb_idl_omit_alert(idl, &ovsrec_fan_col_name);
    ovsdb_idl_add_column(idl, &ovsrec_fan_col_speed);
    ovsdb_idl_omit_alert(idl, &ovsrec_fan_col_speed);
    ovsdb_idl_add_column(idl, &ovsrec_fan_col_direction);
    ovsdb_idl_omit_alert(idl, &ovsrec_fan_col_direction);
    ovsdb_idl_add_column(idl, &ovsrec_fan_col_rpm);
    ovsdb_idl_omit_alert(idl, &ovsrec_fan_col_rpm);
    ovsdb_idl_add_column(idl, &ovsrec_fan_col_status);
    ovsdb_idl_omit_alert(idl, &ovsrec_fan_col_status);

    /* handle temp sensors (fan status output of temp sensors) */
    ovsdb_idl_add_table(idl, &ovsrec_table_temp_sensor);
    ovsdb_idl_add_column(idl, &ovsrec_temp_sensor_col_fan_state);

    /* register interest in the subsystems. this process needs the
       name and hw_desc_dir fields. the name value must be unique within
       all subsystems (used as a key). the hw_desc_dir needs to be populated
       with the location where the hardware description files are located */
    ovsdb_idl_add_table(idl, &ovsrec_table_subsystem);
    ovsdb_idl_add_column(idl, &ovsrec_subsystem_col_other_config);
    ovsdb_idl_add_column(idl, &ovsrec_subsystem_col_name);
    ovsdb_idl_add_column(idl, &ovsrec_subsystem_col_hw_desc_dir);
    ovsdb_idl_add_column(idl, &ovsrec_subsystem_col_temp_sensors);
    ovsdb_idl_add_column(idl, &ovsrec_subsystem_col_fans);
    ovsdb_idl_omit_alert(idl, &ovsrec_subsystem_col_fans);

    /* OPS_TODO: add temperature sensors status */

    unixctl_command_register("ops-fand/dump", "", 0, 0,
                             fand_unixctl_dump, NULL);

    retval = event_log_init("FAN");
    if(retval < 0) {
         VLOG_ERR("Event log initialization failed for FAN");
    }
}

static void
fand_exit(void)
{
    ovsdb_idl_destroy(idl);
}

static void
fand_read_status(struct ovsdb_idl *idl)
{
    const struct ovsrec_fan *db_fan;
    const struct ovsrec_daemon *db_daemon;
    const struct shash_node *node;
    const struct shash_node *fan_node;
    struct ovsdb_idl_txn *txn;
    int64_t rpm[1];
    bool change;

    /* read all fan status */
    SHASH_FOR_EACH(node, &subsystem_data) {
        struct locl_subsystem *subsystem = (struct locl_subsystem *)node->data;
        SHASH_FOR_EACH(fan_node, &subsystem->subsystem_fans) {
            struct locl_fan *fan;
            fan = (struct locl_fan *)fan_node->data;
            fan->speed = subsystem->speed;
            fand_read_fan_status(fan);
            VLOG_DBG("fan %s rpm set to %d\n", fan->name, fan->rpm);
        }
    }

    txn = ovsdb_idl_txn_create(idl);

    change = false;
    /* walk through each fan in DB and update status from cached data */
    OVSREC_FAN_FOR_EACH(db_fan, idl) {
        struct locl_fan *fan;
        fan_node = shash_find(&fan_data, db_fan->name);
        fan = (struct locl_fan *)fan_node->data;

        const char *status = fan_status_enum_to_string(fan->status);
        if (strcmp(db_fan->status, status) != 0) {
            ovsrec_fan_set_status(db_fan, status);
            change = true;
        }
        const char *speed = fan_speed_enum_to_string(fan->speed);
        if (strcmp(db_fan->speed, speed) != 0) {
            ovsrec_fan_set_speed(db_fan, speed);
            change = true;
        }
        if (strcmp(db_fan->direction, fan->direction) != 0) {
            ovsrec_fan_set_direction(db_fan, fan->direction);
            change = true;
        }
        if (db_fan->rpm == NULL || db_fan->rpm[0] != fan->rpm) {
            rpm[0] = fan->rpm;
            ovsrec_fan_set_rpm(db_fan, rpm, 1);
            change = true;
        }
    }

    /* Set cur_hw = 1 if this is first time through. */
    if (!cur_hw_set) {
        OVSREC_DAEMON_FOR_EACH(db_daemon, idl) {
            if (strcmp(db_daemon->name, NAME_IN_DAEMON_TABLE) == 0) {
                ovsrec_daemon_set_cur_hw(db_daemon, (int64_t) 1);
                cur_hw_set = true;
                change = true;
                break;
            }
        }
    }

    if (change) {
        ovsdb_idl_txn_commit_block(txn);
    }

    ovsdb_idl_txn_destroy(txn);
}

static void
fand_run__(void)
{
    fand_read_status(idl);
}

static void
fand_reconfigure(struct ovsdb_idl *idl)
{
    const struct ovsrec_subsystem *cfg;
    unsigned int new_idl_seqno = ovsdb_idl_get_seqno(idl);

    COVERAGE_INC(fand_reconfigure);

    if (new_idl_seqno == idl_seqno){
        return;
    }

    idl_seqno = new_idl_seqno;

    fand_unmark_subsystems();

    OVSREC_SUBSYSTEM_FOR_EACH(cfg, idl) {
        const char *override = NULL;
        enum fanspeed override_value;
        struct locl_subsystem *subsystem;
        size_t idx;
        enum fanspeed highest = FAND_SPEED_SLOW;

        subsystem = get_subsystem(cfg);

        /* Skip if this subsystem is to be ignored. */
        if (subsystem == NULL) {
            continue;
        }

        /* find the highest fan_state value in the subsystem */
        for (idx = 0; idx < cfg->n_temp_sensors; idx++) {
            struct ovsrec_temp_sensor *sensor = cfg->temp_sensors[idx];
            enum fanspeed speed = fan_speed_string_to_enum(sensor->fan_state);

            if (speed > highest) {
                highest = speed;
            }
        }
        /* record that as the current speed by sensor */
        subsystem->fan_speed = highest;

        /* but also check to see if we have an override value */
        override = smap_get(&cfg->other_config, "fan_speed_override");
        override_value = fan_speed_string_to_enum(override);
        if (subsystem->fan_speed_override != override_value) {
            subsystem->fan_speed_override = override_value;
        }

        fand_set_fanspeed(subsystem);

        /* "mark" the subsystem, to indicate that it is still present */
        subsystem->marked = true;
    }

    /* delete all subsystems that aren't actually present in the DB */
    fand_remove_unmarked_subsystems();
}

static void
fand_run(void)
{
    ovsdb_idl_run(idl);

    if (ovsdb_idl_is_lock_contended(idl)) {
        static struct vlog_rate_limit rl = VLOG_RATE_LIMIT_INIT(1, 1);

        VLOG_ERR_RL(&rl, "another ops-fand process is running, "
                    "disabling this process until it goes away");

        return;
    } else if (!ovsdb_idl_has_lock(idl)) {
        return;
    }

    fand_reconfigure(idl);
    fand_run__();

    daemonize_complete();
    vlog_enable_async();
    VLOG_INFO_ONCE("%s (OpenSwitch fand) %s", program_name, VERSION);
}

static void
fand_wait(void)
{
    ovsdb_idl_wait(idl);
    poll_timer_wait(FAN_POLL_INTERVAL * MSEC_PER_SEC);
}

static void
fand_unixctl_dump(struct unixctl_conn *conn, int argc OVS_UNUSED,
                          const char *argv[] OVS_UNUSED, void *aux OVS_UNUSED)
{
    const struct locl_subsystem *subsystem = NULL;
    const struct locl_fan *fan = NULL;
    const struct shash_node *node = NULL;
    const struct shash_node *fan_node = NULL;
    struct ds ds = DS_EMPTY_INITIALIZER;

    SHASH_FOR_EACH(node, &subsystem_data) {

        subsystem = (struct locl_subsystem *)node->data;

        ds_put_format(&ds, "Subsystem: %s\n", subsystem->name);

        ds_put_format(&ds, "    Fan speed Override: %s\n",
                      fan_speed_enum_to_string(subsystem->fan_speed_override));

        ds_put_format(&ds, "    Fan speed: %s\n",
                      fan_speed_enum_to_string(subsystem->fan_speed));

        ds_put_cstr(&ds, "    Fan details:");

        if (shash_is_empty(&subsystem->subsystem_fans)) {
            ds_put_cstr(&ds, "No Fans found.\n");
            continue;
        }
        ds_put_cstr(&ds, "\n");

        SHASH_FOR_EACH(fan_node, &subsystem->subsystem_fans) {
            fan = (struct locl_fan *)fan_node->data;
            ds_put_format(&ds, "        Name: %s\n", fan->name);
            ds_put_format(&ds, "            rpm: %d\n", fan->rpm);
            ds_put_format(&ds, "            direction: %s\n",
                          fan->direction);
            ds_put_format(&ds, "            status: %s\n",
                          fan_status_enum_to_string(fan->status));
        }
    }

    unixctl_command_reply(conn, ds_cstr(&ds));

    ds_destroy(&ds);
}


static unixctl_cb_func ops_fand_exit;

static char *parse_options(int argc, char *argv[], char **unixctl_path);
OVS_NO_RETURN static void usage(void);

int
main(int argc, char *argv[])
{
    char *unixctl_path = NULL;
    struct unixctl_server *unixctl;
    char *remote;
    bool exiting;
    int retval;

    set_program_name(argv[0]);

    proctitle_init(argc, argv);
    remote = parse_options(argc, argv, &unixctl_path);
    fatal_ignore_sigpipe();

    ovsrec_init();

    daemonize_start();

    retval = unixctl_server_create(unixctl_path, &unixctl);
    if (retval) {
        exit(EXIT_FAILURE);
    }
    unixctl_command_register("exit", "", 0, 0, ops_fand_exit, &exiting);

    fand_init(remote);
    free(remote);

    exiting = false;
    while (!exiting) {
        fand_run();
        unixctl_server_run(unixctl);

        fand_wait();
        unixctl_server_wait(unixctl);
        if (exiting) {
            poll_immediate_wake();
        }
        poll_block();
    }
    fand_exit();
    unixctl_server_destroy(unixctl);

    return 0;
}

static char *
parse_options(int argc, char *argv[], char **unixctl_pathp)
{
    enum {
        OPT_PEER_CA_CERT = UCHAR_MAX + 1,
        OPT_UNIXCTL,
        VLOG_OPTION_ENUMS,
        OPT_BOOTSTRAP_CA_CERT,
        OPT_ENABLE_DUMMY,
        OPT_DISABLE_SYSTEM,
        DAEMON_OPTION_ENUMS,
        OPT_DPDK,
    };
    static const struct option long_options[] = {
        {"help",        no_argument, NULL, 'h'},
        {"version",     no_argument, NULL, 'V'},
        {"unixctl",     required_argument, NULL, OPT_UNIXCTL},
        DAEMON_LONG_OPTIONS,
        VLOG_LONG_OPTIONS,
        STREAM_SSL_LONG_OPTIONS,
        {"peer-ca-cert", required_argument, NULL, OPT_PEER_CA_CERT},
        {"bootstrap-ca-cert", required_argument, NULL, OPT_BOOTSTRAP_CA_CERT},
        {NULL, 0, NULL, 0},
    };
    char *short_options = long_options_to_short_options(long_options);

    for (;;) {
        int c;

        c = getopt_long(argc, argv, short_options, long_options, NULL);
        if (c == -1) {
            break;
        }

        switch (c) {
        case 'h':
            usage();

        case 'V':
            ovs_print_version(OFP10_VERSION, OFP10_VERSION);
            exit(EXIT_SUCCESS);

        case OPT_UNIXCTL:
            *unixctl_pathp = optarg;
            break;

        VLOG_OPTION_HANDLERS
        DAEMON_OPTION_HANDLERS
        STREAM_SSL_OPTION_HANDLERS

        case OPT_PEER_CA_CERT:
            stream_ssl_set_peer_ca_cert_file(optarg);
            break;

        case OPT_BOOTSTRAP_CA_CERT:
            stream_ssl_set_ca_cert_file(optarg, true);
            break;

        case '?':
            exit(EXIT_FAILURE);

        default:
            abort();
        }
    }
    free(short_options);

    argc -= optind;
    argv += optind;

    switch (argc) {
    case 0:
        return xasprintf("unix:%s/db.sock", ovs_rundir());

    case 1:
        return xstrdup(argv[0]);

    default:
        VLOG_FATAL("at most one non-option argument accepted; "
                   "use --help for usage");
    }
}

static void
usage(void)
{
    printf("%s: OpenSwitch fand daemon\n"
           "usage: %s [OPTIONS] [DATABASE]\n"
           "where DATABASE is a socket on which ovsdb-server is listening\n"
           "      (default: \"unix:%s/db.sock\").\n",
           program_name, program_name, ovs_rundir());
    stream_usage("DATABASE", true, false, true);
    daemon_usage();
    vlog_usage();
    printf("\nOther options:\n"
           "  --unixctl=SOCKET        override default control socket name\n"
           "  -h, --help              display this help message\n"
           "  -V, --version           display version information\n");
    exit(EXIT_SUCCESS);
}

static void
ops_fand_exit(struct unixctl_conn *conn, int argc OVS_UNUSED,
                  const char *argv[] OVS_UNUSED, void *exiting_)
{
    bool *exiting = exiting_;
    *exiting = true;
    unixctl_command_reply(conn, NULL);
}
