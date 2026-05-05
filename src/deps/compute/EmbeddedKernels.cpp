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

#include "compute/EmbeddedKernels.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <cstring>

// Provided by the generated kernels_embedded.c
extern "C"
{
   struct KernelEntry { const char* name; const uint8_t* data; size_t size; };
   extern const KernelEntry g_embedded_kernels[];
   extern const size_t g_embedded_kernel_count;
}
#endif

namespace DEP
{

KERNEL_DATA GetEmbeddedKernel (const char* szName)
{
   KERNEL_DATA pResult = { nullptr, 0 };

#ifdef _WIN32
   HMODULE pModule = GetModuleHandleW (nullptr);
   if (pModule)
   {
      HRSRC pResource = FindResourceA (pModule, szName, "SPV_KERNEL");
      if (pResource)
      {
         HGLOBAL pLoaded = LoadResource (pModule, pResource);
         if (pLoaded)
         {
            pResult.pBytes = static_cast<const uint8_t*> (LockResource (pLoaded));
            pResult.nSize  = SizeofResource (pModule, pResource);
         }
      }
   }
#else
   for (size_t i = 0; i < g_embedded_kernel_count; ++i)
   {
      if (std::strcmp (g_embedded_kernels[i].name, szName) == 0)
      {
         pResult.pBytes = g_embedded_kernels[i].data;
         pResult.nSize  = g_embedded_kernels[i].size;
         break;
      }
   }
#endif

   return pResult;
}

} // namespace DEP
