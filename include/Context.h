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

#ifndef SNEEZE_CONTEXT_H
#define SNEEZE_CONTEXT_H

#include <string>

namespace SNEEZE
{
   class ENGINE;
   class ICONTEXT;
   class IVIEWPORT;
   class CONSOLE;
   class NETWORK;
   class STORAGE;
   class VIEWPORT;

   class CONTEXT
   {
   public:

      // ---------------------------------------------------------------------------
      // CONTEXT::CONTAINER - the runtime manifestation of an MSF file.
      //
      // CID is the identity record for a container. Uniqueness is determined by
      // the tuple (persona hash, fingerprint, container name).
      // ---------------------------------------------------------------------------

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
      };

      enum eSESSION
      {
         kSESSION_PERSISTENT,
         kSESSION_TRANSITORY,
      };

      CONTEXT (ENGINE* pEngine, ICONTEXT* pHost, eSESSION kSession, const std::string& sPath_Permanent, const std::string& sPath_Temporary);

      CONTEXT & operator= (CONTEXT const  & rhs)   = delete;
      CONTEXT & operator= (CONTEXT       && rhs)   = delete;
      CONTEXT             (CONTEXT const  & other) = delete;
      CONTEXT             (CONTEXT       && other) = delete;
      ~CONTEXT            ();

      bool Initialize (const std::string& sUrl);
      void Logout ();

      ENGINE*    Engine   () const;
      ICONTEXT*  Host     () const;

      CONSOLE*   Console  () const;
      NETWORK*   Network  () const;
      STORAGE*   Storage  () const;
      VIEWPORT*  Viewport () const;

      const std::string& Path_Permanent () const;
      const std::string& Path_Temporary () const;

      const CONTAINER::CID* CID_Pool (const CONTAINER::CID* pCID);

   private:
      class Impl;
      Impl* m_pImpl;
   };
}

#endif // SNEEZE_CONTEXT_H
