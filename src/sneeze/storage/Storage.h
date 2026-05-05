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

#include "Sneeze.h"
#include "container/Container.h"
#include <nlohmann/json.hpp>
#include <string>
#include <vector>
#include <unordered_map>
#include <mutex>
#include <memory>
#include <cstdint>
#include <fstream>
#include <filesystem>

// ---------------------------------------------------------------------------
// SNEEZE::STORAGE — persistent per-persona/per-org/per-container JSON document store.
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

class SNEEZE::STORAGE
{
public:

   // -----------------------------------------------------------------------
   // SCOPE — selects one of the four storage units within an ASSET.
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
   class ASSET;

   // -----------------------------------------------------------------------
   // IENUM — enumeration callback interface.
   //
   // Implement to receive ASSET pointers during Enumerate().
   // -----------------------------------------------------------------------

   class IENUM
   {
   public:
      virtual ~IENUM () {}
      virtual void OnAsset (ASSET* pAsset) = 0;
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

      bool     IsLoaded () const          { return m_bLoaded; }
      bool     IsDirty () const           { return m_bDirty; }
      SCOPE    GetScope () const          { return m_eScope; }

      // --- JSON access ---

      nlohmann::json  Get (const std::string& sPath) const;
      void            Set (const std::string& sPath, const nlohmann::json& jValue);
      void            Remove (const std::string& sPath);
      bool            Has (const std::string& sPath) const;

      // --- Bulk ---

      std::string     GetJson () const;
      void            SetJson (const std::string& sJson);

      // --- Lifecycle ---

      void            Load ();
      void            Save ();
      void            Evict ();

      // --- Meta sidecar ---

      const std::string&  GetJsonPath () const        { return m_sJsonPath; }
      uint64_t            GetSizeBytes () const       { return m_nSizeBytes; }
      const std::string&  GetCreatedTime () const     { return m_sCreatedAt; }
      const std::string&  GetLastAccessTime () const  { return m_sLastAccessedAt; }
      uint32_t            GetAccessCount () const     { return m_nAccessCount; }

      void  TouchAccess ();
      void  SaveMeta (std::shared_ptr<SNEEZE::VIEWPORT::CONTAINER::NAME> pName);
      void  LoadMeta ();

   private:
      void  NavigatePath (const std::string& sPath, nlohmann::json*& pParent, std::string& sFinalKey) const;
      void  AppendLog (const std::string& sOp, const std::string& sPath, const nlohmann::json& jValue);
      void  ReplayLog ();
      void  DeleteLog ();

      static std::string NowIso8601 ();

      STORAGE*             m_pStorage;
      SCOPE                m_eScope;
      std::string          m_sJsonPath;

      nlohmann::json       m_jData;
      bool                 m_bLoaded;
      bool                 m_bDirty;
      uint32_t             m_nRefCount;

      // Meta sidecar fields
      uint64_t             m_nSizeBytes;
      std::string          m_sCreatedAt;
      std::string          m_sLastAccessedAt;
      uint32_t             m_nAccessCount;

      mutable std::recursive_mutex  m_mutex;

      friend class ASSET;
      friend class STORAGE;
   };

   // -----------------------------------------------------------------------
   // ASSET — groups four UNITs for a specific container.
   //
   // The handle passed to both WASM host functions and the inspector.
   // Created when a WASM container is instantiated or when the inspector
   // enumerates from disk. Destroyed/evicted when last reference releases.
   // -----------------------------------------------------------------------

   class ASSET : public SNEEZE::NOTIFICATION
   {
   public:
      ASSET (STORAGE* pStorage, std::shared_ptr<SNEEZE::VIEWPORT::CONTAINER::NAME> pName);

      // --- Identity ---

      std::shared_ptr<SNEEZE::VIEWPORT::CONTAINER::NAME>  GetName () const { return m_pName; }
      std::string  GetDisplayName () const { return m_pName ? m_pName->DisplayName () : ""; }

      // --- Path-based API ---

      nlohmann::json  Get (SCOPE eScope, const std::string& sPath) const;
      void            Set (SCOPE eScope, const std::string& sPath, const nlohmann::json& jValue);
      void            Remove (SCOPE eScope, const std::string& sPath);
      bool            Has (SCOPE eScope, const std::string& sPath) const;

      // --- Bulk ---

      std::string     GetJson (SCOPE eScope) const;
      void            SetJson (SCOPE eScope, const std::string& sJson);

      // --- Reference counting (WASM + inspector attachments) ---

      void     Attach ();
      void     Detach ();
      uint32_t GetRefCount () const { return m_nRefCount; }

      // --- Clear flag (deferred history removal) ---

      bool     IsPendingClear () const       { return m_bPendingClear; }
      void     SetPendingClear (bool b)      { m_bPendingClear = b; }

      // --- Internal ---

      UNIT*    GetUnit (SCOPE eScope) const  { return m_apUnits[eScope]; }
      void     SetUnit (SCOPE eScope, UNIT* pUnit) { m_apUnits[eScope] = pUnit; }

   private:
      STORAGE*    m_pStorage;
      std::shared_ptr<SNEEZE::VIEWPORT::CONTAINER::NAME>  m_pName;
      UNIT*       m_apUnits[SCOPE_COUNT];
      uint32_t    m_nRefCount;
      bool        m_bPendingClear;
   };

   // -----------------------------------------------------------------------
   // STORAGE public API
   // -----------------------------------------------------------------------

   explicit STORAGE (SNEEZE* pSneeze);
   ~STORAGE ();

   bool Initialize ();
   void Shutdown ();

   // --- Container lifecycle ---

   ASSET*  Open (std::shared_ptr<SNEEZE::VIEWPORT::CONTAINER::NAME> pName);
   void    Close (ASSET* pAsset);

   // --- Inspector ---

   void    Enumerate (IENUM* pEnum);

   // --- Paths ---

   const std::string& GetPermanentPath () const { return m_sPermanentPath; }
   const std::string& GetTemporaryPath () const { return m_sTemporaryPath; }

private:
   std::string  ComputeUnitPath (const std::string& sBasePath, const std::string& sPersonaHash,
                                 const std::string& sFingerprint, const std::string& sFileName) const;
   UNIT*        FindOrCreateUnit (const std::string& sJsonPath);
   void         SaveAllDirty ();

   SNEEZE*   m_pSneeze;
   std::string     m_sPermanentPath;
   std::string     m_sTemporaryPath;

   std::unordered_map<std::string, std::unique_ptr<UNIT>>  m_mapUnits;
   std::vector<std::unique_ptr<ASSET>>                     m_aAssets;
   std::recursive_mutex                                    m_mutex;
};

#endif // SNEEZE_STORAGE_STORAGE_H
