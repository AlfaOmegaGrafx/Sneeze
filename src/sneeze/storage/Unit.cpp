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
#include <sstream>
#include <chrono>
#include <iomanip>

using namespace SNEEZE;

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

STORAGE::UNIT::UNIT (STORAGE* pStorage, eSCOPE eScope, const std::string& sPathname) :
   m_pStorage       (pStorage),
   m_eScope         (eScope),
   m_sPathname      (sPathname),
   m_bLoaded        (false),
   m_bDirty         (false),
   m_nCount_Open    (0),
   m_nCount_Load    (0),
   m_nSizeBytes     (0),
   m_nAccessCount   (0)
{
   LoadMeta ();
}

// ---------------------------------------------------------------------------
// Accessors
// ---------------------------------------------------------------------------

bool                 STORAGE::UNIT::IsLoaded       () const { return m_bLoaded; }
bool                 STORAGE::UNIT::IsDirty        () const { return m_bDirty; }
STORAGE::eSCOPE      STORAGE::UNIT::GetScope       () const { return m_eScope; }
const std::string&   STORAGE::UNIT::Pathname       () const { return m_sPathname; }
uint64_t             STORAGE::UNIT::SizeBytes      () const { return m_nSizeBytes; }
const std::string&   STORAGE::UNIT::CreatedTime    () const { return m_sCreatedAt; }
const std::string&   STORAGE::UNIT::LastAccessTime () const { return m_sLastAccessedAt; }
uint32_t             STORAGE::UNIT::AccessCount    () const { return m_nAccessCount; }

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

      if (!aSegments.empty ())
      {
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

void STORAGE::UNIT::Set (const std::string& sPath, const nlohmann::json& jValue)
{
   std::lock_guard<std::recursive_mutex> guard (m_mutex);

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
         (*pParent)[nIdx] = jValue;
      }
      else
      {
         if (!pParent->is_object ())
            *pParent = nlohmann::json::object ();
         (*pParent)[sFinalKey] = jValue;
      }

      m_bDirty = true;
      Log_Append ("Set", sPath, jValue);
   }
}

void STORAGE::UNIT::Remove (const std::string& sPath)
{
   std::lock_guard<std::recursive_mutex> guard (m_mutex);

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

      m_bDirty = true;
      Log_Append ("Remove", sPath, nlohmann::json ());
   }
}

bool STORAGE::UNIT::Has (const std::string& sPath) const
{
   std::lock_guard<std::recursive_mutex> guard (m_mutex);

   nlohmann::json* pParent = nullptr;
   std::string sFinalKey;
   NavigatePath (sPath, pParent, sFinalKey);

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

std::string STORAGE::UNIT::Json () const
{
   std::lock_guard<std::recursive_mutex> guard (m_mutex);
   return m_jData.dump (2);
}

void STORAGE::UNIT::Json (const std::string& sJson)
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

void STORAGE::UNIT::Log_Append (const std::string& sOp, const std::string& sPath, const nlohmann::json& jValue)
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

void STORAGE::UNIT::Log_Replay ()
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
}

void STORAGE::UNIT::Log_Delete ()
{
   std::string sLogPath = m_sPathname + ".log";
   std::error_code ec;
   std::filesystem::remove (sLogPath, ec);
}

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

uint32_t STORAGE::UNIT::Open ()  { return ++m_nCount_Open; }
uint32_t STORAGE::UNIT::Close () { return --m_nCount_Open; }

void STORAGE::UNIT::Attach () { if (++m_nCount_Load == 1) Load (); }
void STORAGE::UNIT::Detach () { if (m_nCount_Load > 0  &&  --m_nCount_Load == 0) { if (m_bDirty) Save (); Evict (); } }

void STORAGE::UNIT::Load ()
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

void STORAGE::UNIT::Save ()
{
   std::lock_guard<std::recursive_mutex> guard (m_mutex);

   if (m_bLoaded)
   {
      std::string sJsonFile = m_sPathname + ".json";
      std::string sTmpPath  = sJsonFile + ".temp";
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

void STORAGE::UNIT::SaveMeta (const VIEWPORT::CONTAINER::CID& CID)
{
   std::string sMetaPath = m_sPathname + ".meta";

   nlohmann::json jMeta;

   jMeta["fingerprint"]   = CID.sFingerprint;
   jMeta["organization"]  = CID.sOrganization;
   jMeta["commonName"]    = CID.sCommonName;
   jMeta["containerName"] = CID.sContainerName;
   jMeta["personaHash"]   = CID.sPersonaHash;
   jMeta["validated"]     = CID.bValidated;

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

