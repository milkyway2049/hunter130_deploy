# Design: Numbered adapter selection for JSON templates

## Context

Each `EcJointHardware` instance loads one JSON file containing an `adapters`
array and a `joints` array. The JSON adapter type, adapter interface name, and
each joint's `motorParams.adapterType`/`adapterId` currently use the same
numbered placeholder convention. The xacro that owns a hardware instance is
the correct layer for selecting the actual adapter type and interface name.

The previous design passed one serialized replacement map through
`config_replacements`. That does not match the desired xacro interface: the
hardware should receive ordinary named parameters, and the marker suffix must
select the corresponding adapter pair.

## Goals

- Pass adapter selections as ordinary ros2_control hardware parameters.
- Support multiple adapter selections in one hardware instance using numeric
  IDs.
- Resolve both adapter declarations and nested joint motor references using
  the same ID mapping.
- Preserve all non-string JSON values exactly, especially bus and motor IDs.
- Fail before SDK adapter or joint creation when the configuration cannot be
  resolved completely.
- Keep the JSON files as templates and keep the common
  `ec_joint_hardware.xacro` free of robot-specific adapter declarations.

## Non-goals

- Do not introduce a generic JSON replacement map parameter.
- Do not rewrite or generate JSON files at launch time.
- Do not infer or remap numeric `busId` or `motorId` values.
- Do not modify joint_sdk or non-joint hardware plugin implementations.
- Do not modify the four user-edited JSON files.

## Decisions

### 1. Hardware parameter contract

Each adapter selection is represented by a pair of hardware parameters:

```xml
<param name="adapter_type_1">$(arg adapter_type)</param>
<param name="interface_name_1">$(arg interface_name)</param>
```

The numeric suffix is the adapter ID. A hardware instance that uses more
adapters passes additional complete pairs, for example
`adapter_type_2`/`interface_name_2`. The first top-level xacro adapter uses the
existing concise xacro arguments `adapter_type` and `interface_name`; extra
adapter slots use correspondingly numbered xacro arguments. Hardware
parameters remain numbered so the plugin has one unambiguous parsing rule.

An `EcJointHardware` instance must provide at least one complete pair. Values
must be non-empty. A pair supplied by hardware but not referenced by the JSON
is allowed; this lets one xacro expose a stable adapter set while individual
JSON templates use only the IDs they need. A type/interface pair may not be
split across different IDs.

The `config_replacements` parameter is not part of this design and must not be
required or emitted by the xacros.

### 2. Build the adapter selection map in `on_init()`

`on_init()` scans `HardwareInfo::hardware_parameters` for the exact key forms:

```text
adapter_type_<decimal-id>
interface_name_<decimal-id>
```

It parses the suffix as the adapter ID and stores an internal map equivalent to:

```text
adapter ID -> { adapter type, interface name }
```

Missing parameters, empty values, malformed numeric suffixes, and incomplete
pairs return lifecycle `ERROR`. The parsed map is retained for
`on_configure()`.

### 3. Resolve numbered JSON markers before SDK calls

The loader receives the JSON path and the parsed adapter-selection map. Before
converting adapter DTOs or passing joint JSON to the SDK, it recursively walks
JSON objects and arrays. It changes only complete string values matching:

```text
plugin-replace-me-<id>  -> adapter_selection[id].adapter_type
enx-replace-me-<id>     -> adapter_selection[id].interface_name
```

The replacement applies equally to the root `adapters` entries and nested
joint fields such as `config.motorParams.adapterType` and
`config.motorParams.adapterId`. Strings that merely contain a marker as a
substring are not changed. Numeric, boolean, null, object, and array structure
remain unchanged apart from recursively visited string values.

If a marker refers to an ID absent from the map, loading fails. After
replacement, any remaining exact `plugin-replace-me-N` or
`enx-replace-me-N` value also fails. This validation occurs before
`DefaultJointManager::CreateAdapter()` and `AddJoint()`.

### 4. Loader API

The loader exposes one interface that accepts the adapter selection map:

```cpp
JointHardwareConfig LoadJointHardwareConfig(
    const std::string& config_file,
    const AdapterSelections& adapter_selections);
```

`AdapterSelections` is a package configuration type keyed by numeric adapter
ID. Its value contains the adapter type and interface name needed to construct
the existing `AdapterConfig` (`type` and `name`) after JSON substitution.
There is no one-argument compatibility overload and no `ConfigSubstitutions`
type in the new design.

### 5. Xacro integration

`robot.xacro`, `slave_robot.xacro`, and `master_robot.xacro` each declare the
adapter xacro arguments used by their hardware. Every `EcJointHardware`
`<hardware>` block directly emits the numbered `adapter_type_N` and
`interface_name_N` parameters it needs. The same numbered pair is available
to the JSON template and to the plugin; no wrapper macro is introduced.

The manually established generic `adapter_type`/`interface_name` arguments
remain the source for the single default adapter slot. Existing non-joint
hardware may consume those generic arguments where the top-level xacro already
uses them; this change does not alter their plugin APIs.

## Error handling

| Condition | Result |
| --- | --- |
| No numbered adapter pair | `on_init()` returns `ERROR` |
| One side of an ID pair is missing | `on_init()` returns `ERROR` |
| Parameter value is empty | `on_init()` returns `ERROR` |
| JSON marker references unknown ID | `on_configure()` returns `ERROR` before SDK creation |
| Marker remains after resolution | `on_configure()` returns `ERROR` before SDK creation |
| Numeric JSON field is next to a marker | Numeric field remains numeric and unchanged |

## Data flow

```text
top-level xacro
    -> adapter_type_N/interface_name_N hardware parameters
    -> EcJointHardware::on_init() parses AdapterSelections
    -> LoadJointHardwareConfig(config_file, selections)
    -> numbered JSON marker resolution
    -> DefaultJointManager::CreateAdapter()
    -> DefaultJointManager::AddJoint()
```
