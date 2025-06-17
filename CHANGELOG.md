<!-- Copyright (c) 2023 Golioth, Inc. -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Fix

- Use correct date and template version for v1.4.0 release notes

## [v1.4.0] - 2025-06-17

### Changed

- Merge changes from
  [`golioth/reference-design-template@template_v2.7.2`](https://github.com/golioth/reference-design-template/tree/template_v2.7.2).
- Firmware version is now selected in the VERSION file.

## [v1.3.0] - 2024-09-24

### Added

- Pipeline example
- Add support for Aludel Elixir

### Changed

- Merge changes from
  [`golioth/reference-design-template@template_v2.4.1`](https://github.com/golioth/reference-design-template/tree/template_v2.4.1).
- Use dynamic allocation for batch upload buffer
- Update board names for Zephyr hardware model v2
- Use `VERSION` file instead of `prj.conf` to set firmware version


## [v1.2.0] - 2024-05-14

### Changed
- Merge changes from
  [`golioth/reference-design-template@template_v2.1.0`](https://github.com/golioth/reference-design-template/tree/template_v2.1.0).

## [v1.1.0] - 2023-09-22

### Added

- Initial release
- Bulk upload of cached data
- Log number of satellites being tracked when awaiting GPS lock

### Changed
- Increase cache slots from 64 to 500
- Log cache usage every 5 readings
- GPS readings will continue to be reported in the event sensor information is unavailable

### Fixed
- Parse NMEA in dedicated thread instead of during UART interrupt
- Check for Golioth connection before streaming battery readings
- Prevent negative sign to the right of decimal place when logging subzero weather readings
