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

SNEEZE::AGENT::AGENT (POOL* pPool, int nAgentIz) : THREAD (),
   m_pPool    (pPool),
   m_nAgentIz (nAgentIz),
   m_bBusy    (false)
{
}

SNEEZE::AGENT::~AGENT ()
{
   Join ();
}

SNEEZE::ENGINE* SNEEZE::AGENT::Engine () const
{
   return m_pPool->Engine ();
}

bool SNEEZE::AGENT::Busy ()
{
   bool bExpected = false;

   return m_bBusy.compare_exchange_strong (bExpected, true, std::memory_order_acq_rel, std::memory_order_relaxed);
}
