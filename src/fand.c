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
#include "sset.h"

#include "fanspeed.h"
#include "fanstatus.h"
#include "physfan.h"
#include "fand-locl.h"
#include "eventlog.h"
#include "fand_plugins.h"

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

static struct locl_subsystem *
add_subsystem(const struct ovsrec_subsystem *ovsrec_subsys)
{
    struct locl_subsystem *result;
    int rc;
    int total_fans;
    struct ovsdb_idl_txn *txn;
    struct ovsrec_fan **fan_array;
    int total_fan_idx;
    const char *override;
    const char *fan_dev_name = NULL, *next = NULL;
    enum fanspeed override_value = FAND_SPEED_NONE;
    struct sset fan_set;
    const struct fand_subsystem_class *subsystem_class = NULL;
    const struct fand_fan_class *fan_class = NULL;

    VLOG_DBG("Adding new subsystem %s", ovsrec_subsys->name);

    /* Using hard coded type untill there's support for multiple platforms in
     * ops-sysd. */
    subsystem_class = fand_subsystem_class_get(PLATFORM_TYPE_STR);
    fan_class = fand_fan_class_get(PLATFORM_TYPE_STR);

    if (subsystem_class == NULL) {
        VLOG_ERR("No plugin provides subsystem class for %s type",
                 PLATFORM_TYPE_STR);
        return NULL;
    }

    if (fan_class == NULL) {
        VLOG_ERR("No plugin provides fan class for %s type",
                 PLATFORM_TYPE_STR);
        return NULL;
    }

    result = subsystem_class->fand_subsystem_alloc();
    (void)shash_add(&subsystem_data, ovsrec_subsys->name, (void *)result);
    result->name = strdup(ovsrec_subsys->name);
    result->marked = false;
    result->valid = false;
    result->parent_subsystem = NULL;  /* OPS_TODO: find parent subsystem */
    result->class = subsystem_class;
    shash_init(&result->subsystem_fans);
    override = smap_get(&ovsrec_subsys->other_config, "fan_speed_override");
    if (override != NULL) {
        override_value = fan_speed_string_to_enum(override);
    }
    result->fan_speed_override = override_value;

    rc = subsystem_class->fand_subsystem_construct(result);
    if (rc) {
        VLOG_ERR("Failed to construct subsystem %s", result->name);
        free(result->name);
        subsystem_class->fand_subsystem_dealloc(result);
        return(NULL);
    }

    /* OPS_TODO: could check to see if the temp sensors have been populated
       with data and use that for the sensor speed when initializing the
       fan_speed value. */
    result->fan_speed = FAND_SPEED_NORMAL;

    /* count the total fans in the subsystem */
    total_fans = 0;
    total_fan_idx = 0;

    sset_init(&fan_set);
    rc = subsystem_class->fand_enumerate_devices(result, &fan_set);

    total_fans = sset_count(&fan_set);

    fan_array = (struct ovsrec_fan **)malloc(total_fans * sizeof(struct ovsrec_fan *));
    memset(fan_array, 0, total_fans * sizeof(struct ovsrec_fan *));

    txn = ovsdb_idl_txn_create(idl);

    SSET_FOR_EACH_SAFE(fan_dev_name, next, &fan_set) {
        char *fan_name = NULL;
        struct ovsrec_fan *ovs_fan;
        struct locl_fan *new_fan;
        VLOG_DBG("Adding fan %s in subsystem %s",
                fan_dev_name,
                ovsrec_subsys->name);

        asprintf(&fan_name, "%s-%s", ovsrec_subsys->name, fan_dev_name);
        new_fan = fan_class->fand_fan_alloc();
        asprintf(&new_fan->name, "%s", fan_dev_name);
        new_fan->subsystem = result;
        new_fan->class = fan_class;
        rc = fan_class->fand_fan_construct(new_fan);
        if (rc) {
            VLOG_ERR("Failed constructing fan %s subsystem %s",
                     new_fan->name,
                     result->name);
            free(new_fan->name);
            fan_class->fand_fan_dealloc(new_fan);
            free(fan_name);
        }

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

        if (fan_class->fand_speed_set(new_fan, result->speed)) {
            VLOG_ERR("Failed setting speed subsystem %s fan %s",
                     result->name,
                     new_fan->name);
        }

        fan_array[total_fan_idx++] = ovs_fan;
        free(fan_name);
    }

    ovsrec_subsystem_set_fans(ovsrec_subsys, fan_array, total_fans);
    ovsdb_idl_txn_commit_block(txn);
    ovsdb_idl_txn_destroy(txn);
    free(fan_array);
    sset_destroy(&fan_set);

    result->valid = true;

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
                fan->class->fand_fan_destruct(fan);
                /* free the allocated data */
                free(fan->name);
                fan->class->fand_fan_dealloc(fan);
            }
            subsystem->class->fand_subsystem_destruct(subsystem);
            free(subsystem->name);
            subsystem->class->fand_subsystem_dealloc(subsystem);

            shash_delete(&subsystem_data, node);
        }
    }
}

/* perform general initialization, including registering for notifications */
static void
fand_init(const char *remote)
{
    int retval = 0;

    if (fand_plugins_load()) {
        VLOG_ERR("Failed loading platform support plugin.");
    }

    fand_plugins_init();

    /* initialize subsystems */
    init_subsystems();

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
    fand_plugins_deinit();
    fand_plugins_unload();
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
            if (fan->class->fand_status_get(fan, &fan->status)) {
                VLOG_ERR("Failed reading fan status subsystem %s fan %s",
                         subsystem->name,
                         fan->name);
            }
            if (fan->class->fand_direction_get &&
                fan->class->fand_direction_get(fan, &fan->direction)) {
                VLOG_ERR("Failed reading fan direction subsystem %s fan %s",
                         subsystem->name,
                         fan->name);
            }
            if (fan->class->fand_rpm_get &&
                fan->class->fand_rpm_get(fan, &fan->rpm)) {
                VLOG_ERR("Failed reading fan rpm subsystem %s fan %s",
                         subsystem->name,
                         fan->name);
            }
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
        const char *direction = fan_direction_enum_to_string(fan->direction);
        if (strcmp(db_fan->direction, direction) != 0) {
            ovsrec_fan_set_direction(db_fan, direction);
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
        struct locl_fan *fan = NULL;
        const struct shash_node *fan_node = NULL;

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
        subsystem->speed = subsystem->fan_speed;

        /* but also check to see if we have an override value */
        override = smap_get(&cfg->other_config, "fan_speed_override");
        override_value = fan_speed_string_to_enum(override);
        if (subsystem->fan_speed_override != override_value) {
            subsystem->fan_speed_override = override_value;
        }

        /* use override if it exists, unless the sensors think the speed should
         * be "max" (potential overtemp situation). */
        if (override_value != FAND_SPEED_NONE && subsystem->fan_speed != FAND_SPEED_MAX) {
            subsystem->speed = override_value;
        }

        SHASH_FOR_EACH(fan_node, &subsystem->subsystem_fans) {
            fan = (struct locl_fan *)fan_node->data;
            if (fan->class->fand_speed_set(fan, subsystem->speed)) {
                VLOG_ERR("Failed setting speed subsystem %s fan %s",
                         subsystem->name,
                         fan->name);
            }
        }

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
                          fan_direction_enum_to_string(fan->direction));
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
        fand_plugins_run();
        unixctl_server_run(unixctl);

        fand_wait();
        fand_plugins_wait();
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
