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

#ifndef SNEEZE_SOM_TYPES_H
#define SNEEZE_SOM_TYPES_H

#include <atomic>
#include <cstdint>

namespace som {

// ---------------------------------------------------------------------------
// Map object type identifiers
// ---------------------------------------------------------------------------

enum MAP_OBJECT_TYPE
{
   MAP_OBJECT_TYPE_ROOT        = 0,
   MAP_OBJECT_TYPE_CELESTIAL   = 1,
   MAP_OBJECT_TYPE_TERRESTRIAL = 2,
   MAP_OBJECT_TYPE_PHYSICAL    = 3,
};

// ---------------------------------------------------------------------------
// CAS Multi-Writer Seqlock
//
// Writers: atomically increment the sequence number (odd = write in progress),
// write their data, then increment again (even = stable). Readers spin until
// they observe a stable even sequence that doesn't change across their read.
// ---------------------------------------------------------------------------

class SEQLOCK
{
public:
   SEQLOCK () : m_nSequence (0) {}

   uint32_t BeginRead () const
   {
      uint32_t nSeq;
      do
      {
         nSeq = m_nSequence.load (std::memory_order_acquire);
      }
      while (nSeq & 1);
      return nSeq;
   }

   bool EndRead (uint32_t nSeq) const
   {
      std::atomic_thread_fence (std::memory_order_acquire);
      return m_nSequence.load (std::memory_order_relaxed) == nSeq;
   }

   void BeginWrite ()
   {
      uint32_t nExpected = m_nSequence.load (std::memory_order_relaxed);
      uint32_t nDesired;
      do
      {
         while (nExpected & 1)
            nExpected = m_nSequence.load (std::memory_order_relaxed);
         nDesired = nExpected + 1;
      }
      while (!m_nSequence.compare_exchange_weak (nExpected, nDesired,
         std::memory_order_acquire, std::memory_order_relaxed));
   }

   void EndWrite ()
   {
      m_nSequence.fetch_add (1, std::memory_order_release);
   }

private:
   std::atomic<uint32_t> m_nSequence;
};

} // namespace som

#endif // SNEEZE_SOM_TYPES_H
