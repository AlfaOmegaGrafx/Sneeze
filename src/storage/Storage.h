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

#ifndef SNEEZE_STORAGE_STORAGE_H
#define SNEEZE_STORAGE_STORAGE_H

#include <string>
#include <unordered_map>
#include <vector>
#include <mutex>
#include <memory>

namespace SNEEZE { namespace CORE { class SNEEZE; }}

namespace SNEEZE {

// ===========================================================================
// STORAGE — persistent per-persona/per-org/per-container key-value storage.
//
// Analogous to localStorage in a web browser. Data is stored as JSON files
// on disk and survives application restarts.
//
// Hierarchy: STORAGE -> PERSONA_STORE -> FINGERPRINT -> CONTAINER
// On disk:   <SessionPath>/Storage/<persona>/<fingerprint>/container-<name>.json
// ===========================================================================

class STORAGE
{
public:

   // -----------------------------------------------------------------------
   // CONTAINER — per-container key-value store.
   //
   // On disk: <root>/<persona>/<fingerprint>/container-<name>.json
   // Thread safety: mutex per container, write-on-set.
   // -----------------------------------------------------------------------

   class CONTAINER
   {
   public:
      CONTAINER (const std::string& sName, const std::string& sDiskPath);

      const std::string& GetName () const { return m_sName; }

      std::string Get (const std::string& sKey) const;
      void        Set (const std::string& sKey, const std::string& sValue);
      void        Remove (const std::string& sKey);
      bool        Has (const std::string& sKey) const;

      void Load ();
      void Save () const;

   private:
      std::string                                m_sName;
      std::string                                m_sDiskPath;
      std::unordered_map<std::string, std::string> m_mapData;
      mutable std::mutex                         m_mutex;
   };

   // -----------------------------------------------------------------------
   // FINGERPRINT — per-organization scope.
   //
   // Owns a "common" store (shared across containers of same org) and a map
   // of named CONTAINERs.
   // On disk: <root>/<persona>/<fingerprint>/common.json
   // -----------------------------------------------------------------------

   class FINGERPRINT
   {
   public:
      FINGERPRINT (const std::string& sFingerprint, const std::string& sBasePath);

      const std::string& GetFingerprint () const { return m_sFingerprint; }

      // Common (organization-shared) storage
      std::string Common_Get (const std::string& sKey) const;
      void        Common_Set (const std::string& sKey, const std::string& sValue);
      void        Common_Remove (const std::string& sKey);
      bool        Common_Has (const std::string& sKey) const;

      // Per-container storage
      CONTAINER*  GetContainer (const std::string& sName);

      void Load ();
      void Save () const;

   private:
      std::string                                          m_sFingerprint;
      std::string                                          m_sBasePath;
      std::unordered_map<std::string, std::string>         m_mapCommon;
      mutable std::mutex                                   m_commonMutex;
      std::unordered_map<std::string, std::unique_ptr<CONTAINER>> m_mapContainers;
      std::mutex                                           m_containersMutex;
   };

   // -----------------------------------------------------------------------
   // PERSONA_STORE — per-persona root of the storage hierarchy.
   //
   // On disk: <root>/<persona>/
   // -----------------------------------------------------------------------

   class PERSONA_STORE
   {
   public:
      PERSONA_STORE (const std::string& sPersonaHash, const std::string& sBasePath);

      const std::string& GetPersonaHash () const { return m_sPersonaHash; }

      FINGERPRINT* GetFingerprint (const std::string& sFingerprint);

   private:
      std::string                                               m_sPersonaHash;
      std::string                                               m_sBasePath;
      std::unordered_map<std::string, std::unique_ptr<FINGERPRINT>> m_mapFingerprints;
      std::mutex                                                m_mutex;
   };

   // -----------------------------------------------------------------------
   // STORAGE public API
   // -----------------------------------------------------------------------

   explicit STORAGE (CORE::SNEEZE* pSneeze);
   ~STORAGE ();

   bool Initialize ();
   void Shutdown ();

   PERSONA_STORE* GetPersona (const std::string& sPersonaHash);

private:
   std::string GetStorageRootPath () const;

   CORE::SNEEZE*                                                m_pSneeze;
   std::string                                                  m_sRootPath;
   std::unordered_map<std::string, std::unique_ptr<PERSONA_STORE>> m_mapPersonas;
   std::mutex                                                   m_mutex;
};

} // namespace SNEEZE

#endif // SNEEZE_STORAGE_STORAGE_H
