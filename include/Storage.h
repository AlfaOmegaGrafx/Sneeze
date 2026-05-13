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

#include <nlohmann/json.hpp>
#include <string>
#include <vector>
#include <unordered_map>
#include <mutex>
#include <memory>
#include <cstdint>
#include <fstream>
#include <filesystem>

namespace SNEEZE
{
   // ---------------------------------------------------------------------------
   // STORAGE - persistent per-persona/per-org/per-container JSON document store.
   //
   // Analogous to localStorage/sessionStorage in a web browser. Each container
   // gets four independent JSON document stores (organization permanent/temporary,
   // container permanent/temporary). Data is stored as JSON files on disk.
   //
   // Consumers:
   //   1. WASM modules — scoped to their own four storage units
   //   2. Inspector — omniscient, browsable, request/release pattern
   //
   // Caching: UNITs are loaded on demand and evicted when no longer referenced.
   // Crash durability: JSONL changelog appended on every mutation.
   // ---------------------------------------------------------------------------

   class STORAGE
   {
   public:

      // -----------------------------------------------------------------------
      // SCOPE — selects one of the four storage units within a SILO.
      // -----------------------------------------------------------------------

      enum SCOPE
      {
         ORG_PERMANENT       = 0,
         ORG_TEMPORARY       = 1,
         CONTAINER_PERMANENT = 2,
         CONTAINER_TEMPORARY = 3,
         SCOPE_COUNT         = 4,
      };

      // -----------------------------------------------------------------------
      // Forward declarations
      // -----------------------------------------------------------------------

      class UNIT;
      class SILO;

      // -----------------------------------------------------------------------
      // IENUM — enumeration callback interface.
      //
      // Implement to receive SILO pointers during Enumerate().
      // -----------------------------------------------------------------------

      class IENUM
      {
      public:
         virtual ~IENUM () {}
         virtual void OnSilo (SILO* pSilo) = 0;
      };

      // -----------------------------------------------------------------------
      // UNIT — one per JSON file on disk. The core data wrapper.
      //
      // Caches an nlohmann::json document in memory, manages a .meta sidecar
      // file for inspector metadata, and provides the JSONL changelog for crash
      // durability. Also serves as the interface for flat file sandbox ops.
      // -----------------------------------------------------------------------

      class UNIT
      {
      public:
         UNIT (STORAGE* pStorage, SCOPE eScope, const std::string& sJsonPath);

         // --- State ---

         bool     IsLoaded () const;
         bool     IsDirty () const;
         SCOPE    GetScope () const;

         // --- JSON access ---

         nlohmann::json  Get (const std::string& sPath) const;
         void            Set (const std::string& sPath, const nlohmann::json& jValue);
         void            Remove (const std::string& sPath);
         bool            Has (const std::string& sPath) const;

         // --- Bulk ---

         std::string     Json () const;
         void            Json (const std::string& sJson);

         // --- Lifecycle ---

         void            Load ();
         void            Save ();
         void            Evict ();

         // --- Meta sidecar ---

         const std::string&  JsonPath       () const;
         uint64_t            SizeBytes      () const;
         const std::string&  CreatedTime    () const;
         const std::string&  LastAccessTime () const;
         uint32_t            AccessCount    () const;

         void  TouchAccess ();
         void  SaveMeta (std::shared_ptr<VIEWPORT::CONTAINER::NAME> pName);
         void  LoadMeta ();

      private:
         void  NavigatePath (const std::string& sPath, nlohmann::json*& pParent, std::string& sFinalKey) const;
         void  Log_Append (const std::string& sOp, const std::string& sPath, const nlohmann::json& jValue);
         void  Log_Replay ();
         void  Log_Delete ();

         static std::string NowIso8601 ();

         STORAGE*             m_pStorage;
         SCOPE                m_eScope;
         std::string          m_sJsonPath;

         nlohmann::json       m_jData;
         bool                 m_bLoaded;
         bool                 m_bDirty;
         uint32_t             m_nCount_Open;
         uint32_t             m_nCount_Load;

         // Meta sidecar fields
         uint64_t             m_nSizeBytes;
         std::string          m_sCreatedAt;
         std::string          m_sLastAccessedAt;
         uint32_t             m_nAccessCount;

         mutable std::recursive_mutex  m_mutex;

         friend class SILO;
         friend class STORAGE;
      };

      // -----------------------------------------------------------------------
      // SILO — groups four UNITs for a specific container.
      //
      // The handle passed to both WASM host functions and the inspector.
      // Created when a WASM container is instantiated or when the inspector
      // enumerates from disk. Destroyed/evicted when last reference releases.
      // -----------------------------------------------------------------------

      class SILO
      {
      public:
         SILO (STORAGE* pStorage, std::shared_ptr<VIEWPORT::CONTAINER::NAME> pName, VIEWPORT* pViewport);

         // --- Identity ---

         std::shared_ptr<VIEWPORT::CONTAINER::NAME>  Name () const;
         std::string  DisplayName () const;
         VIEWPORT* Viewport () const;
         const std::string& sPath_Permanent () const;
         const std::string& sPath_Temporary () const;

         // --- Path-based API ---

         nlohmann::json  Get    (SCOPE eScope, const std::string& sPath) const;
         void            Set    (SCOPE eScope, const std::string& sPath, const nlohmann::json& jValue);
         void            Remove (SCOPE eScope, const std::string& sPath);
         bool            Has    (SCOPE eScope, const std::string& sPath) const;

         // --- Bulk ---

         std::string     Json (SCOPE eScope) const;
         void            Json (SCOPE eScope, const std::string& sJson);

         // --- Reference counting (WASM + inspector attachments) ---

         void     Attach ();
         void     Detach ();
         uint32_t Count_Load () const;

         // --- Clear flag (deferred history removal) ---

         bool     IsPendingClear () const;
         void     SetPendingClear (bool b);

         // --- Internal ---

         UNIT*    Unit (SCOPE eScope) const;
         void     Unit (SCOPE eScope, UNIT* pUnit);

      private:
         STORAGE*    m_pStorage;
         std::shared_ptr<VIEWPORT::CONTAINER::NAME>  m_pName;
         VIEWPORT* m_pViewport;
         std::string m_sPath_Permanent;
         std::string m_sPath_Temporary;
         UNIT*       m_apUnits[SCOPE_COUNT];
         uint32_t    m_nCount_Load;
         bool        m_bPendingClear;
      };

      // -----------------------------------------------------------------------
      // STORAGE public API
      // -----------------------------------------------------------------------

      explicit STORAGE (ENGINE* pEngine);
      ~STORAGE ();

      bool Initialize ();

      // --- Container lifecycle ---

      SILO*   Silo_Open (std::shared_ptr<VIEWPORT::CONTAINER::NAME> pName, VIEWPORT* pViewport);
      void    Silo_Close (SILO* pSilo);

      // --- Inspector ---

      void    Enumerate (IENUM* pEnum, VIEWPORT* pViewport);

      // --- Paths ---

      const std::string& sPath_Permanent () const;
      const std::string& sPath_Temporary () const;

   private:
      ENGINE*   m_pEngine;
      std::string     m_sPath_Permanent;
      std::string     m_sPath_Temporary;

      std::unordered_map<std::string, UNIT*>  m_umpUnit;
      std::vector<SILO*>                       m_apSilo;
      std::recursive_mutex                    m_mxStorage;
   };
}
#endif // SNEEZE_STORAGE_STORAGE_H
