# View — Camera Controls

The camera orbit functionality is now part of `VIEWPORT::VIEW`, a struct
declared inside `Viewport.h`. The `view/` module directory is retained for
documentation and future camera modes.

## VIEWPORT::VIEW

An orbiting camera that rotates around a target point. Each VIEWPORT owns
one VIEW instance, updated by the compositor from that viewport's input.

```cpp
SNEEZE::VIEWPORT::VIEW& view = pViewport->View ();
view.dTheta    = 0.0f;      // azimuthal angle (radians)
view.dPhi      = 0.3f;      // polar angle (radians)
view.dDistance  = 10.0f;     // distance from target
view.dTargetX  = 0.0f;      // look-at point
view.dTargetY  = 0.0f;
view.dTargetZ  = 0.0f;

// Each frame, the compositor feeds input deltas
view.Update (nDX, nDY, dScrollY, bMouseLeft, bMouseRight);
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

- **First-person camera** -- a walk/fly camera for terrestrial navigation
  (when the user is "on the surface" of a planet or inside a building).
- **Camera transitions** -- smooth animated transitions between camera
  modes or between targets (e.g., clicking a planet to fly to it).
- **Camera constraints** -- clamping phi to prevent flipping, minimum
  zoom distance, collision with geometry.
