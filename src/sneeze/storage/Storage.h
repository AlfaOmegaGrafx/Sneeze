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

#ifndef SNEEZE_STORAGE_ISTORAGEIMPL_H
#define SNEEZE_STORAGE_ISTORAGEIMPL_H

namespace SNEEZE
{
   class ISTORAGE_IMPL
   {
   public:
      ISTORAGE_IMPL ();
      virtual ~ISTORAGE_IMPL ();

      virtual SASSET*            Asset_Open  (STORAGE::eSCOPE eScope, const std::string& sPathname)                        = 0;
      virtual void               Asset_Close (SASSET* pAsset)                                                              = 0;

      virtual void               Log (IENGINE::eLOGLEVEL Level, const std::string& sModule, const std::string& sMessage)   = 0;

      virtual const std::string& Path_Permanent () const                                                                   = 0;
      virtual const std::string& Path_Temporary () const                                                                   = 0;

      virtual ICONTEXT*          Host () const                                                                             = 0;

   private:
   };
}
#endif // SNEEZE_STORAGE_ISTORAGEIMPL_H
