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

#include <Sneeze.h>
#include "Storage_Asset.h"

using namespace SNEEZE;

// ===========================================================================
// Helpers
// ===========================================================================

class ASSET::Impl
{
public:
   Impl (STORAGE* pStorage, STORAGE::eSCOPE eScope, const std::string& sPathname) :
      m_pStorage (pStorage),
      m_eScope (eScope),
      m_sPathname (sPathname),
      m_bLoaded (false),
      m_bDirty (false),
      m_nCount_Open (0),
      m_nCount_Load (0),
      m_nSizeBytes (0),
      m_nAccessCount (0)
   {
      std::string sMetaPath = m_sPathname + ".meta";

      std::ifstream file (sMetaPath);
      if (file.is_open ())
      {
         try
         {
            nlohmann::json jMeta = nlohmann::json::parse (file);
            m_nSizeBytes      = jMeta.value ("sizeBytes", static_cast<uint64_t> (0));
            m_sCreatedAt      = jMeta.value ("createdAt", "");
            m_sLastAccessedAt = jMeta.value ("lastAccessedAt", "");
            m_nAccessCount    = jMeta.value ("accessCount", static_cast<uint32_t> (0));
         }
         catch (...) {}
      }
   }

public:
   // ---------------------------------------------------------------------------
   // JSON path navigation
   //
   // Supports dot notation and array brackets: "game.poker.table[5].card-color"
   // Sets pParent to the parent object/array and sFinalKey to the last segment.
   // ---------------------------------------------------------------------------

   void NavigatePath (const std::string& sPath, nlohmann::json*& pParent, std::string& sFinalKey) const
   {
      pParent = const_cast<nlohmann::json*> (&m_jData);
      sFinalKey.clear ();

      if (!sPath.empty ())
      {
         std::vector<std::string> aSegments;
         std::string sCurrent;

         for (size_t i = 0; i < sPath.size (); i++)
         {
            char c = sPath[i];
            if (c == '.')
            {
               if (!sCurrent.empty ())
               {
                  aSegments.push_back (sCurrent);
                  sCurrent.clear ();
               }
            }
            else if (c == '[')
            {
               if (!sCurrent.empty ())
               {
                  aSegments.push_back (sCurrent);
                  sCurrent.clear ();
               }
               i++;
               std::string sIndex;
               while (i < sPath.size () && sPath[i] != ']')
                  sIndex += sPath[i++];
               aSegments.push_back ("[" + sIndex + "]");
            }
            else
            {
               sCurrent += c;
            }
         }
         if (!sCurrent.empty ())
            aSegments.push_back (sCurrent);

         if (!aSegments.empty ())
         {
            sFinalKey = aSegments.back ();
            aSegments.pop_back ();

            for (auto& sSeg : aSegments)
            {
               if (sSeg.size () > 2 && sSeg[0] == '[' && sSeg.back () == ']')
               {
                  int nIdx = std::stoi (sSeg.substr (1, sSeg.size () - 2));
                  if (!pParent->is_array ())
                     *pParent = nlohmann::json::array ();
                  while (static_cast<size_t> (nIdx) >= pParent->size ())
                     pParent->push_back (nlohmann::json::object ());
                  pParent = &(*pParent)[nIdx];
               }
               else
               {
                  if (!pParent->is_object ())
                     *pParent = nlohmann::json::object ();
                  if (!pParent->contains (sSeg))
                     (*pParent)[sSeg] = nlohmann::json::object ();
                  pParent = &(*pParent)[sSeg];
               }
            }
         }
      }
   }

   // ---------------------------------------------------------------------------
   // JSONL Changelog — crash durability
   // ---------------------------------------------------------------------------

   void Log_Append (const std::string& sOp, const std::string& sPath, const nlohmann::json& jValue)
   {
      std::string sLogPath = m_sPathname + ".log";

      std::ofstream file (sLogPath, std::ios::app);
      if (file.is_open ())
      {
         nlohmann::json jEntry = nlohmann::json::array ();
         jEntry.push_back (sOp);
         jEntry.push_back (sPath);
         if (!jValue.is_null ())
            jEntry.push_back (jValue);
         file << jEntry.dump () << "\n";
      }
   }

   void Log_Replay ()
   {
      std::string sLogPath = m_sPathname + ".log";

      std::ifstream file (sLogPath);
      if (file.is_open ())
      {
         std::string sLine;
         while (std::getline (file, sLine))
         {
            if (sLine.empty ())
               continue;

            try
            {
               nlohmann::json jEntry = nlohmann::json::parse (sLine);
               if (!jEntry.is_array () || jEntry.size () < 2)
                  continue;

               std::string sOp = jEntry[0].get<std::string> ();
               std::string sPath = jEntry[1].get<std::string> ();

               if (sOp == "Set" && jEntry.size () >= 3)
               {
                  nlohmann::json* pParent = nullptr;
                  std::string sFinalKey;
                  NavigatePath (sPath, pParent, sFinalKey);

                  if (pParent && !sFinalKey.empty ())
                  {
                     if (sFinalKey.size () > 2 && sFinalKey[0] == '[' && sFinalKey.back () == ']')
                     {
                        int nIdx = std::stoi (sFinalKey.substr (1, sFinalKey.size () - 2));
                        if (!pParent->is_array ())
                           *pParent = nlohmann::json::array ();
                        while (static_cast<size_t> (nIdx) >= pParent->size ())
                           pParent->push_back (nlohmann::json ());
                        (*pParent)[nIdx] = jEntry[2];
                     }
                     else
                     {
                        if (!pParent->is_object ())
                           *pParent = nlohmann::json::object ();
                        (*pParent)[sFinalKey] = jEntry[2];
                     }
                  }
               }
               else if (sOp == "Remove")
               {
                  nlohmann::json* pParent = nullptr;
                  std::string sFinalKey;
                  NavigatePath (sPath, pParent, sFinalKey);

                  if (pParent && !sFinalKey.empty ())
                  {
                     if (sFinalKey.size () > 2 && sFinalKey[0] == '[' && sFinalKey.back () == ']')
                     {
                        int nIdx = std::stoi (sFinalKey.substr (1, sFinalKey.size () - 2));
                        if (pParent->is_array () && nIdx >= 0 && static_cast<size_t> (nIdx) < pParent->size ())
                           pParent->erase (nIdx);
                     }
                     else
                     {
                        if (pParent->is_object () && pParent->contains (sFinalKey))
                           pParent->erase (sFinalKey);
                     }
                  }
               }
            }
            catch (...) {}
         }

         m_bDirty = true;
      }
   }

   void Log_Delete ()
   {
      std::string sLogPath = m_sPathname + ".log";
      std::error_code ec;
      std::filesystem::remove (sLogPath, ec);
   }

   void Attach () 
   { 
      if (++m_nCount_Load == 1) 
         Load (); 
   }

   void Detach (const VIEWPORT::CONTAINER::CID& CID)
   {
      if (m_nCount_Load > 0 && --m_nCount_Load == 0)
      {
         SaveMeta (CID);

         if (m_bDirty)
            Save ();

         Evict ();
      }
   }

   void Load ()
   {
      std::lock_guard<std::recursive_mutex> guard (m_mutex);

      if (!m_bLoaded)
      {
         std::error_code ec;
         std::filesystem::create_directories (std::filesystem::path (m_sPathname).parent_path (), ec);

         m_jData = nlohmann::json::object ();

         std::string sJsonFile = m_sPathname + ".json";
         if (std::filesystem::exists (sJsonFile))
         {
            std::ifstream file (sJsonFile);
            if (file.is_open ())
            {
               try
               {
                  m_jData = nlohmann::json::parse (file);
               }
               catch (...)
               {
                  m_jData = nlohmann::json::object ();
               }
            }
         }

         Log_Replay ();

         if (m_sCreatedAt.empty ())
            m_sCreatedAt = NowIso8601 ();

         m_bLoaded = true;

         if (m_bDirty)
            Save ();
      }
   }

   void Save ()
   {
      std::lock_guard<std::recursive_mutex> guard (m_mutex);

      if (m_bLoaded)
      {
         std::string sJsonFile = m_sPathname + ".json";
         std::string sTmpPath = sJsonFile + ".temp";
         std::ofstream file (sTmpPath, std::ios::trunc);
         if (file.is_open ())
         {
            file << m_jData.dump (2);
            file.close ();

            std::error_code ec;
            std::filesystem::rename (sTmpPath, sJsonFile, ec);

            if (!ec)
            {
               auto nSize = std::filesystem::file_size (sJsonFile, ec);
               if (!ec)
                  m_nSizeBytes = static_cast<uint64_t> (nSize);
            }
         }

         Log_Delete ();
         m_bDirty = false;
      }
   }

   void Evict ()
   {
      std::lock_guard<std::recursive_mutex> guard (m_mutex);

      if (m_bDirty)
         Save ();

      m_jData = nlohmann::json ();
      m_bLoaded = false;
   }

   void TouchAccess ()
   {
      m_sLastAccessedAt = NowIso8601 ();
      m_nAccessCount++;
   }

   void SaveMeta (const VIEWPORT::CONTAINER::CID& CID)
   {
      std::string sMetaPath = m_sPathname + ".meta";

      nlohmann::json jMeta;

      jMeta["fingerprint"] = CID.sFingerprint;
      jMeta["organization"] = CID.sOrganization;
      jMeta["commonName"] = CID.sCommonName;
      jMeta["containerName"] = CID.sContainerName;
      jMeta["personaHash"] = CID.sPersonaHash;
      jMeta["validated"] = CID.bValidated;

      jMeta["scope"] = static_cast<int> (m_eScope);
      jMeta["sizeBytes"] = m_nSizeBytes;
      jMeta["createdAt"] = m_sCreatedAt;
      jMeta["lastAccessedAt"] = m_sLastAccessedAt;
      jMeta["accessCount"] = m_nAccessCount;

      std::string sTmpPath = sMetaPath + ".temp";
      std::ofstream file (sTmpPath, std::ios::trunc);
      if (file.is_open ())
      {
         file << jMeta.dump (2);
         file.close ();

         std::error_code ec;
         std::filesystem::rename (sTmpPath, sMetaPath, ec);
      }
   }

   STORAGE*             m_pStorage;
   STORAGE::eSCOPE      m_eScope;
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

   friend class UNIT;
   friend class STORAGE;
};

// ===========================================================================
// ASSET
// ===========================================================================

ASSET::ASSET (STORAGE* pStorage, STORAGE::eSCOPE eScope, const std::string& sPathname) :
   m_pImpl (new Impl (pStorage, eScope, sPathname))
{
}

ASSET::~ASSET ()
{
   delete m_pImpl;
}

// ---------------------------------------------------------------------------
// Accessors
// ---------------------------------------------------------------------------

bool                 ASSET::IsLoaded       () const { return m_pImpl->m_bLoaded; }
bool                 ASSET::IsDirty        () const { return m_pImpl->m_bDirty; }
STORAGE::eSCOPE      ASSET::GetScope       () const { return m_pImpl->m_eScope; }
const std::string&   ASSET::Pathname       () const { return m_pImpl->m_sPathname; }
uint64_t             ASSET::SizeBytes      () const { return m_pImpl->m_nSizeBytes; }
const std::string&   ASSET::CreatedTime    () const { return m_pImpl->m_sCreatedAt; }
const std::string&   ASSET::LastAccessTime () const { return m_pImpl->m_sLastAccessedAt; }
uint32_t             ASSET::AccessCount    () const { return m_pImpl->m_nAccessCount; }

// ---------------------------------------------------------------------------
// JSON access
// ---------------------------------------------------------------------------

nlohmann::json ASSET::Get (const std::string& sPath) const
{
   std::lock_guard<std::recursive_mutex> guard (m_pImpl->m_mutex);

   nlohmann::json* pParent = nullptr;
   std::string sFinalKey;
   m_pImpl->NavigatePath (sPath, pParent, sFinalKey);

   nlohmann::json jResult;
   if (pParent  &&  !sFinalKey.empty ())
   {
      if (sFinalKey.size () > 2  &&  sFinalKey[0] == '['  &&  sFinalKey.back () == ']')
      {
         int nIdx = std::stoi (sFinalKey.substr (1, sFinalKey.size () - 2));
         if (pParent->is_array ()  &&  nIdx >= 0  &&  static_cast<size_t> (nIdx) < pParent->size ())
            jResult = (*pParent)[nIdx];
      }
      else if (pParent->is_object ()  &&  pParent->contains (sFinalKey))
         jResult = (*pParent)[sFinalKey];
   }

   return jResult;
}

void ASSET::Set (const std::string& sPath, const nlohmann::json& jValue)
{
   std::lock_guard<std::recursive_mutex> guard (m_pImpl->m_mutex);

   nlohmann::json* pParent = nullptr;
   std::string sFinalKey;
   m_pImpl->NavigatePath (sPath, pParent, sFinalKey);

   if (pParent  &&  !sFinalKey.empty ())
   {
      if (sFinalKey.size () > 2  &&  sFinalKey[0] == '['  &&  sFinalKey.back () == ']')
      {
         int nIdx = std::stoi (sFinalKey.substr (1, sFinalKey.size () - 2));
         if (!pParent->is_array ())
            *pParent = nlohmann::json::array ();
         while (static_cast<size_t> (nIdx) >= pParent->size ())
            pParent->push_back (nlohmann::json ());
         (*pParent)[nIdx] = jValue;
      }
      else
      {
         if (!pParent->is_object ())
            *pParent = nlohmann::json::object ();
         (*pParent)[sFinalKey] = jValue;
      }

      m_pImpl->m_bDirty = true;
      m_pImpl->TouchAccess ();
      m_pImpl->Log_Append ("Set", sPath, jValue);
   }
}

void ASSET::Remove (const std::string& sPath)
{
   std::lock_guard<std::recursive_mutex> guard (m_pImpl->m_mutex);

   nlohmann::json* pParent = nullptr;
   std::string sFinalKey;
   m_pImpl->NavigatePath (sPath, pParent, sFinalKey);

   if (pParent  &&  !sFinalKey.empty ())
   {
      if (sFinalKey.size () > 2  &&  sFinalKey[0] == '['  &&  sFinalKey.back () == ']')
      {
         int nIdx = std::stoi (sFinalKey.substr (1, sFinalKey.size () - 2));
         if (pParent->is_array ()  &&  nIdx >= 0  &&  static_cast<size_t> (nIdx) < pParent->size ())
            pParent->erase (nIdx);
      }
      else
      {
         if (pParent->is_object ()  &&  pParent->contains (sFinalKey))
            pParent->erase (sFinalKey);
      }

      m_pImpl->m_bDirty = true;
      m_pImpl->TouchAccess ();
      m_pImpl->Log_Append ("Remove", sPath, nlohmann::json ());
   }
}

bool ASSET::Has (const std::string& sPath) const
{
   std::lock_guard<std::recursive_mutex> guard (m_pImpl->m_mutex);

   nlohmann::json* pParent = nullptr;
   std::string sFinalKey;
   m_pImpl->NavigatePath (sPath, pParent, sFinalKey);

   bool bHas = false;
   if (pParent  &&  !sFinalKey.empty ())
   {
      if (sFinalKey.size () > 2  &&  sFinalKey[0] == '['  &&  sFinalKey.back () == ']')
      {
         int nIdx = std::stoi (sFinalKey.substr (1, sFinalKey.size () - 2));
         bHas = pParent->is_array ()  &&  nIdx >= 0  &&  static_cast<size_t> (nIdx) < pParent->size ();
      }
      else
         bHas = pParent->is_object ()  &&  pParent->contains (sFinalKey);
   }

   return bHas;
}

std::string ASSET::Json () const
{
   std::lock_guard<std::recursive_mutex> guard (m_pImpl->m_mutex);

   return m_pImpl->m_jData.dump (2);
}

void ASSET::Json (const std::string& sJson)
{
   std::lock_guard<std::recursive_mutex> guard (m_pImpl->m_mutex);

   try
   {
      m_pImpl->m_jData = nlohmann::json::parse (sJson);
   }
   catch (...)
   {
      m_pImpl->m_jData = nlohmann::json::object ();
   }

   m_pImpl->m_bDirty = true;
   m_pImpl->TouchAccess ();
}

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

uint32_t ASSET::Open ()  { return ++m_pImpl->m_nCount_Open; }
uint32_t ASSET::Close () { return --m_pImpl->m_nCount_Open; }

void ASSET::Attach ()    { m_pImpl->Attach (); }
void ASSET::Detach (const VIEWPORT::CONTAINER::CID& CID) { m_pImpl->Detach (CID); }
void ASSET::Load ()      { m_pImpl->Load ();   }
void ASSET::Save ()      { m_pImpl->Save ();   }
void ASSET::Evict ()     { m_pImpl->Evict ();  }

// ---------------------------------------------------------------------------
// Meta sidecar
// ---------------------------------------------------------------------------

void ASSET::TouchAccess ()                                   { m_pImpl->TouchAccess (); }
void ASSET::SaveMeta (const VIEWPORT::CONTAINER::CID& CID)   { m_pImpl->SaveMeta (CID); }
