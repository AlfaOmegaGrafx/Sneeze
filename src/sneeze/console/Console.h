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

#ifndef SNEEZE_CONSOLE_ICONSOLEIMPL_H
#define SNEEZE_CONSOLE_ICONSOLEIMPL_H

namespace SNEEZE
{
   class ICONSOLE_IMPL
   {
   public:
      ICONSOLE_IMPL ();
      virtual ~ICONSOLE_IMPL ();

      virtual const std::string& Path_Temporary () const                         = 0;

      virtual BLOCK* Block_Open (uint32_t nIndex, const std::string& sPathname)  = 0;
      virtual void   Block_Close (BLOCK* pBlock) = 0;

      virtual std::shared_ptr<const CONSOLE::ENTRY> Entry_Create (const CONTEXT::CONTAINER::CID* pCID, CONSOLE::eLEVEL eLevel, const std::string& sMessage, uint32_t nGroupDepth, bool bCollapsed) = 0;

   private:
   };
}
#endif // SNEEZE_NETWORK_INETWORKIMPL_H
