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

namespace SNEEZE
{
   // =========================================================================
   // POOL_QUEUE (explicit instantiation: IFETCH*, ISCRUB* only)
   // =========================================================================

   template <typename JOB_PTR>
   void POOL_QUEUE<JOB_PTR>::Post (JOB_PTR pJob)
   {
      std::lock_guard<std::mutex> guard (m_mxQueue);

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

   template <typename JOB_PTR>
   bool POOL_QUEUE<JOB_PTR>::Grab (JOB_PTR& pJob)
   {
      std::lock_guard<std::mutex> guard (m_mxQueue);

      bool bResult = false;

      if (!m_apJob.empty ())
      {
         pJob = m_apJob.front ();

         m_apJob.erase (m_apJob.begin ());

         bResult = true;
      }
      else pJob = JOB_PTR{};

      return bResult;
   }

   template class POOL_QUEUE<ISCRUB*>;
   template class POOL_QUEUE<IFETCH*>;
}
