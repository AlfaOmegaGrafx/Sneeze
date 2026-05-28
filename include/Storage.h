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
   //   1. WASM modules — scoped to their own four storage assets
   //   2. Inspector — omniscient, browsable, request/release pattern
   //
   // Caching: ASSETs are loaded on demand and evicted when no longer referenced.
   // Crash durability: JSONL changelog appended on every mutation.
   // ---------------------------------------------------------------------------

   class SASSET;
   class ISTORAGE_IMPL;

   class STORAGE
   {
   public:

      // -----------------------------------------------------------------------
      // SCOPE — selects one of the four storage assets within a SILO.
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
      // SILO — groups four ASSETs for a specific container.
      //
      // The handle passed to both WASM host functions and the inspector.
      // Created when a WASM container is instantiated or when the inspector
      // enumerates from disk. Destroyed/evicted when last reference releases.
      // -----------------------------------------------------------------------

      class SILO
      {
      public:
         SILO (ISTORAGE_IMPL* pIStorage_Impl, const CONTEXT::CONTAINER::CID* pCID);
        ~SILO ();

         void Initialize ();

         // --- Identity ---

         std::string  DisplayName () const;

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
         // --- Paths ---

         std::string Path     (eSCOPE eScope) const;
         std::string Filename (eSCOPE eScope, const std::string& sExt = "") const;
         std::string Pathname (eSCOPE eScope, const std::string& sExt = "") const;

      private:
         class Impl;
         Impl* m_pImpl;
      };

      // -----------------------------------------------------------------------
      // IENUM_SILO — enumeration callback interface.
      //
      // Implement to receive SILO pointers during Silo_Enum().
      // -----------------------------------------------------------------------

      class IENUM_SILO
      {
      public:
         virtual ~IENUM_SILO () {}
         virtual void OnSilo (SILO* pSilo) = 0;
      };

      // -----------------------------------------------------------------------
      // STORAGE public API
      // -----------------------------------------------------------------------

      explicit STORAGE (CONTEXT* pContext);
      ~STORAGE ();

      bool Initialize ();

      // --- Container lifecycle ---

      SILO*   Silo_Open  (const CONTEXT::CONTAINER::CID* pCID);
      void    Silo_Close (SILO* pSilo);
      void    Silo_Enum  (IENUM_SILO* pEnum);

   private:
      class Impl;
      Impl* m_pImpl;

   };
}
#endif // SNEEZE_STORAGE_STORAGE_H
