## ADDED Requirements

### Requirement: Relative Target-Size Deadzone
The system SHALL dynamically compute the jitter deadzone thresholds for the X and Y axes independently as a percentage of the target's current size, ensuring that targets must move beyond 1/3 of the crop dimension in large grids to trigger tracking.

#### Scenario: 1/3 width displacement tracking
- **WHEN** target width is 300 pixels and strategy defines 156px limit for 470px base (33.2% ratio)
- **THEN** the computed horizontal deadzone (`dist_limit_x`) SHALL be 99.6 pixels, and tracking SHALL NOT start if horizontal displacement is below 99.6 pixels

### Requirement: Physical Minimum Clamping for Jitter Deadzones
The system SHALL clamp the computed自适应位移与尺寸死区 to physical pixel minimum limits to prevent deadzones from collapsing to zero or sub-pixel levels for extremely distant targets.

#### Scenario: Clamping extremely small target deadzone
- **WHEN** target width is 50 pixels
- **THEN** the computed位移deadzone (`D_dist`) SHALL be clamped to its minimum limit of 4.0 pixels (instead of 2.5px), and the size deadzone (`D_scale`) SHALL be clamped to 6.0 pixels (instead of 4.0px)

### Requirement: Relative Convergence and Locking Limits
The system SHALL scale the motion convergence locking limits proportionally with the target's width (1.5% for displacement and 2.0% for scale size) to cleanly truncate the exponential decay smoothing long-tail, clamping to minimum boundaries to prevent sub-pixel jelly-effect crawling.

#### Scenario: Convergence locking for 800px target
- **WHEN** the target width is 800 pixels and it is moving in the smooth state
- **THEN** the lock distance limit (`lock_dist`) SHALL be 12.0 pixels (1.5% of 800px) and the lock scale limit (`lock_scale`) SHALL be 16.0 pixels (2.0% of 800px)
