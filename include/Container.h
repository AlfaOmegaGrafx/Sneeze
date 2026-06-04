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

#ifndef SNEEZE_CONTAINER_H
#define SNEEZE_CONTAINER_H

namespace SNEEZE
{
   class CONTEXT;
   class FABRIC;

   enum eTRUST
   {
      kTRUST_NONE,
      kTRUST_UNTRUSTED,
      kTRUST_UNVERIFIED,
      kTRUST_EXPIRED,
      kTRUST_VERIFIED,
      kTRUST_ROOT,
   };

   class CONTAINER
   {
   public:

      class CID
      {
      public:
         std::string sFingerprint;
         std::string sOrganization;
         std::string sOrganizationHash;
         std::string sContainer;
         std::string sPersonaHash;
         eTRUST      eTrust;

         CID () : eTrust (kTRUST_NONE) {}

         std::string DisplayName () const { return ((eTrust >= kTRUST_EXPIRED) ? sOrganization : sOrganizationHash) + "/" + sContainer; }
         std::string Key         () const { return sPersonaHash.substr (0, 12) + "/" + sFingerprint.substr (0, 2) + "/" + sFingerprint.substr (2, 22) + "/" + sContainer; }
      };

      CONTAINER (CONTEXT* pContext, const CID* pCID);
     ~CONTAINER ();

      CONTAINER & operator= (CONTAINER const  & rhs)   = delete;
      CONTAINER & operator= (CONTAINER       && rhs)   = delete;
      CONTAINER             (CONTAINER const  & other) = delete;
      CONTAINER             (CONTAINER       && other) = delete;

      bool    Open  (FABRIC* pFabric);
      size_t  Close (FABRIC* pFabric);

      const CID*         Identity () const;
      const std::string& Key      () const;

   private:
      class Impl;
      Impl* m_pImpl;
   };
}

#endif // SNEEZE_CONTAINER_H
