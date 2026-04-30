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

#ifndef SNEEZE_XR_RUNTIME_H
#define SNEEZE_XR_RUNTIME_H

#include <openxr/openxr.h>
#include <string>

namespace SNEEZE { namespace CORE { class SNEEZE; }}

namespace SNEEZE
{
namespace xr
{

class XR_RUNTIME
{
public:
   XR_RUNTIME ();
   ~XR_RUNTIME ();

   bool Initialize (CORE::SNEEZE* pSneeze);
   void Shutdown ();

   bool        HasRuntime () const;
   std::string GetRuntimeName () const;

private:
   CORE::SNEEZE* m_pSneeze;
   XrInstance  hInstance;
   bool        bHasRuntime;
   std::string sRuntimeName;
};

} // namespace xr
} // namespace SNEEZE

#endif // SNEEZE_XR_RUNTIME_H
