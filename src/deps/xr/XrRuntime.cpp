// Copyright 2026 Metaversal Corporation
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "xr/XrRuntime.h"
#include "Sneeze.h"
#include <cstdlib>
#include <cstring>

namespace DEP
{

XR_RUNTIME::XR_RUNTIME ()
   : m_pSneeze    (nullptr)
   , hInstance   (XR_NULL_HANDLE)
   , bHasRuntime (false)
{
}

XR_RUNTIME::~XR_RUNTIME ()
{
   Shutdown ();
}

bool XR_RUNTIME::Initialize (SNEEZE* pSneeze)
{
   m_pSneeze = pSneeze;
   // Suppress the loader's own stderr diagnostics - we handle all error cases
   // ourselves with clearer messages. Without this, the loader prints alarming
   // "Error [GENERAL | xrCreateInstance | OpenXR-Loader]" lines on machines
   // that simply don't have a VR/AR runtime installed.
#ifdef _WIN32
   _putenv_s ("XR_LOADER_DEBUG", "none");
#else
   setenv ("XR_LOADER_DEBUG", "none", 1);
#endif

   XrApplicationInfo pAppInfo = {};
   std::strncpy (pAppInfo.applicationName, "Sneeze", XR_MAX_APPLICATION_NAME_SIZE);
   pAppInfo.applicationVersion = 1;
   std::strncpy (pAppInfo.engineName, "MBE", XR_MAX_ENGINE_NAME_SIZE);
   pAppInfo.engineVersion = 1;
   pAppInfo.apiVersion    = XR_API_VERSION_1_0;

   XrInstanceCreateInfo pCreateInfo = { XR_TYPE_INSTANCE_CREATE_INFO };
   pCreateInfo.applicationInfo        = pAppInfo;
   pCreateInfo.enabledApiLayerCount   = 0;
   pCreateInfo.enabledExtensionCount  = 0;

   XrResult nResult = xrCreateInstance (&pCreateInfo, &hInstance);
   if (XR_FAILED (nResult))
   {
      bHasRuntime = false;
      m_pSneeze->Log (SNEEZE::ISNEEZE::kLOGLEVEL_Warning, "XR_RUNTIME",
         "OpenXR loader initialized - no XR runtime detected (code " + std::to_string (nResult) + ")");
      m_pSneeze->Log (SNEEZE::ISNEEZE::kLOGLEVEL_Warning, "XR_RUNTIME",
         "This is normal on machines without a VR/AR headset or runtime installed.");
      return true;
   }

   bHasRuntime = true;

   XrInstanceProperties pProps = { XR_TYPE_INSTANCE_PROPERTIES };
   if (XR_SUCCEEDED (xrGetInstanceProperties (hInstance, &pProps)))
   {
      sRuntimeName = pProps.runtimeName;
      m_pSneeze->Log (SNEEZE::ISNEEZE::kLOGLEVEL_Info, "XR_RUNTIME",
         "OpenXR " + std::to_string (XR_VERSION_MAJOR (XR_CURRENT_API_VERSION)) + "."
         + std::to_string (XR_VERSION_MINOR (XR_CURRENT_API_VERSION)) + "."
         + std::to_string (XR_VERSION_PATCH (XR_CURRENT_API_VERSION))
         + " initialized - runtime: " + pProps.runtimeName
         + " (v" + std::to_string (XR_VERSION_MAJOR (pProps.runtimeVersion)) + "."
         + std::to_string (XR_VERSION_MINOR (pProps.runtimeVersion)) + "."
         + std::to_string (XR_VERSION_PATCH (pProps.runtimeVersion)) + ")");
   }

   return true;
}

void XR_RUNTIME::Shutdown ()
{
   if (hInstance != XR_NULL_HANDLE)
   {
      xrDestroyInstance (hInstance);
      hInstance = XR_NULL_HANDLE;
   }
   bHasRuntime = false;
   sRuntimeName.clear ();
}

bool XR_RUNTIME::HasRuntime () const
{
   return bHasRuntime;
}

std::string XR_RUNTIME::GetRuntimeName () const
{
   return sRuntimeName;
}

} // namespace DEP
