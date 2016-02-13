/* Fan daemon client callback resigitration source files.
 *
 * Copyright (C) 2016 Hewlett Packard Enterprise Development LP.
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2, or (at your option) any
 * later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
 * MA 02110-1301, USA.
 *
 * File: vtysh_ovsdb_fan_context.c
 *
 * Purpose: Source for registering sub-context callback with
 *          global config context.
 */

#include "vtysh/vty.h"
#include "vtysh/vector.h"
#include "vswitch-idl.h"
#include "openswitch-idl.h"
#include "vtysh/vtysh_ovsdb_if.h"
#include "vtysh/vtysh_ovsdb_config.h"
#include "vtysh/utils/system_vtysh_utils.h"
#include "vtysh_ovsdb_fan_context.h"


/*-----------------------------------------------------------------------------
| Function : vtysh_ovsdb_subsystemtable_parse_othercfg
| Responsibility : parse subsystem table
| Parameters :
|     void *p_private: void type object typecast to required
| Return : void
-----------------------------------------------------------------------------*/
static vtysh_ret_val
vtysh_ovsdb_subsystemtable_parse_othercfg(
                           const struct smap *subsystemrow_config,
                           vtysh_ovsdb_cbmsg *p_msg)
{
    const char *data = NULL;
    if(NULL == subsystemrow_config)
    {
        return e_vtysh_error;
    }
    data = smap_get(subsystemrow_config, FAN_SPEED_OVERRIDE_STR);
    if(data)
    {
        if(!(VTYSH_STR_EQ(data, "normal")))
            vtysh_ovsdb_cli_print(p_msg, "fan-speed %s",data);
    }
    return e_vtysh_ok;
}


/*-----------------------------------------------------------------------------
| Function : vtysh_config_context_fan_clientcallback
| Responsibility : fan config client callback routine
| Parameters :
|     void *p_private: void type object typecast to required
| Return : void
-----------------------------------------------------------------------------*/
vtysh_ret_val
vtysh_config_context_fan_clientcallback(void *p_private)
{
    vtysh_ovsdb_cbmsg_ptr p_msg = (vtysh_ovsdb_cbmsg *)p_private;
    const struct ovsrec_subsystem *subsysrow;

    vtysh_ovsdb_config_logmsg(VTYSH_OVSDB_CONFIG_DBG,
                           "vtysh_config_context_fan_clientcallback entered");
    subsysrow = ovsrec_subsystem_first(p_msg->idl);
    if(subsysrow)
    {
    /* parse other config param */
        vtysh_ovsdb_subsystemtable_parse_othercfg(&subsysrow->other_config,
                                                  p_msg);
    }
    return e_vtysh_ok;
}
