# Change: Select JSON adapters from numbered hardware parameters

## Why

The four joint JSON files describe adapter references with numbered template
markers such as `plugin-replace-me-1` and `enx-replace-me-1`. Adapter type and
interface name are deployment-specific hardware settings and should be passed
from the top-level xacro to each `EcJointHardware` instance. Requiring a large
replacement JSON map in xacro duplicates the JSON structure and makes it hard
to see which hardware adapter a joint hardware instance actually uses.

The hardware plugin should instead discover numbered `adapter_type_N` and
`interface_name_N` parameters, pair them by `N`, and resolve the corresponding
numbered JSON markers before creating any joint_sdk resources.

## What Changes

- Remove the aggregate `config_replacements` hardware parameter design.
- Parse numbered adapter parameters from `HardwareInfo`:
  `adapter_type_N` and `interface_name_N`.
- Replace exact JSON string values `plugin-replace-me-N` with the selected
  adapter type and `enx-replace-me-N` with the selected interface name.
- Keep JSON numeric topology values such as `busId` and `motorId` unchanged.
- Reject incomplete adapter parameter pairs, missing referenced IDs, and
  unresolved template markers before calling joint_sdk.
- Update `robot.xacro`, `slave_robot.xacro`, and `master_robot.xacro` to pass
  only the numbered adapter parameters required by each joint hardware.
- Keep `ec_joint_hardware.xacro`, joint_sdk, launch/YAML files, and the four
  user-maintained JSON files outside the implementation scope.

## Impact

- Changes the `EcJointHardware` hardware-parameter contract.
- Changes the configuration loader input from a generic replacement map to a
  numbered adapter selection map.
- Adds parser, loader, lifecycle, fake-adapter, and xacro coverage for the new
  contract.
- Requires the three top-level xacros to use the same numbered-parameter
  convention.
