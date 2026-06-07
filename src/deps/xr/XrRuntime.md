# XR — OpenXR Runtime Detection

The `xr` module probes for an OpenXR runtime on the system and reports its
availability and name. Used to determine whether VR/AR headset rendering is
possible.

## XR_RUNTIME

```cpp
DEP::XR_RUNTIME xr;
xr.Initialize ();

if (xr.HasRuntime ())
{
   std::string sName = xr.RuntimeName ();
   // e.g. "Oculus", "SteamVR", "Windows Mixed Reality"
}

xr.Shutdown ();
```

`Initialize()` attempts to create an `XrInstance`. If no runtime is found,
initialization succeeds silently (non-fatal) and `HasRuntime()` returns false.

OpenXR loader stderr diagnostics are suppressed via `XR_LOADER_DEBUG=none`.

## Files

| File | Contents |
|------|----------|
| `XrRuntime.h` | XR_RUNTIME declaration |
| `XrRuntime.cpp` | Implementation (instance creation, runtime detection) |
