## Context

Currently, the person tracker uses fixed pixel thresholds (12.0px/16.0px) for static deadzones and (2.0px/3.0px) for convergence locking. This absolute configuration behaves inconsistently under different input camera resolutions and for targets at varying distances. Small far-away targets look laggy because a 12px shift represents a massive body displacement. Large close targets trigger the smooth state too frequently due to natural minor breathing perturbations.

## Goals / Non-Goals

**Goals:**
- Implement a target relative size adaptive deadzone algorithm in C++ to achieve identical physical jitter filtering feel across all distances and resolutions.
- Protect extremely far-away targets from deadzone collapse (which would lead to permanent micro-crawling) using safety clamps.
- Adjust strategies based on active layout tiles (e.g. relaxed deadzones for large grids, sensitive for small grids).

**Non-Goals:**
- Move parameters back to dynamic INI configuration.

## Decisions

### Decision 1: Target Width (`w_slot`) as Relative Aspect Reference
- **Choice**: Derive all deadzones and lock thresholds proportionally from the tracked slot's current width `slots_[s].w`.
- **Rationale**: Since aspect ratio is mathematically locked by our earlier change, target width scales linearly with overall target area and represents image density perfectly.

### Decision 2: Static Parameter Upgrades in CropStrategy
- **Choice**: Extend the static table `kBaseCropStrategies` in `person_tracker.cc` with `base_dist_limit` and `base_scale_limit`.
- **Rationale**: Keeps target sensitivity tailored to window sizes (1-4 grid needs maximum damping, 10-16 grid needs fast responsiveness).

### Decision 3: Safety Clamping and Axial-Independent Equations
- **Choice**: Force min constraints on computed pixel boundaries with axis-specific limits:
  - $D_{dist\_x} = \max(w_{slot} \times \frac{base\_dist\_limit}{target\_w}, \ 4.0)$
  - $D_{dist\_y} = \max(h_{slot} \times \frac{base\_dist\_limit}{target\_w}, \ 4.0)$
  - $D_{scale} = \max(w_{slot} \times \frac{base\_scale\_limit}{target\_w}, \ 6.0)$
  - $Lock_{dist\_x} = \max(w_{slot} \times 0.015, \ 2.0)$
  - $Lock_{dist\_y} = \max(h_{slot} \times 0.015, \ 2.0)$
  - $Lock_{scale} = \max(w_{slot} \times 0.02, \ 3.0)$
- **Rationale**: Keeps horizontal and vertical tracking hysteresis strictly matching the 1/3 grid aspect limits, while keeping locking threshold highly precise (1.5%-2.0%) to prevent lock lags. Prevents sub-pixel jitter oscillations.

## Risks / Trade-offs

- **[Risk]**: Highly distant small targets could see larger bounding boxes due to clamping, showing more background.
  - **Mitigation**: This is correct and keeps distant targets clear and stable instead of introducing high-frequency noise.
