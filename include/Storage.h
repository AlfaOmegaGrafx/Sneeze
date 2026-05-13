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

      enum eSCOPE
      {
         kSCOPE_PERMANENT_ORG     = 0,
         kSCOPE_PERMANENT_COMPANY = 1,
         kSCOPE_TEMPORARY_ORG     = 2,
         kSCOPE_TEMPORARY_COMPANY = 3,
         kSCOPE_COUNT             = 4,
      };

      // -----------------------------------------------------------------------
      // Forward declarations
      // -----------------------------------------------------------------------

      class UNIT;
      class SILO;

      // -----------------------------------------------------------------------
      // IENUM — enumeration callback interface.
      //
      // Implement to receive SILO pointers during Silo_Enum().
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
         UNIT (STORAGE* pStorage, eSCOPE eScope, const std::string& sPathname);

         // --- State ---

         bool                IsLoaded       () const;
         bool                IsDirty        () const;
         eSCOPE              GetScope       () const;

         // --- JSON access ---

         nlohmann::json      Get            (const std::string& sPath) const;
         void                Set            (const std::string& sPath, const nlohmann::json& jValue);
         void                Remove         (const std::string& sPath);
         bool                Has            (const std::string& sPath) const;

         // --- Bulk ---

         std::string         Json           () const;
         void                Json           (const std::string& sJson);

         // --- Lifecycle ---

         uint32_t            Open           ();
         uint32_t            Close          ();
         void                Attach         ();
         void                Detach         ();
         void                Load           ();
         void                Save           ();
         void                Evict          ();

         // --- Meta sidecar ---

         const std::string&  Pathname       () const;
         uint64_t            SizeBytes      () const;
         const std::string&  CreatedTime    () const;
         const std::string&  LastAccessTime () const;
         uint32_t            AccessCount    () const;

         void  TouchAccess ();
         void  SaveMeta (const VIEWPORT::CONTAINER::CID& CID);
         void  LoadMeta ();

      private:
         void  NavigatePath (const std::string& sPath, nlohmann::json*& pParent, std::string& sFinalKey) const;
         void  Log_Append (const std::string& sOp, const std::string& sPath, const nlohmann::json& jValue);
         void  Log_Replay ();
         void  Log_Delete ();

         static std::string NowIso8601 ();

         STORAGE*             m_pStorage;
         eSCOPE               m_eScope;
         std::string          m_sPathname;

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
         SILO (STORAGE* pStorage, const VIEWPORT::CONTAINER::CID* pCID, VIEWPORT* pViewport);
        ~SILO ();

         void Initialize ();

         // --- Identity ---

         const VIEWPORT::CONTAINER::CID&  CID () const;
         std::string  DisplayName () const;
         VIEWPORT* Viewport () const;
         const std::string& sPath_Permanent () const;
         const std::string& sPath_Temporary () const;

         // --- Path-based API ---

         nlohmann::json  Get    (eSCOPE eScope, const std::string& sPath) const;
         void            Set    (eSCOPE eScope, const std::string& sPath, const nlohmann::json& jValue);
         void            Remove (eSCOPE eScope, const std::string& sPath);
         bool            Has    (eSCOPE eScope, const std::string& sPath) const;

         // --- Bulk ---

         std::string     Json (eSCOPE eScope) const;
         void            Json (eSCOPE eScope, const std::string& sJson);

         // --- Reference counting (WASM + inspector attachments) ---

         void     Attach ();
         void     Detach ();
         uint32_t Count_Load () const;

         // --- Clear flag (deferred history removal) ---

         bool     IsPendingClear () const;
         void     SetPendingClear (bool b);

         // --- Paths ---

         std::string sPath     (eSCOPE eScope) const;
         std::string sFilename (eSCOPE eScope, const std::string& sExt = "") const;
         std::string sPathname (eSCOPE eScope, const std::string& sExt = "") const;

         // --- Internal ---

         UNIT*    Unit (eSCOPE eScope) const;
         void     Unit (eSCOPE eScope, UNIT* pUnit);

      private:
         STORAGE*                                    m_pStorage;
         VIEWPORT*                                   m_pViewport;
         VIEWPORT::CONTAINER::CID                    m_CID;
         std::string                                 m_sPath_Permanent;
         std::string                                 m_sPath_Temporary;
         UNIT*                                       m_apUnit[kSCOPE_COUNT];
         uint32_t                                    m_nCount_Load;
         bool                                        m_bPendingClear;
      };

      // -----------------------------------------------------------------------
      // STORAGE public API
      // -----------------------------------------------------------------------

      explicit STORAGE (ENGINE* pEngine);
      ~STORAGE ();

      bool Initialize ();

      // --- Container lifecycle ---

      SILO*   Silo_Open  (VIEWPORT* pViewport, const VIEWPORT::CONTAINER::CID* pCID);
      void    Silo_Close (VIEWPORT* pViewport, SILO* pSilo);
      void    Silo_Enum  (VIEWPORT* pViewport, IENUM* pEnum);

   private:
      UNIT*   Unit_Open  (eSCOPE eScope, const std::string& sPathname);
      void    Unit_Close (UNIT* pUnit);

      ENGINE*                                 m_pEngine;

      std::recursive_mutex                    m_mxStorage;
      std::vector<SILO*>                      m_apSilo;
      std::unordered_map<std::string, UNIT*>  m_umpUnit;
   };
}
#endif // SNEEZE_STORAGE_STORAGE_H
