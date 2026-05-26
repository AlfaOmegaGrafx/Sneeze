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

#include "Control.h"

namespace SNEEZE
{
   // =========================================================================
   // POOL_CYCLE
   // =========================================================================

   void POOL_CYCLE::Post (JOB_COMPOSITOR* pJob)
   {
      std::lock_guard<std::mutex> guard (m_mxCycle);

      m_apJob.push_back (pJob);

      for (AGENT* pAgent : m_apAgent)
      {
         if (pAgent->Busy ())
         {
            pAgent->Signal ();

            break;
         }
      }
   }

   bool POOL_CYCLE::Grab (JOB_COMPOSITOR*& pJob, int nAgentIz)
   {
      std::lock_guard<std::mutex> guard (m_mxCycle);

      pJob = nullptr;

      for (JOB_COMPOSITOR* pJob_It : m_apJob)
      {
         if (pJob_It->Busy ())
         {
            JOB_COMPOSITOR::eSTATE eState = pJob_It->State ();

            if (eState == JOB_COMPOSITOR::kSTATE_CREATE  ||  eState == JOB_COMPOSITOR::kSTATE_DESTROY)
            {
               if (nAgentIz == 0)
               {
                  if (pJob)
                     pJob->Idle ();
   
                  pJob = pJob_It;
                  break;
               }
               else pJob_It->Idle ();
            }
            else
            {
               if (!pJob  ||  pJob_It->m_nLastFrame < pJob->m_nLastFrame)
               {
                  if (pJob)
                     pJob->Idle ();

                  pJob = pJob_It;
               }
               else pJob_It->Idle ();
            }
         }
      }

      if (pJob)
         pJob->Unlock ();

      return (pJob != nullptr);
   }

   void POOL_CYCLE::Remove (JOB_COMPOSITOR* pJob)
   {
      std::lock_guard<std::mutex> guard (m_mxCycle);

      auto it = std::find (m_apJob.begin (), m_apJob.end (), pJob);
      if (it != m_apJob.end ())
         m_apJob.erase (it);
   }
}
