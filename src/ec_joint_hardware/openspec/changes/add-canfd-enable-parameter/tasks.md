## 1. Hardware parameter and SDK integration

- [x] 1.1 Add the default-enabled `canfd_enable_` member and parse the exact
  accepted boolean values in `on_init()`, resetting omission to `true` and
  rejecting invalid values with contextual logs.
- [x] 1.2 Apply the resolved setting through the installed JointSDK enable or
  disable method after each manager recreation and before adding joints.
- [x] 1.3 Include the selected setting in successful configuration logs and
  retain the existing SDK exception-to-lifecycle-error behavior.

## 2. Hardware regression tests

- [x] 2.1 Cover omitted values, all accepted boolean spellings, empty values,
  unsupported values, surrounding whitespace, and initialization with omission
  following an explicit disabled value.
- [x] 2.2 Use existing Fake adapter helpers to verify actual command CANFD frame
  flags for both enabled and disabled configurations.
- [x] 2.3 Verify reconfiguration preserves the selected mode after manager
  recreation and regress configuration failures without hardware activation.

## 3. Robot description integration

- [x] 3.1 Declare the default-true `canfd_enable` argument in `robot.xacro`,
  `master_robot.xacro`, and `slave_robot.xacro` in the description repository.
- [x] 3.2 Pass the argument to all four arm hardware declarations and literal
  `false` to all four left/right hand declarations across the entrypoints.
- [x] 3.3 Expand all three entrypoints with joint devices enabled for omitted,
  explicit-true, and explicit-false settings; assert arm values and fixed hand
  overrides in every emitted `EcJointHardware` block.
- [x] 3.4 Regress `isaac` mode and disabled device groups, preserving existing
  simulation interfaces and hardware-enable conditions.

## 4. Documentation and verification

- [x] 4.1 Update the hardware README with accepted values, default-enabled
  omission behavior, configuration timing, hand overrides, and direct xacro
  override usage; explain that existing launch parameter forwarding is outside
  this change.
- [x] 4.2 Build `ec_joint_hardware` and `robot_description` with one worker and
  one build job using the installed SDK; enable and run all hardware package
  tests, then inspect `colcon test-result --verbose`.
- [x] 4.3 Run the applicable clang-format and clang-tidy checks for changed C++
  code, inspect compiler diagnostics, and run `git diff --check` in both repos.
- [x] 4.4 Run `openspec validate add-canfd-enable-parameter --strict
  --no-interactive` and `openspec validate --all --strict --no-interactive` in
  the hardware repository; verify the final implementation remains confined to
  the two requested repositories and does not change JointSDK, JSON files,
  launch forwarding, or unrelated hardware interfaces.

## Verification results (2026-09-11)

- Built both packages with `BUILD_TESTING=ON`, the installed JointSDK, a
  sequential executor, and one build job.
- All 65 hardware-package cases passed, including 10 new CANFD cases; all
  23 xacro verification scenarios passed. `colcon test-result --verbose`
  reported no errors, failures, or skipped tests in that run (its totals also
  count CTest wrapper entries). Per the user's final scope, the xacro test
  script, CMake registration, and pytest dependency were removed after this
  verification; the description repository retains only the three xacro edits.
- Compared all six encos/isaac entrypoint expansions against pre-change
  snapshots: only the intended CANFD hardware parameters changed.
- The three changed C++ files pass repository clang-format checks.
  clang-tidy with `clang-analyzer-*` and `bugprone-*` completed successfully;
  all eight visible diagnostics refer to unchanged code, with none on changed
  lines. Existing findings concern branch duplication, swappable test-helper
  parameters, and unchecked optional access.
- The C++ build retains the existing ROS `on_init(HardwareInfo)` deprecation
  warning. The initial description build also reported that the C++ compilation
  database option is unused by this data-only package.
- Change-specific and repository-wide strict OpenSpec validation pass, as do
  whitespace checks. Changes are confined to the two requested repositories;
  JSON, JointSDK, launch forwarding, and unrelated hardware interfaces were
  not modified.
- Real hardware was not launched; physical-device communication remains an
  integration check beyond the Fake adapter verification performed here.
