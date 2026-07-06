# yolo-crop-aspect-ratio Specification

## Purpose
TBD - created by archiving change yolo-crop-aspect-ratio. Update Purpose after archive.
## Requirements
### Requirement: Aspect Ratio Locking
The system SHALL lock the aspect ratio of the YOLO person crop box to match the target display grid tile's aspect ratio, ensuring the cropped target does not stretch or distort when resized into the grid.

#### Scenario: Locked aspect ratio crop
- **WHEN** YOLO detects a person and the active tile layout aspect ratio is 470:1070
- **THEN** the calculated crop box aspect ratio SHALL be equal to 470:1070 after padding and clamping

### Requirement: Clear-Distance Protection
The system SHALL apply a minimum crop width threshold to prevent small targets (far-away persons) from being over-magnified, and this threshold SHALL scale proportionally with the actual camera input width.

#### Scenario: Far-away target clarity protection
- **WHEN** camera input width is 3840 (4K) and target detection width is 40 pixels (far-away person)
- **THEN** the minimum crop width applied SHALL scale up by 2.0x relative to the 1080p base threshold (e.g., 240 * 2 = 480 pixels), preventing pixelation

### Requirement: Tight Layout Margin for Dense Grids
The system SHALL dynamically decrease the padding margin and lower the minimum crop width limit for dense grid configurations (e.g., 8-tile or 16-tile layouts) to maximize target representation inside smaller grid displays.

#### Scenario: Tight padding in 16-tile grid
- **WHEN** the number of active tiles is 16 and target is cropped
- **THEN** the safety padding applied SHALL be reduced to 1.05 and the minimum crop width limit SHALL be reduced to 100 pixels (at 1080p base) to highlight the person

