# View — Camera Controls

The `view` module provides camera control primitives used by the compositor
to transform user input into camera movement.

## CAMERA_ORBIT

An orbiting camera that rotates around a target point. Used by the
compositor to navigate the solar system view.

```cpp
#include "view/CameraOrbit.h"

sneeze::view::CAMERA_ORBIT camera;
camera.dTheta    = 0.0f;      // azimuthal angle (radians)
camera.dPhi      = 0.3f;      // polar angle (radians)
camera.dDistance  = 10.0f;     // distance from target
camera.dTargetX  = 0.0f;      // look-at point
camera.dTargetY  = 0.0f;
camera.dTargetZ  = 0.0f;

// Each frame, feed mouse input
sneeze::view::UpdateCameraOrbit (camera, nDX, nDY, dScrollY, bMouseLeft, bMouseRight);
```

### Controls

| Input        | Action                                 |
|--------------|----------------------------------------|
| Left drag    | Orbit (rotate theta/phi)               |
| Right drag   | Pan (translate target)                 |
| Scroll wheel | Zoom (adjust distance)                 |

### Coordinate System

The camera computes a spherical-to-Cartesian position from `dTheta`,
`dPhi`, and `dDistance`, then looks at `(dTargetX, dTargetY, dTargetZ)`.
The compositor converts this into `CAMERA_DATA` for the renderer.

## Unimplemented / Future Work

- **First-person camera** — a walk/fly camera for terrestrial navigation
  (when the user is "on the surface" of a planet or inside a building).
- **Camera transitions** — smooth animated transitions between camera
  modes or between targets (e.g., clicking a planet to fly to it).
- **Camera constraints** — clamping phi to prevent flipping, minimum
  zoom distance, collision with geometry.
