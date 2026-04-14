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

#include "compute/ComputeDispatch.h"
#include "compute/EmbeddedKernels.h"
#include <anari/ext/anari_ext_interface.h>
#include <cmath>
#include <cstring>

namespace sneeze
{
namespace compute
{

// ---------------------------------------------------------------------------
// ANARI compute extension
// ---------------------------------------------------------------------------

static const char* ANARI_COMPUTE_EXT = "SNEEZE_dispatch_compute";

typedef bool (*PFN_sneeze_dispatch_compute) (
   ANARIDevice     pDevice,
   const uint8_t*  pSpvBytes,
   size_t          nSpvSize,
   uint32_t        nGroupsX,
   uint32_t        nGroupsY,
   uint32_t        nGroupsZ,
   BUFFER_BINDING* aBindings,
   uint32_t        nBindingCount,
   const void*     pPushConstants,
   size_t          nPushConstantSize
);

// ---------------------------------------------------------------------------
// CPU fallback: test_proximity kernel
// ---------------------------------------------------------------------------

static void CpuProximityKernel (
   BUFFER_BINDING* aBindings,
   uint32_t        nBindingCount,
   const void*     pPushConstants,
   size_t          nPushConstantSize,
   uint32_t        nGroupsX,
   uint32_t        nGroupsY,
   uint32_t        nGroupsZ
)
{
   struct PC
   {
      float    dX, dY, dZ, dW;
      uint32_t nCount;
   };

   if (nPushConstantSize < sizeof (PC))
      return;

   PC pPC;
   std::memcpy (&pPC, pPushConstants, sizeof (PC));

   const float* pPositions = nullptr;
   float*       pDistances = nullptr;

   for (uint32_t i = 0; i < nBindingCount; i++)
   {
      if (aBindings[i].nBinding == 0)
         pPositions = static_cast<const float*> (aBindings[i].pData);
      if (aBindings[i].nBinding == 1)
         pDistances = static_cast<float*> (aBindings[i].pData);
   }

   if (!pPositions || !pDistances)
      return;

   for (uint32_t i = 0; i < pPC.nCount; i++)
   {
      float dDx = pPositions[i * 4 + 0] - pPC.dX;
      float dDy = pPositions[i * 4 + 1] - pPC.dY;
      float dDz = pPositions[i * 4 + 2] - pPC.dZ;
      pDistances[i] = std::sqrt (dDx * dDx + dDy * dDy + dDz * dDz);
   }
}

// ---------------------------------------------------------------------------
// COMPUTE_DISPATCH
// ---------------------------------------------------------------------------

COMPUTE_DISPATCH::COMPUTE_DISPATCH (ANARIDevice pDevice)
   : m_pDevice (pDevice)
   , m_pfnNativeDispatch (nullptr)
{
   if (m_pDevice)
      m_pfnNativeDispatch = anariDeviceGetProcAddress (m_pDevice, ANARI_COMPUTE_EXT);

   RegisterCpuKernel ("TEST_PROXIMITY", CpuProximityKernel);
}

COMPUTE_DISPATCH::~COMPUTE_DISPATCH ()
{
}

bool COMPUTE_DISPATCH::SupportsNativeCompute () const
{
   return m_pfnNativeDispatch != nullptr;
}

void COMPUTE_DISPATCH::RegisterCpuKernel (const char* szName, CpuKernelFn fnKernel)
{
   m_aCpuKernels[szName] = fnKernel;
}

bool COMPUTE_DISPATCH::Dispatch (
   const char*     szKernelName,
   uint32_t        nGroupsX,
   uint32_t        nGroupsY,
   uint32_t        nGroupsZ,
   BUFFER_BINDING* aBindings,
   uint32_t        nBindingCount,
   const void*     pPushConstants,
   size_t          nPushConstantSize
)
{
   if (!szKernelName)
      return false;

   if (m_pfnNativeDispatch && m_pDevice)
   {
      KERNEL_DATA pKernel = GetEmbeddedKernel (szKernelName);
      if (pKernel.pBytes && pKernel.nSize > 0)
      {
         auto pfn = reinterpret_cast<PFN_sneeze_dispatch_compute> (m_pfnNativeDispatch);
         return pfn (m_pDevice, pKernel.pBytes, pKernel.nSize,
            nGroupsX, nGroupsY, nGroupsZ,
            aBindings, nBindingCount,
            pPushConstants, nPushConstantSize);
      }
   }

   auto it = m_aCpuKernels.find (szKernelName);
   if (it != m_aCpuKernels.end ())
   {
      it->second (aBindings, nBindingCount,
         pPushConstants, nPushConstantSize,
         nGroupsX, nGroupsY, nGroupsZ);
      return true;
   }

   return false;
}

} // namespace compute
} // namespace sneeze
