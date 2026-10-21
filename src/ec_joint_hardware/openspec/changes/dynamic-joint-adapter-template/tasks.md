## 1. OpenSpec and repository review

- [x] 1.1 Verify the current user-edited `robot.xacro` is the source of truth
  for the numbered hardware parameter naming convention.
- [x] 1.2 Confirm the revised proposal, design, and delta spec contain no
  `config_replacements` requirements.
- [x] 1.3 Run `openspec validate --all --strict` after updating all artifacts.

## 2. Adapter parameter parsing tests

- [x] 2.1 Add tests for one valid `adapter_type_1` /
  `interface_name_1` pair.
- [x] 2.2 Add tests for multiple numbered adapter pairs and independent IDs.
- [x] 2.3 Add tests rejecting no pairs, empty values, malformed suffixes, and
  incomplete type/interface pairs.
- [x] 2.4 Add tests proving extra complete pairs not used by JSON are allowed.

## 3. Numbered JSON loader implementation

- [x] 3.1 Replace the generic replacement-map type with an adapter-selection
  type keyed by numeric ID and containing type/interface values.
- [x] 3.2 Parse exact `adapter_type_N` and `interface_name_N` hardware
  parameters during `on_init()` and retain the selection map.
- [x] 3.3 Change `LoadJointHardwareConfig()` to the single interface accepting
  the adapter-selection map; remove the old generic replacement interface.
- [x] 3.4 Recursively resolve exact `plugin-replace-me-N` and
  `enx-replace-me-N` string values using matching IDs.
- [x] 3.5 Preserve JSON numeric values and reject unknown or unresolved marker
  IDs before SDK calls.
- [x] 3.6 Add loader tests for nested motor references, multiple IDs, unknown
  IDs, substring non-matches, and numeric preservation.

## 4. EcJointHardware lifecycle integration

- [x] 4.1 Make `on_init()` require at least one complete numbered adapter pair.
- [x] 4.2 Pass the retained adapter-selection map to the loader from
  `on_configure()`.
- [x] 4.3 Add a Fake adapter configure test proving resolved adapter type and
  interface name reach SDK configuration.
- [x] 4.4 Verify unresolved IDs fail before adapter/joint creation.
- [x] 4.5 Update all existing tests and run the complete
  `ec_joint_hardware` package test suite.

## 5. Xacro integration

- [x] 5.1 Remove aggregate `joint_hardware_replacements` and
  `config_replacements` wiring.
- [x] 5.2 Align `robot.xacro`, `slave_robot.xacro`, and `master_robot.xacro`
  on the `adapter_type`/`interface_name` source arguments and numbered
  hardware parameters.
- [x] 5.3 Pass only the adapter pairs required by each `EcJointHardware`
  instance, including additional numbered pairs when needed.
- [x] 5.4 Leave `ec_joint_hardware.xacro` unchanged.
- [x] 5.5 Expand all three top-level xacros with enabled joint hardware and
  verify the expected numbered parameters are present.

## 6. Documentation and final verification

- [x] 6.1 Document the numbered parameter contract and marker-to-ID mapping in
  `ec_joint_hardware/README.md`.
- [x] 6.2 Validate all four user-edited JSON files with `jq` without modifying
  them.
- [x] 6.3 Run package build, tests, `colcon test-result --verbose`,
  `git diff --check`, and C++ formatting checks.
- [x] 6.4 Re-run `openspec validate --all --strict` and confirm the final
  implementation does not modify the four JSON files.
