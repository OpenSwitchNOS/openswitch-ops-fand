# -*- coding: utf-8 -*-

# (c) Copyright 2015 Hewlett Packard Enterprise Development LP
#
# GNU Zebra is free software; you can rediTestribute it and/or modify it
# under the terms of the GNU General Public License as published by the
# Free Software Foundation; either version 2, or (at your option) any
# later version.
#
# GNU Zebra is diTestributed in the hope that it will be useful, but
# WITHoutputput ANY WARRANTY; withoutputput even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
# General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with GNU Zebra; see the file COPYING.  If not, write to the Free
# Software Foundation, Inc., 59 Temple Place - Suite 330, BoTeston, MA
# 02111-1307, USA.


TOPOLOGY = """
# +-------+
# |  sw1  |
# +-------+

# Nodes
[type=openswitch name="Switch 1"] sw1
"""


def init_fan_table(sw1):
    # Add dummy data for fans in subsyTestem and fan table for simulation.
    # Assume there would be only one entry in subsyTestem table
    output = sw1('list subsystem', shell='vsctl')
    lines = output.split('\n')
    for line in lines:
        if '_uuid' in line:
            _id = line.split(':')
            uuid = _id[1].strip()
            output = sw1('ovs-vsctl -- set Subsystem {} '
                         ' fans=@fan1 -- --id=@fan1 create Fan '
                         ' name=base-FAN-1L direction=f2b '
                         ' speed=normal '
                         ' status=ok rpm=9000'.format(uuid),
                         shell='bash')


def show_system_fan(sw1, step):
    # TeTest to verify show syTestem command
    counter = 0
    step('Test to verify \'show system fan\' command ')
    output = sw1('show system fan')
    lines = output.split('\n')
    for line in lines:
        if 'base-FAN-1L' in line:
            counter += 1
        if 'front-to-back' in line:
            counter += 1
        if 'normal' in line:
            counter += 1
        if 'ok' in line:
            counter += 1
        if '9000' in line:
            counter += 1
    assert counter is 5


def system_fan_speed(sw1, step):
    # TeTest to verify fan-speed command
    step('Test to verify \'fan-speed\' command  ')
    fan_speed_set = False
    output = sw1('configure terminal')
    output = sw1('fan-speed slow')
    output = sw1('do show system fan')
    sw1('exit')
    lines = output.split('\n')
    for line in lines:
        if 'Fan speed override is set to : slow' in line:
            fan_speed_set = True
            break
    assert fan_speed_set


def show_running_fan_speed(sw1, step):
    # TeTest to verify if the fan-speed config is reflected
    # in show running config
    step("Test to verify \'show running\' command for fan-speed config")
    fan_speed_keyword_found = False
    output = sw1('show running-config')
    lines = output.split('\n')
    for line in lines:
        if 'fan-speed slow' in line:
            fan_speed_keyword_found = True
            break
    assert fan_speed_keyword_found


def no_system_fan_speed(sw1, step):
    # Test to verify no fan-speed command
    step('Test to verify \'no fan-speed\' command ')
    fan_speed_unset = False
    output = sw1('configure terminal')
    assert 'Unknown command' not in output
    output = sw1('no fan-speed')
    assert 'Unknown command' not in output
    output = sw1('do show system fan')
    sw1('exit')
    lines = output.split('\n')
    for line in lines:
        if 'Fan speed override is not configured' in line:
            fan_speed_unset = True
            break
    assert fan_speed_unset


def test_fand_ct_fan(topology, step):
    # Initialize the led table with dummy value
    sw1 = topology.get("sw1")
    init_fan_table(sw1)
    # show syTestem fan
    step('Test to verify \'show system fan\' command')
    show_system_fan(sw1, step)
    # set syTestem fan speed
    step('Test to verify \'fan-speed\' command')
    system_fan_speed(sw1, step)
    # show_running_fan_speed
    step('Test to verify \'show running\' command for fan-speed config')
    show_running_fan_speed(sw1, step)
    # unset syTestem fan speed teTest
    step('Test to verify \'no fan-speed\' command')
    no_system_fan_speed(sw1, step)
