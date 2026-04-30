# XR — OpenXR Runtime Detection

The `xr` module probes for an OpenXR runtime on the system and reports its
availability and name. This is used to determine whether VR/AR headset
rendering is possible.

## XR_RUNTIME

```cpp
#include "xr/XrRuntime.h"

sneeze::xr::XR_RUNTIME xr;
xr.Initialize ();

if (xr.HasRuntime ())
{
   std::string sName = xr.GetRuntimeName ();
   // e.g. "Oculus", "SteamVR", "Windows Mixed Reality"
}

xr.Shutdown ();
```

### Behavior

`Initialize()` attempts to create an `XrInstance`. If an OpenXR runtime is
installed and responsive, `HasRuntime()` returns true and the runtime name
is available via `GetRuntimeName()`. If no runtime is found, initialization
succeeds silently (non-fatal) and `HasRuntime()` returns false.

## Dependencies

- **OpenXR SDK** — linked at build time. Requires the OpenXR loader
  (`openxr_loader`) and headers.

## Unimplemented / Future Work

- **Session creation** — the runtime detects OpenXR but does not create a
  session, reference space, or swapchain.
- **Stereo rendering** — no stereo camera or dual-viewport rendering path.
- **Controller input** — no action sets, bindings, or haptic feedback.
- **Compositor integration** — the compositor does not yet have an XR frame
  loop (wait frame, begin frame, acquire swapchain, end frame).
- **Passthrough / AR** — no mixed-reality or passthrough support.
