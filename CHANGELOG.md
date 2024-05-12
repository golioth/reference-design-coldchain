<!-- Copyright (c) 2023 Golioth, Inc. -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# Changelog
All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [v1.2.0] - Unreleased

### Changed
- Merged Reference Design template_v2.1.0

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
