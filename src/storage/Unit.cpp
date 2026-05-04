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

#include "Storage.h"
#include "core/Sneeze.h"
#include <sstream>
#include <chrono>
#include <iomanip>


namespace SNEEZE {

// ===========================================================================
// Helpers
// ===========================================================================

std::string STORAGE::UNIT::NowIso8601 ()
{
   auto tpNow = std::chrono::system_clock::now ();
   auto tTime = std::chrono::system_clock::to_time_t (tpNow);
   struct tm tmBuf = {};
#ifdef _WIN32
   gmtime_s (&tmBuf, &tTime);
#else
   gmtime_r (&tTime, &tmBuf);
#endif
   char szBuf[32];
   std::strftime (szBuf, sizeof (szBuf), "%Y-%m-%dT%H:%M:%SZ", &tmBuf);
   return std::string (szBuf);
}

// ===========================================================================
// STORAGE::UNIT
// ===========================================================================

STORAGE::UNIT::UNIT (STORAGE* pStorage, SCOPE eScope, const std::string& sJsonPath) :
   m_pStorage       (pStorage),
   m_eScope         (eScope),
   m_sJsonPath      (sJsonPath),
   m_bLoaded        (false),
   m_bDirty         (false),
   m_nRefCount      (0),
   m_nSizeBytes     (0),
   m_nAccessCount   (0)
{
}

// ---------------------------------------------------------------------------
// JSON path navigation
//
// Supports dot notation and array brackets: "game.poker.table[5].card-color"
// Sets pParent to the parent object/array and sFinalKey to the last segment.
// ---------------------------------------------------------------------------

void STORAGE::UNIT::NavigatePath (const std::string& sPath, nlohmann::json*& pParent, std::string& sFinalKey) const
{
   pParent = const_cast<nlohmann::json*> (&m_jData);
   sFinalKey.clear ();

   if (sPath.empty ())
      return;

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
         while (i < sPath.size ()  &&  sPath[i] != ']')
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

   if (aSegments.empty ())
      return;

   sFinalKey = aSegments.back ();
   aSegments.pop_back ();

   for (auto& sSeg : aSegments)
   {
      if (sSeg.size () > 2  &&  sSeg[0] == '['  &&  sSeg.back () == ']')
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

// ---------------------------------------------------------------------------
// JSON access
// ---------------------------------------------------------------------------

nlohmann::json STORAGE::UNIT::Get (const std::string& sPath) const
{
   std::lock_guard<std::recursive_mutex> guard (m_mutex);

   nlohmann::json* pParent = nullptr;
   std::string sFinalKey;
   NavigatePath (sPath, pParent, sFinalKey);

   if (!pParent  ||  sFinalKey.empty ())
      return nlohmann::json ();

   if (sFinalKey.size () > 2  &&  sFinalKey[0] == '['  &&  sFinalKey.back () == ']')
   {
      int nIdx = std::stoi (sFinalKey.substr (1, sFinalKey.size () - 2));
      if (pParent->is_array ()  &&  nIdx >= 0  &&  static_cast<size_t> (nIdx) < pParent->size ())
         return (*pParent)[nIdx];
      return nlohmann::json ();
   }

   if (pParent->is_object ()  &&  pParent->contains (sFinalKey))
      return (*pParent)[sFinalKey];

   return nlohmann::json ();
}

void STORAGE::UNIT::Set (const std::string& sPath, const nlohmann::json& jValue)
{
   std::lock_guard<std::recursive_mutex> guard (m_mutex);

   nlohmann::json* pParent = nullptr;
   std::string sFinalKey;
   NavigatePath (sPath, pParent, sFinalKey);

   if (!pParent  ||  sFinalKey.empty ())
      return;

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

   m_bDirty = true;
   AppendLog ("Set", sPath, jValue);
}

void STORAGE::UNIT::Remove (const std::string& sPath)
{
   std::lock_guard<std::recursive_mutex> guard (m_mutex);

   nlohmann::json* pParent = nullptr;
   std::string sFinalKey;
   NavigatePath (sPath, pParent, sFinalKey);

   if (!pParent  ||  sFinalKey.empty ())
      return;

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

   m_bDirty = true;
   AppendLog ("Remove", sPath, nlohmann::json ());
}

bool STORAGE::UNIT::Has (const std::string& sPath) const
{
   std::lock_guard<std::recursive_mutex> guard (m_mutex);

   nlohmann::json* pParent = nullptr;
   std::string sFinalKey;
   NavigatePath (sPath, pParent, sFinalKey);

   if (!pParent  ||  sFinalKey.empty ())
      return false;

   if (sFinalKey.size () > 2  &&  sFinalKey[0] == '['  &&  sFinalKey.back () == ']')
   {
      int nIdx = std::stoi (sFinalKey.substr (1, sFinalKey.size () - 2));
      return pParent->is_array ()  &&  nIdx >= 0  &&  static_cast<size_t> (nIdx) < pParent->size ();
   }

   return pParent->is_object ()  &&  pParent->contains (sFinalKey);
}

std::string STORAGE::UNIT::GetJson () const
{
   std::lock_guard<std::recursive_mutex> guard (m_mutex);
   return m_jData.dump (2);
}

void STORAGE::UNIT::SetJson (const std::string& sJson)
{
   std::lock_guard<std::recursive_mutex> guard (m_mutex);

   try
   {
      m_jData = nlohmann::json::parse (sJson);
   }
   catch (...)
   {
      m_jData = nlohmann::json::object ();
   }

   m_bDirty = true;
}

// ---------------------------------------------------------------------------
// JSONL Changelog — crash durability
// ---------------------------------------------------------------------------

void STORAGE::UNIT::AppendLog (const std::string& sOp, const std::string& sPath, const nlohmann::json& jValue)
{
   std::string sLogPath = m_sJsonPath + ".log";

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

void STORAGE::UNIT::ReplayLog ()
{
   std::string sLogPath = m_sJsonPath + ".log";

   std::ifstream file (sLogPath);
   if (!file.is_open ())
      return;

   std::string sLine;
   while (std::getline (file, sLine))
   {
      if (sLine.empty ())
         continue;

      try
      {
         nlohmann::json jEntry = nlohmann::json::parse (sLine);
         if (!jEntry.is_array ()  ||  jEntry.size () < 2)
            continue;

         std::string sOp   = jEntry[0].get<std::string> ();
         std::string sPath = jEntry[1].get<std::string> ();

         if (sOp == "Set"  &&  jEntry.size () >= 3)
         {
            nlohmann::json* pParent = nullptr;
            std::string sFinalKey;
            NavigatePath (sPath, pParent, sFinalKey);

            if (pParent  &&  !sFinalKey.empty ())
            {
               if (sFinalKey.size () > 2  &&  sFinalKey[0] == '['  &&  sFinalKey.back () == ']')
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
            }
         }
      }
      catch (...) {}
   }

   m_bDirty = true;
}

void STORAGE::UNIT::DeleteLog ()
{
   std::string sLogPath = m_sJsonPath + ".log";
   std::error_code ec;
   std::filesystem::remove (sLogPath, ec);
}

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

void STORAGE::UNIT::Load ()
{
   std::lock_guard<std::recursive_mutex> guard (m_mutex);

   if (m_bLoaded)
      return;

   m_jData = nlohmann::json::object ();

   if (std::filesystem::exists (m_sJsonPath))
   {
      std::ifstream file (m_sJsonPath);
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

   ReplayLog ();

   if (m_sCreatedAt.empty ())
      m_sCreatedAt = NowIso8601 ();

   m_bLoaded = true;

   if (m_bDirty)
      Save ();
}

void STORAGE::UNIT::Save ()
{
   std::lock_guard<std::recursive_mutex> guard (m_mutex);

   if (!m_bLoaded)
      return;

   std::filesystem::create_directories (std::filesystem::path (m_sJsonPath).parent_path ());

   std::string sTmpPath = m_sJsonPath + ".temp";
   std::ofstream file (sTmpPath, std::ios::trunc);
   if (file.is_open ())
   {
      file << m_jData.dump (2);
      file.close ();

      std::error_code ec;
      std::filesystem::rename (sTmpPath, m_sJsonPath, ec);

      if (!ec)
      {
         auto nSize = std::filesystem::file_size (m_sJsonPath, ec);
         if (!ec)
            m_nSizeBytes = static_cast<uint64_t> (nSize);
      }
   }

   DeleteLog ();
   m_bDirty = false;
}

void STORAGE::UNIT::Evict ()
{
   std::lock_guard<std::recursive_mutex> guard (m_mutex);

   if (m_bDirty)
      Save ();

   m_jData = nlohmann::json ();
   m_bLoaded = false;
}

// ---------------------------------------------------------------------------
// Meta sidecar
// ---------------------------------------------------------------------------

void STORAGE::UNIT::TouchAccess ()
{
   m_sLastAccessedAt = NowIso8601 ();
   m_nAccessCount++;
}

void STORAGE::UNIT::SaveMeta (std::shared_ptr<CONTAINER::NAME> pName)
{
   std::string sMetaPath = m_sJsonPath + ".meta";
   std::filesystem::create_directories (std::filesystem::path (sMetaPath).parent_path ());

   nlohmann::json jMeta;

   if (pName)
   {
      jMeta["fingerprint"]   = pName->sFingerprint;
      jMeta["organization"]  = pName->sOrganization;
      jMeta["commonName"]    = pName->sCommonName;
      jMeta["containerName"] = pName->sContainerName;
      jMeta["personaHash"]   = pName->sPersonaHash;
      jMeta["validated"]     = pName->bValidated;
   }

   jMeta["scope"]          = static_cast<int> (m_eScope);
   jMeta["sizeBytes"]      = m_nSizeBytes;
   jMeta["createdAt"]      = m_sCreatedAt;
   jMeta["lastAccessedAt"] = m_sLastAccessedAt;
   jMeta["accessCount"]    = m_nAccessCount;

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

void STORAGE::UNIT::LoadMeta ()
{
   std::string sMetaPath = m_sJsonPath + ".meta";

   std::ifstream file (sMetaPath);
   if (!file.is_open ())
      return;

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

} // namespace SNEEZE
