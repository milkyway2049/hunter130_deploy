## Context

`EcJointHardware::on_init()` reads hardware parameters, and `on_configure()`
recreates its `DefaultJointManager`, creates adapters, and adds JSON-defined
joints. The installed `/usr/local/include/JointManager.h` exposes enable,
disable, and query methods for the manager's CANFD setting. Newly added joint
motors inherit that setting, including motors created for coupled joints.

The three top-level xacros contain eight `EcJointHardware` declarations:

| Entrypoint | Arms following the argument | Hands fixed to false |
| --- | --- | --- |
| `robot.xacro` | Master arm, slave arm | Left hand, right hand |
| `master_robot.xacro` | Master arm | None |
| `slave_robot.xacro` | Slave arm | Left hand, right hand |

The common ros2_control xacro defines joint interfaces rather than the hardware
parameter blocks. Parameter wiring therefore belongs in the entrypoints.

## Goals / Non-Goals

**Goals:**

- Expose a validated startup CANFD setting for each hardware instance.
- Default arms to CANFD and allow the xacro argument to disable it.
- Keep both hands explicitly disabled regardless of the argument.
- Reapply the setting whenever the hardware manager is recreated.

**Non-Goals:**

- Runtime ROS parameter updates, services, or command interfaces for switching
  CANFD while hardware is active.
- Motor firmware configuration, persistent mode programming, bus bitrate
  changes, or changes to JointSDK and JSON configuration files.
- Changes to `robot_bringup`, its launch argument allowlists, or other hardware
  plugins. Direct xacro expansion can override the argument; existing launch
  paths use the xacro default.

## Decisions

### 1. Parse one optional hardware parameter

Add `bool canfd_enable_{true}` to the hardware instance. Each `on_init()`
invocation resolves the parameter from its own input, using `true` when absent.
Accept exactly `true`, `True`, `TRUE`, `1`, `false`, `False`, `FALSE`, and `0`,
consistent with existing boolean configuration conventions in adjacent hardware
packages. Do not trim strings or accept other spellings. Empty or invalid values
produce an error log identifying the parameter and value, then return lifecycle
`ERROR` before SDK resources are created.

The default is deliberately `true` in both C++ and xacro. Backward-compatible
omission behavior was considered; the selected contract instead makes callers
explicitly pass `false` when they require CANFD disabled.

### 2. Apply through the manager before adding joints

Immediately after `manager_ = encos::DefaultJointManager{}` in each
`on_configure()`, call `EnableJointMotorCanfd()` for `true` or
`DisableJointMotorCanfd()` for `false`. Keep this inside the existing exception
boundary and before `AddJoint()`, allowing the SDK to apply its setting to each
new joint's motors. Do not duplicate SDK motor traversal in the plugin.

Configuration exceptions continue to produce a logged lifecycle `ERROR` with
the instance unconfigured. Successful configuration logs include the chosen
CANFD value. Activation and cyclic reads/writes use the configured mode without
reapplying it every cycle; deactivation does not reset it.

The SDK setting selects subsequent transmission frame flags. It does not
establish that motor discovery or internal initialization transactions already
use that flag, and it does not program persistent firmware settings.

### 3. Give hands an explicit hardware-level override

All three entrypoints declare:

```xml
<xacro:arg name="canfd_enable" default="true" />
```

Each of the four arm declarations passes:

```xml
<param name="canfd_enable">$(arg canfd_enable)</param>
```

Each of the four hand declarations passes:

```xml
<param name="canfd_enable">false</param>
```

The hands cannot omit the parameter because omission enables CANFD in the
plugin. Keep the existing device-enable conditions, simulation branches, and
common joint interface macros unchanged.

### 4. Verify observable behavior

Extend the existing hardware tests and Fake adapter helpers. Check parsing and
configuration state, then inspect recorded command frame flags for enabled and
disabled settings; checking only the stored boolean is insufficient. Include
reconfiguration so recreating the manager cannot silently lose the setting.

Expand each entrypoint with joint devices enabled for omitted, `true`, and
`false` arguments. Inspect the hardware blocks by plugin and device group:
arms follow the argument, while hands always receive literal `false`. Regress
the `isaac` and disabled-device branches as well.

Xacro expansion is a local verification step; do not retain a pytest script,
test registration, or additional test dependencies in `robot_description`.

## Risks / Trade-offs

- The new omission default changes existing deployments. README migration
  guidance must show explicit `false` for callers needing the previous mode.
- All hand declarations must carry their explicit override; expansion coverage
  must include full-robot and slave-robot descriptions to catch omissions.
- Software acceptance covers parameter flow and Fake adapter frame flags.
  Real-device transport compatibility still requires subsequent hardware
  integration testing and is not established by Fake adapter tests.
- Build against the locally installed SDK interfaces. Do not add a fallback
  that silently ignores the requested mode when compiling against an older SDK.
