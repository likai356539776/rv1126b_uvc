# Performance Specification

## Purpose
The purpose of this specification is to define performance requirements and constraints for the UVC camera application, specifically focusing on CPU optimization under target tracking and streaming conditions on the RV1126 platform.

## Requirements

### Requirement: Low CPU Overhead on RV1126
The UVC camera application SHALL utilize minimal CPU footprint under target tracking and streaming conditions.

#### Scenario: Active target tracking
- **WHEN** multiple persons are actively tracked by YOLO NPU and streamer pool is compositing frames
- **THEN** single-core CPU utilization of the application MUST be under 40%
