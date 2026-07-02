# Crop Layout Specification

## Purpose
The purpose of this specification is to define the crop layout requirements for the UVC camera application, specifically focusing on adaptive aspect ratio cropping and head-centered vertical offsets for tracked persons on the RV1126 platform.

## Requirements

### Requirement: Adaptive Aspect Ratio Person Cropping
The PersonTracker SHALL dynamically adjust the bounding box dimensions of tracked persons to match the aspect ratio of the target grid display cell based on the UVC canvas dimensions and the maximum active tile count configuration.

#### Scenario: Width expansion for wide layout cells
- **WHEN** the target grid display cell aspect ratio is wider than the person's bounding box aspect ratio
- **THEN** the crop box width SHALL be expanded to match the target aspect ratio while maintaining the vertical bounds

#### Scenario: Height expansion for narrow layout cells
- **WHEN** the target grid display cell aspect ratio is narrower than the person's bounding box aspect ratio
- **THEN** the crop box height SHALL be expanded to match the target aspect ratio while maintaining the horizontal bounds

### Requirement: Head Centered Vertical Offset
The PersonTracker SHALL offset the vertical center of the person crop box upwards to ensure the head is not at the top edge of the crop box.

#### Scenario: Vertically shift center upwards
- **WHEN** calculating the crop box coordinates for a tracked person
- **THEN** the vertical center coordinate of the crop box SHALL be shifted upward by 12% of the person's detected height before bounds clamping and alignment
