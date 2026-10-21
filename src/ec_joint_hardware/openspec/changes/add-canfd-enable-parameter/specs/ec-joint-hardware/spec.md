## ADDED Requirements

### Requirement: CANFD hardware parameter

Each `EcJointHardware` instance MUST accept an optional `canfd_enable` hardware
parameter. `on_init()` MUST resolve a missing parameter to `true`. It MUST
interpret exactly `true`, `True`, `TRUE`, and `1` as enabled, and exactly
`false`, `False`, `FALSE`, and `0` as disabled. Other values, including an empty
string or surrounding whitespace, MUST be rejected with a log identifying the
parameter and supplied value and a lifecycle `ERROR` result.

#### Scenario: Parameter is omitted

- **WHEN** otherwise valid hardware parameters omit `canfd_enable`
- **THEN** `on_init()` MUST succeed with CANFD enabled, including when the
  description predates this parameter

#### Scenario: Explicit boolean values

- **WHEN** otherwise valid hardware parameters provide any accepted spelling
  of an enabled or disabled value
- **THEN** `on_init()` MUST succeed and store the corresponding boolean setting

#### Scenario: Invalid parameter value

- **WHEN** `canfd_enable` is empty or contains an unsupported value such as
  `yes`, `2`, or ` true `
- **THEN** `on_init()` MUST log the invalid parameter and return `ERROR` before
  any SDK adapter or joint is created

#### Scenario: Initialization does not retain an omitted previous value

- **WHEN** initialization with otherwise valid parameters omits `canfd_enable`
  after an earlier initialization supplied `false`
- **THEN** the resolved CANFD setting MUST be `true`

### Requirement: CANFD configuration through JointSDK

Every `on_configure()` MUST apply the resolved setting to its newly created
manager before adding joints, using `EnableJointMotorCanfd()` for enabled mode
and `DisableJointMotorCanfd()` for disabled mode. Subsequent joint feedback
requests and control transmissions MUST use the configured CANFD frame setting
through the SDK. Successful configuration MUST log the selected value. SDK
exceptions during configuration MUST be logged and return lifecycle `ERROR`
without marking the hardware configured.

The setting MUST be applied during configuration, without introducing a
runtime ROS parameter callback, switching service, or command/state interface.

#### Scenario: Enabled mode reaches newly added joint motors

- **WHEN** hardware configured with `canfd_enable=true` sends a joint command
  through a Fake adapter
- **THEN** the recorded command MUST have its CANFD frame bits enabled

#### Scenario: Disabled mode reaches newly added joint motors

- **WHEN** hardware configured with `canfd_enable=false` sends a joint command
  through a Fake adapter
- **THEN** the recorded command MUST have its CANFD frame bits disabled

#### Scenario: Reconfiguration reapplies the resolved setting

- **WHEN** hardware is configured again and recreates its manager
- **THEN** it MUST apply the resolved CANFD setting before adding joints again,
  and subsequent commands MUST retain the selected frame mode

#### Scenario: SDK configuration fails

- **WHEN** an SDK operation in the configuration sequence throws an exception
- **THEN** the hardware MUST log the failure, return `ERROR`, and remain
  unconfigured

### Requirement: Arm CANFD xacro wiring with fixed hand overrides

`robot.xacro`, `master_robot.xacro`, and `slave_robot.xacro` MUST each declare
`<xacro:arg name="canfd_enable" default="true" />`. Every enabled master-arm
and slave-arm `EcJointHardware` block MUST receive the argument through
`<param name="canfd_enable">$(arg canfd_enable)</param>`. Every enabled left-
and right-hand `EcJointHardware` block MUST instead receive the literal
`<param name="canfd_enable">false</param>`, regardless of the argument.

#### Scenario: Default arm and hand settings

- **WHEN** an entrypoint is expanded with its relevant joint hardware enabled
  and without a `canfd_enable` override
- **THEN** all emitted arm hardware blocks MUST receive `true` and all emitted
  hand hardware blocks MUST receive literal `false`

#### Scenario: Explicit enable preserves hand overrides

- **WHEN** an entrypoint is expanded with `canfd_enable:=true`
- **THEN** all emitted arm hardware blocks MUST receive `true` and all emitted
  hand hardware blocks MUST still receive literal `false`

#### Scenario: Explicit disable covers every joint hardware group

- **WHEN** an entrypoint is expanded with `canfd_enable:=false`
- **THEN** all emitted arm and hand hardware blocks MUST receive `false`

#### Scenario: Device group is disabled

- **WHEN** an existing device-enable condition disables an arm or hand group
- **THEN** adding the CANFD argument MUST NOT cause that hardware block to be
  emitted

#### Scenario: Simulation description

- **WHEN** an entrypoint is expanded in `isaac` mode
- **THEN** expansion MUST continue to succeed and the CANFD addition MUST NOT
  emit an `EcJointHardware` block or change simulation interface declarations
