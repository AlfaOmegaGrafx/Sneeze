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

   class CONTAINER
   {
   public:

      class CID
      {
      public:
         std::string sFingerprint;
         std::string sOrganization;
         std::string sCommonName;
         std::string sContainerName;
         std::string sPersonaHash;
         bool        bValidated;

         std::string DisplayName () const { return sCommonName + "/" + sContainerName; }
         std::string Key         () const { return sPersonaHash.substr (0, 12) + "/" + sFingerprint.substr (0, 2) + "/" + sFingerprint.substr (2, 22) + "/" + sContainerName; }
      };

      CONTAINER (CONTEXT* pContext, const CID* pCID);
     ~CONTAINER ();

      CONTAINER & operator= (CONTAINER const  & rhs)   = delete;
      CONTAINER & operator= (CONTAINER       && rhs)   = delete;
      CONTAINER             (CONTAINER const  & other) = delete;
      CONTAINER             (CONTAINER       && other) = delete;

      bool Initialize ();
      void Shutdown   ();

      int  Open  ();
      int  Close ();
      int  Count () const;

      CONTEXT*   Context () const;
      const CID* CID_Get () const;

   private:
      class Impl;
      Impl* m_pImpl;
   };
}

#endif // SNEEZE_CONTAINER_H
