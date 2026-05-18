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

void JOB_SCRUB::Complete_Deliver ()
{
   OnScrub_Complete ();
}

JOB_SCRUB::JOB_SCRUB (const std::string& sPath) :
   m_sPath (sPath)
{
}

AGENT::SCRUB::SCRUB (POOL* pPool, int nAgentIz)
   : AGENT (pPool, nAgentIz)
{
}

AGENT::SCRUB::~SCRUB ()
{
   Join ();
}

void AGENT::SCRUB::Main ()
{
   Ready ();

   Wait ([this] { return Job (); });
}

bool AGENT::SCRUB::Job ()
{
   bool bResult, bJob;
   ISCRUB* pJob = nullptr;
   auto* pQueue = static_cast<POOL_QUEUE<ISCRUB*>*> (m_pPool);

   while (true)
   {
      bResult = IsShutdown ();
      bJob    = pQueue->Grab (pJob);

      m_bBusy.store (bJob, std::memory_order_release);

      if (bJob)  // flush out all jobs before shutdown
      {
         if (!pJob->IsCancelled ())
         {
            std::string sMarker = std::string ("/") + ENGINE::sFOLDER_TRANSITORY + "/";

            if (pJob->Path ().find (sMarker) != std::string::npos)
            {
               std::error_code ec;
               std::filesystem::remove_all (pJob->Path (), ec);

               if (!ec)
               {
                  Engine ()->Log (IENGINE::kLOGLEVEL_Trace, "SCRUB", "Removed " + pJob->Path ());
               }
               else Engine ()->Log (IENGINE::kLOGLEVEL_Warning, "SCRUB", "Failed to remove " + pJob->Path () + ": " + ec.message ());
            }
            else Engine ()->Log (IENGINE::kLOGLEVEL_Error, "SCRUB", "REJECTED -- path is not under " + std::string (ENGINE::sFOLDER_TRANSITORY) + "/: " + pJob->Path ());
         }

         pJob->Complete ();
      }
      else break;
   }

   return bResult;
}
