# High level design of ops-fand

See [thermal management design](http://www.openswitch.net/dev/thermal_management_design) for a description of OpenSwitch thermal management.

## Reponsibilities
The fan daemon reads the fan level values determined by the [temperature sensor daemon](http://www.openswitch.net/dev/tempd_design) and sets the fan speed to an appropriate value. The fan daemon also reads the RPM information from fans and reports the information so you can verify that the fans are operating properly.

## Design choices
ops-fand could have been merged with ops-tempdd. However, keeping these separate will accomodate future platforms with more involved thermal management strategies.

## Relationships to external OpenSwitch entities
```ditaa
  +--------+
  |database|
  +-^-----^+
    |     |
    |     |
+-----+  +----+
|tempd|  |fand|
+-----+  +----+
   |      |
   |      |
+--v------v-+
|config_yaml|
+-----------+
   |       |
   |       |
 +-v-+  +--v----+
 |i2c|  |hw desc|
 +---+  +-------+
```

## OVSDB-Schema
The following rows/cols are created by ops-fand
```
  rows in Fan table
  Fan:name
  Fan:speed
  Fan:direction
  Fan:rpm
  Fan:status
```

The following cols are written by ops-fand
```
  Fan:speed
  Fan:direction
  Fan:rpm
  Fan:status
  daemon["ops-fand"]:cur_hw
  subsystem:fans
```

The following cols are read by ops-fand
```
  subsystem:name
  subsystem:hw_desc_dir
  subsystem:other_config
  subsystem:temp_sensors
```

## Internal structure
### Main loop
Main loop pseudo-code
```
  initialize OVS IDL
  initialize appctl interface
  while not exiting
  if db has been configured
     check for any inserted/removed fan modules
     for each fan
        update status
        set fan direction
        set fan speed
  check for appctl
  wait for IDL or appctl input
```

### Source modules
```ditaa
  +--------+
  | fand.c |        +---------------------+
  |        |        | config-yaml library |    +----------------------+
  |        +------->+                     +--->+ hw description files |
  |        |        |                     |    +----------------------+
  |        |        |                     |
  |        |        |            +--------+
  |        +-------------------> | i2c    |    +----------+
  |        |        |            |        +--->+ fan FRUs |
  |        |        +------------+--------+    +----------+
  |        |
  |        |       +-------------+
  |        +-------+ fanstatus.c |
  |        |       +-------------+
  |        |
  |        |       +-------+
  |        +------>+ OVSDB |
  |        |       +-------+
  |        |
  |        |       +-----------+     +----------------------+
  |        |       | physfan.c +---->+ fandirection.c       |
  |        |       |           |     +----------------------+
  |        +------>+           |
  |        |       |           |     +-----------------------+
  |        |       |           +---->+ fanspeed.c            |
  |        |       +-----------+     +-----------------------+
  +--------+
```

### Data structures
```
locl_subsystem: list of fan modules and their status
locl_fan: fan data
```

## References
* [thermal management design](http://www.openswitch.net/dev/thermal_management_design)
* [config-yaml library](http://www.openswitch.net/dev/config_yaml_design)
* [hardware description files](http://www.openswitch.net/dev/ops_hw_config_design)
* [temperature daemon](http://www.openswitch.net/dev/tempd_design)
