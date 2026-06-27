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

#ifndef SNEEZE_STORAGE_ISTORAGEIMPL_H
#define SNEEZE_STORAGE_ISTORAGEIMPL_H

namespace SNEEZE
{
   class ISTORAGE_IMPL
   {
   public:
      ISTORAGE_IMPL ();
      virtual ~ISTORAGE_IMPL ();

      virtual UNIT*              Unit_Open  (eSILO_SCOPE eScope, const std::string& sPathname)                            = 0;
      virtual void               Unit_Close (UNIT* pUnit)                                                                 = 0;

      virtual void               Log (IENGINE::eLOGLEVEL Level, const std::string& sModule, const std::string& sMessage)  = 0;

      virtual ICONTEXT*          Host () const                                                                            = 0;

   private:
   };

   // -----------------------------------------------------------------------
   // UNIT -- one per JSON file on disk. The core data wrapper.
   //
   // Caches an nlohmann::json document in memory, manages a .meta sidecar
   // file for inspector metadata, and provides the JSONL changelog for crash
   // durability. Also serves as the interface for flat file sandbox ops.
   // -----------------------------------------------------------------------

   class UNIT
   {
   public:
      UNIT (ISTORAGE_IMPL* pIStorage_Impl, eSILO_SCOPE eScope, const std::string& sPathname);
      virtual ~UNIT ();

      // --- State ---

      bool                IsLoaded () const;
      bool                IsDirty  () const;
      eSILO_SCOPE         GetScope () const;

      // --- JSON access ---

      nlohmann::json      Get    (const std::string& sPath) const;
      void                Set    (const std::string& sPath, const nlohmann::json& jValue);
      void                Remove (const std::string& sPath);
      bool                Has    (const std::string& sPath) const;

      // --- Bulk ---

      std::string         Json () const;
      void                Json (const std::string& sJson);

      // --- Lifecycle ---

      uint32_t            Open   ();
      uint32_t            Close  ();
      void                Attach ();
      void                Detach (CONTAINER* pContainer);
      void                Load   ();
      void                Save   ();
      void                Evict  ();

      // --- Meta sidecar ---

      const std::string& Pathname       () const;
      uint64_t           SizeBytes      () const;
      const std::string& CreatedTime    () const;
      const std::string& LastAccessTime () const;
      uint32_t           AccessCount    () const;

      void  TouchAccess ();
      void  Meta_Save (CONTAINER* pContainer);

   private:
      class Impl;
      Impl* m_pImpl;
   };
}
#endif // SNEEZE_STORAGE_ISTORAGEIMPL_H
