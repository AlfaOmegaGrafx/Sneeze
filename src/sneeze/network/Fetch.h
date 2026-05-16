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

#ifndef SNEEZE_NETWORK_FETCH_H
#define SNEEZE_NETWORK_FETCH_H

#include <Sneeze.h>
#include "Engine.h"

namespace SNEEZE
{
   // -----------------------------------------------------------------------
   // FETCH — transient download thread for a single ASSET.
   //
   // Born when an ASSET needs data, downloads via curl, delivers the result
   // back to ASSET::FetchComplete(), and dies. One FETCH per ASSET at a time.
   // -----------------------------------------------------------------------

   class NETWORK::FETCH : public THREAD
   {
   public:
      FETCH (ASSET* pAsset, const std::string& sUrl, const std::string& sPath_Temp, const std::string& sPath_Data, const std::string& sHash);
      ~FETCH ();

      void Main () override;

      const FETCH_RESULT& Result () const;

   private:
      ASSET*         m_pAsset;
      std::string    m_sUrl;
      std::string    m_sPath_Temp;
      std::string    m_sPath_Data;
      std::string    m_sHash;
      FETCH_RESULT   m_Fetch_Result;
   };
}

#endif // SNEEZE_NETWORK_FETCH_H
