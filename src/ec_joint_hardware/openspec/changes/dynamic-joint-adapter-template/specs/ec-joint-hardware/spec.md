## ADDED Requirements

### Requirement: Numbered adapter hardware parameters

Every `EcJointHardware` instance MUST receive at least one complete numbered
adapter pair through hardware parameters. The accepted parameter names MUST be
`adapter_type_<N>` and `interface_name_<N>`, where `<N>` is a decimal adapter
ID. Both values MUST be non-empty strings. `on_init()` MUST discover all such
pairs and return lifecycle `ERROR` for missing pairs, malformed IDs, or empty
values.

#### Scenario: One adapter pair is parsed

- **WHEN** hardware parameters contain `adapter_type_1` and
  `interface_name_1`
- **THEN** `on_init()` MUST store adapter ID `1` with its type and interface
  name

#### Scenario: Multiple adapter pairs are parsed

- **WHEN** hardware parameters contain complete pairs for IDs `1` and `2`
- **THEN** `on_init()` MUST retain both selections independently

#### Scenario: Adapter pair is incomplete

- **WHEN** `adapter_type_N` or `interface_name_N` is missing for an ID used by
  the hardware parameter set
- **THEN** `on_init()` MUST log a configuration error and return `ERROR`

### Requirement: ID-based JSON template resolution

The configuration loader MUST expose exactly one loading interface accepting a
JSON path and an adapter-selection map. Before parsing adapter DTOs or passing
joint JSON to joint_sdk, it MUST recursively replace complete string values
matching `plugin-replace-me-N` with the selected adapter type and
`enx-replace-me-N` with the selected interface name. JSON numeric values,
including `busId` and `motorId`, MUST remain numeric and unchanged.

#### Scenario: Adapter and motor references use the same ID

- **WHEN** an adapter entry and nested motor parameters contain
  `plugin-replace-me-1` and `enx-replace-me-1`
- **THEN** every exact occurrence MUST resolve from adapter selection `1`

#### Scenario: Multiple adapter IDs are resolved

- **WHEN** a JSON template contains markers ending in `-1` and `-2`, and both
  adapter selections are supplied
- **THEN** each marker MUST resolve using the selection with the matching ID

#### Scenario: Numeric topology is preserved

- **WHEN** numeric `busId` and `motorId` fields are adjacent to template
  markers
- **THEN** their values and JSON numeric types MUST be unchanged

#### Scenario: Unused adapter selection is allowed

- **WHEN** hardware provides a complete adapter pair whose ID is not present in
  the JSON template
- **THEN** configuration loading MUST succeed and ignore the unused selection

### Requirement: Unresolvable JSON markers fail before SDK creation

The loader MUST reject a marker whose numeric ID is absent from the adapter
selection map. After substitution it MUST also reject any remaining exact
`plugin-replace-me-N` or `enx-replace-me-N` string. These errors MUST occur
before any adapter or joint is created.

#### Scenario: JSON references an unknown adapter ID

- **WHEN** JSON contains `plugin-replace-me-2` or `enx-replace-me-2` but only
  adapter selection `1` is supplied
- **THEN** loading MUST throw a configuration error and `on_configure()` MUST
  return `ERROR`

#### Scenario: Marker is not an exact string value

- **WHEN** a string contains a marker as a substring rather than being exactly
  a marker value
- **THEN** the loader MUST leave that string unchanged and MUST NOT treat it as
  an adapter reference

### Requirement: Top-level xacro adapter wiring

`robot.xacro`, `slave_robot.xacro`, and `master_robot.xacro` MUST declare the
adapter xacro arguments used by their joint hardware. Each
`EcJointHardware` declaration MUST directly emit the numbered
`adapter_type_N` and `interface_name_N` parameters it uses. The common
`ec_joint_hardware.xacro` MUST NOT be required to declare or wrap these
parameters. The xacros MUST NOT emit `config_replacements`.

#### Scenario: Default single adapter wiring

- **WHEN** a top-level xacro is expanded with its default adapter settings and
  a joint hardware instance is enabled
- **THEN** that hardware MUST receive a complete `adapter_type_1` /
  `interface_name_1` pair

#### Scenario: Additional adapter wiring

- **WHEN** a hardware instance needs a second adapter
- **THEN** its top-level xacro MUST be able to declare and pass
  `adapter_type_2` and `interface_name_2` without changing C++ marker-specific
  code
