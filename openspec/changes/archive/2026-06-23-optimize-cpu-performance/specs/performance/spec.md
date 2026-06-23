## ADDED Requirements

### Requirement: Low CPU Overhead on RV1126
The UVC camera application SHALL utilize minimal CPU footprint under target tracking and streaming conditions.

#### Scenario: Active target tracking
- **WHEN** multiple persons are actively tracked by YOLO NPU and streamer pool is compositing frames
- **THEN** single-core CPU utilization of the application MUST be under 40%
