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

#ifndef SNEEZE_NETWORK_INETWORKIMPL_H
#define SNEEZE_NETWORK_INETWORKIMPL_H

namespace SNEEZE
{
   class INETWORK_IMPL
   {
   public:
      INETWORK_IMPL ();
      virtual ~INETWORK_IMPL ();

      virtual NASSET*            Asset_Open  (NETWORK::FILE* pFile)                                                        = 0;
      virtual void               Asset_Close (NASSET* pAsset, NETWORK::FILE* pFile)                                        = 0;
      virtual uint32_t           Asset_Index ()                                                                            = 0;

      virtual bool               Rules_Stale (NASSET* pAsset) const                                                        = 0;
      virtual void               Queue_Post_Fetch (JOB_FETCH* pJob_Fetch)                                                  = 0;

      virtual void               Log (IENGINE::eLOGLEVEL Level, const std::string& sModule, const std::string& sMessage)   = 0;

      virtual double             SecondsSinceEpoch () const                                                                = 0;
      virtual const std::string& Path_Permanent () const                                                                   = 0;

      virtual ICONTEXT*          Host () const                                                                             = 0;

      virtual void               File_Clear (NETWORK::FILE* pFile)                                                         = 0;
      virtual void               File_Close (NETWORK::FILE* pFile)                                                         = 0;
      virtual void               File_Reset (NETWORK::FILE* pFile)                                                         = 0;

   private:
   };
}
#endif // SNEEZE_NETWORK_INETWORKIMPL_H
