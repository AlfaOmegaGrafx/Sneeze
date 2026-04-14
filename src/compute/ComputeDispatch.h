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

#ifndef SNEEZE_COMPUTE_COMPUTE_DISPATCH_H
#define SNEEZE_COMPUTE_COMPUTE_DISPATCH_H

#include <anari/anari.h>
#include <cstdint>
#include <cstddef>
#include <string>
#include <unordered_map>

namespace sneeze
{
namespace compute
{

struct BUFFER_BINDING
{
   uint32_t nBinding;
   void*    pData;
   size_t   nSize;
   bool     bReadOnly;
};

typedef void (*CpuKernelFn) (
   BUFFER_BINDING* aBindings,
   uint32_t        nBindingCount,
   const void*     pPushConstants,
   size_t          nPushConstantSize,
   uint32_t        nGroupsX,
   uint32_t        nGroupsY,
   uint32_t        nGroupsZ
);

class COMPUTE_DISPATCH
{
public:
   COMPUTE_DISPATCH (ANARIDevice pDevice);
   ~COMPUTE_DISPATCH ();

   bool SupportsNativeCompute () const;
   void RegisterCpuKernel (const char* szName, CpuKernelFn fnKernel);

   bool Dispatch (
      const char*     szKernelName,
      uint32_t        nGroupsX,
      uint32_t        nGroupsY,
      uint32_t        nGroupsZ,
      BUFFER_BINDING* aBindings,
      uint32_t        nBindingCount,
      const void*     pPushConstants,
      size_t          nPushConstantSize
   );

private:
   ANARIDevice                                  m_pDevice;
   void                                         (*m_pfnNativeDispatch) ();
   std::unordered_map<std::string, CpuKernelFn> m_aCpuKernels;
};

} // namespace compute
} // namespace sneeze

#endif // SNEEZE_COMPUTE_COMPUTE_DISPATCH_H
