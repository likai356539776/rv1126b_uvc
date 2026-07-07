## ADDED Requirements

### Requirement: Specify Camera Node via Command Line Argument
The start script `start_uvc_pip.sh` SHALL accept an optional second argument representing the target camera device node.

#### Scenario: Providing valid camera node as argument
- **WHEN** the user executes `start_uvc_pip.sh v4l2 /dev/video53`
- **THEN** the pipeline starts with `/dev/video53` as the input camera node.

### Requirement: Specify Camera Node via Environment Variable
The start script `start_uvc_pip.sh` SHALL accept an environment variable `CAMERA_NODE` to override the camera node when starting in v4l2 mode.

#### Scenario: Overriding via environment variable
- **WHEN** the user executes `CAMERA_NODE=/dev/video53 start_uvc_pip.sh v4l2`
- **THEN** the pipeline starts with `/dev/video53` as the input camera node.

### Requirement: Default Camera Node
The start script `start_uvc_pip.sh` SHALL fall back to `/dev/video51` if no argument is provided and `CAMERA_NODE` environment variable is not set.

#### Scenario: Default behavior
- **WHEN** the user executes `start_uvc_pip.sh v4l2`
- **THEN** the pipeline starts with `/dev/video51` as the input camera node.

### Requirement: Argument Precedence over Environment Variable
If both the command line argument and the environment variable are provided, the command line argument SHALL take precedence.

#### Scenario: Both argument and environment variable provided
- **WHEN** the user executes `CAMERA_NODE=/dev/video51 start_uvc_pip.sh v4l2 /dev/video53`
- **THEN** the pipeline starts with `/dev/video53` as the input camera node.
