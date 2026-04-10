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
#endif

namespace sneeze
{
namespace compute
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
#endif

   return pResult;
}

} // namespace compute
} // namespace sneeze
