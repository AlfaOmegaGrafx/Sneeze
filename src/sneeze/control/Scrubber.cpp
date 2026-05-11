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

#include <Sneeze.h>
#include "Control.h"
#include <filesystem>
#include <cstdio>

using namespace SNEEZE;

AGENT::SCRUBBER::SCRUBBER (CONTROL* pControl)
   : AGENT (pControl)
{
}

void AGENT::SCRUBBER::Tick ()
{
}

void AGENT::SCRUBBER::DrainQueue ()
{
   std::vector<std::string> aPath;
   m_pControl->Cleanup_SwapQueue (aPath);

   std::string sMarker = std::string ("/") + ENGINE::sFOLDER_TRANSITORY + "/";

   for (const auto& sPath : aPath)
   {
      if (sPath.find (sMarker) != std::string::npos)
      {
         std::error_code ec;
         std::filesystem::remove_all (sPath, ec);

         if (!ec)
         {
            Engine ()->Log (IENGINE::kLOGLEVEL_Trace, "SCRUBBER", "Removed " + sPath);
         }
         else Engine ()->Log (IENGINE::kLOGLEVEL_Warning, "SCRUBBER", "Failed to remove " + sPath + ": " + ec.message ());
      }
      else Engine ()->Log (IENGINE::kLOGLEVEL_Error, "SCRUBBER", "REJECTED -- path is not under " + std::string (ENGINE::sFOLDER_TRANSITORY) + "/: " + sPath);
   }
}

void AGENT::SCRUBBER::ThreadLoop ()
{
   SignalReady ();

   while (!IsShutdown ())
   {
      DrainQueue ();

      std::unique_lock<std::mutex> lock (m_mxControl);
      m_cvControl.wait (lock, [this] { return IsShutdown (); });
   }
}
