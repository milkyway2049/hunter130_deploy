## Why

`EcJointHardware` currently has no hardware parameter for selecting the CANFD
frame mode exposed by the installed JointSDK. Arm deployments need this setting
through xacro, while both dexterous hands must explicitly keep CANFD disabled.

## What Changes

- Add the optional `canfd_enable` hardware parameter, defaulting to `true`,
  with validated boolean parsing during initialization.
- Apply the selected mode through JointSDK whenever hardware is configured,
  before adding joints to the newly created manager.
- Declare `<xacro:arg name="canfd_enable" default="true" />` in all three
  robot description entrypoints. Master and slave arms follow the argument;
  left and right dexterous hands explicitly receive `false`.
- **BREAKING**: Hardware descriptions omitting the new parameter will enable
  CANFD instead of inheriting the SDK's current disabled default. Descriptions
  that require the previous behavior must explicitly pass `false`.
- Document the parameter and add parsing, lifecycle, Fake adapter frame, and
  xacro regression coverage.

## Capabilities

### New Capabilities

None.

### Modified Capabilities

- `ec-joint-hardware`: Add the CANFD hardware parameter, its configuration
  lifecycle behavior, and arm/hand-specific xacro wiring.

## Impact

- Implementation spans `src/robot_hardwares/ec_joint_hardware` and
  `src/robot_descriptions/robot_description` on their existing
  `fix/TXAMAXT/canfd` branches. This change package is maintained in the
  hardware repository and describes both repositories.
- Uses the installed `DefaultJointManager::EnableJointMotorCanfd()` and
  `DisableJointMotorCanfd()` interfaces; no JointSDK changes are needed.
- The public addition is a ros2_control hardware parameter and an xacro
  argument. Existing command/state interfaces and JSON formats are unchanged.
- Implementation and verification progress for C++, xacro, tests, and README
  updates are tracked in `tasks.md`.
