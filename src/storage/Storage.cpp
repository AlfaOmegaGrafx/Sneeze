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
#include <nlohmann/json.hpp>
#include <fstream>
#include <filesystem>
#include <cstdio>

#ifdef _WIN32
#include <windows.h>
#include <shlobj.h>
#endif

namespace sneeze { namespace storage {

// ===========================================================================
// CONTAINER
// ===========================================================================

CONTAINER::CONTAINER (const std::string& sName, const std::string& sDiskPath)
   : m_sName (sName)
   , m_sDiskPath (sDiskPath)
{
}

std::string CONTAINER::Get (const std::string& sKey) const
{
   std::lock_guard<std::mutex> guard (m_mutex);
   auto it = m_mapData.find (sKey);
   if (it != m_mapData.end ())
      return it->second;
   return "";
}

void CONTAINER::Set (const std::string& sKey, const std::string& sValue)
{
   std::lock_guard<std::mutex> guard (m_mutex);
   m_mapData[sKey] = sValue;
   Save ();
}

void CONTAINER::Remove (const std::string& sKey)
{
   std::lock_guard<std::mutex> guard (m_mutex);
   m_mapData.erase (sKey);
   Save ();
}

bool CONTAINER::Has (const std::string& sKey) const
{
   std::lock_guard<std::mutex> guard (m_mutex);
   return m_mapData.find (sKey) != m_mapData.end ();
}

void CONTAINER::Load ()
{
   std::lock_guard<std::mutex> guard (m_mutex);
   std::ifstream file (m_sDiskPath);
   if (!file.is_open ())
      return;

   try
   {
      nlohmann::json jDoc = nlohmann::json::parse (file);
      for (auto it = jDoc.begin (); it != jDoc.end (); ++it)
      {
         if (it.value ().is_string ())
            m_mapData[it.key ()] = it.value ().get<std::string> ();
         else
            m_mapData[it.key ()] = it.value ().dump ();
      }
   }
   catch (...) {}
}

void CONTAINER::Save () const
{
   std::filesystem::path dir = std::filesystem::path (m_sDiskPath).parent_path ();
   std::filesystem::create_directories (dir);

   nlohmann::json jDoc = nlohmann::json::object ();
   for (auto& pair : m_mapData)
      jDoc[pair.first] = pair.second;

   std::ofstream file (m_sDiskPath, std::ios::trunc);
   if (file.is_open ())
      file << jDoc.dump (2);
}

// ===========================================================================
// FINGERPRINT
// ===========================================================================

FINGERPRINT::FINGERPRINT (const std::string& sFingerprint, const std::string& sBasePath)
   : m_sFingerprint (sFingerprint)
   , m_sBasePath (sBasePath)
{
}

std::string FINGERPRINT::Common_Get (const std::string& sKey) const
{
   std::lock_guard<std::mutex> guard (m_commonMutex);
   auto it = m_mapCommon.find (sKey);
   if (it != m_mapCommon.end ())
      return it->second;
   return "";
}

void FINGERPRINT::Common_Set (const std::string& sKey, const std::string& sValue)
{
   std::lock_guard<std::mutex> guard (m_commonMutex);
   m_mapCommon[sKey] = sValue;
   Save ();
}

void FINGERPRINT::Common_Remove (const std::string& sKey)
{
   std::lock_guard<std::mutex> guard (m_commonMutex);
   m_mapCommon.erase (sKey);
   Save ();
}

bool FINGERPRINT::Common_Has (const std::string& sKey) const
{
   std::lock_guard<std::mutex> guard (m_commonMutex);
   return m_mapCommon.find (sKey) != m_mapCommon.end ();
}

CONTAINER* FINGERPRINT::GetContainer (const std::string& sName)
{
   std::lock_guard<std::mutex> guard (m_containersMutex);

   auto it = m_mapContainers.find (sName);
   if (it != m_mapContainers.end ())
      return it->second.get ();

   std::string sDiskPath = m_sBasePath + "/container-" + sName + ".json";
   auto pContainer = std::make_unique<CONTAINER> (sName, sDiskPath);
   pContainer->Load ();
   CONTAINER* pRaw = pContainer.get ();
   m_mapContainers[sName] = std::move (pContainer);
   return pRaw;
}

void FINGERPRINT::Load ()
{
   std::lock_guard<std::mutex> guard (m_commonMutex);
   std::string sCommonPath = m_sBasePath + "/common.json";
   std::ifstream file (sCommonPath);
   if (!file.is_open ())
      return;

   try
   {
      nlohmann::json jDoc = nlohmann::json::parse (file);
      for (auto it = jDoc.begin (); it != jDoc.end (); ++it)
      {
         if (it.value ().is_string ())
            m_mapCommon[it.key ()] = it.value ().get<std::string> ();
         else
            m_mapCommon[it.key ()] = it.value ().dump ();
      }
   }
   catch (...) {}
}

void FINGERPRINT::Save () const
{
   std::filesystem::create_directories (m_sBasePath);
   std::string sCommonPath = m_sBasePath + "/common.json";

   nlohmann::json jDoc = nlohmann::json::object ();
   for (auto& pair : m_mapCommon)
      jDoc[pair.first] = pair.second;

   std::ofstream file (sCommonPath, std::ios::trunc);
   if (file.is_open ())
      file << jDoc.dump (2);
}

// ===========================================================================
// PERSONA_STORE
// ===========================================================================

PERSONA_STORE::PERSONA_STORE (const std::string& sPersonaHash, const std::string& sBasePath)
   : m_sPersonaHash (sPersonaHash)
   , m_sBasePath (sBasePath)
{
}

FINGERPRINT* PERSONA_STORE::GetFingerprint (const std::string& sFingerprint)
{
   std::lock_guard<std::mutex> guard (m_mutex);

   auto it = m_mapFingerprints.find (sFingerprint);
   if (it != m_mapFingerprints.end ())
      return it->second.get ();

   std::string sFpPath = m_sBasePath + "/" + sFingerprint;
   auto pFingerprint = std::make_unique<FINGERPRINT> (sFingerprint, sFpPath);
   pFingerprint->Load ();
   FINGERPRINT* pRaw = pFingerprint.get ();
   m_mapFingerprints[sFingerprint] = std::move (pFingerprint);
   return pRaw;
}

// ===========================================================================
// STORAGE_SYSTEM
// ===========================================================================

STORAGE_SYSTEM::STORAGE_SYSTEM ()
{
}

STORAGE_SYSTEM::~STORAGE_SYSTEM ()
{
   Shutdown ();
}

bool STORAGE_SYSTEM::Initialize ()
{
   m_sRootPath = GetStorageRootPath ();
   if (m_sRootPath.empty ())
   {
      std::fprintf (stderr, "STORAGE: Failed to determine storage root path\n");
      return false;
   }

   std::filesystem::create_directories (m_sRootPath);
   std::fprintf (stdout, "STORAGE: Initialized (root: %s)\n", m_sRootPath.c_str ());
   return true;
}

void STORAGE_SYSTEM::Shutdown ()
{
   std::lock_guard<std::mutex> guard (m_mutex);
   m_mapPersonas.clear ();
}

PERSONA_STORE* STORAGE_SYSTEM::GetPersona (const std::string& sPersonaHash)
{
   std::lock_guard<std::mutex> guard (m_mutex);

   auto it = m_mapPersonas.find (sPersonaHash);
   if (it != m_mapPersonas.end ())
      return it->second.get ();

   std::string sPersonaPath = m_sRootPath + "/" + sPersonaHash;
   auto pPersona = std::make_unique<PERSONA_STORE> (sPersonaHash, sPersonaPath);
   PERSONA_STORE* pRaw = pPersona.get ();
   m_mapPersonas[sPersonaHash] = std::move (pPersona);
   return pRaw;
}

std::string STORAGE_SYSTEM::GetStorageRootPath () const
{
#ifdef _WIN32
   char szPath[MAX_PATH] = {};
   if (SUCCEEDED (SHGetFolderPathA (nullptr, CSIDL_APPDATA, nullptr, 0, szPath)))
   {
      std::string sPath (szPath);
      sPath += "\\Sneeze\\Storage";
      return sPath;
   }
   return "";
#else
   const char* pHome = std::getenv ("HOME");
   if (pHome)
   {
      std::string sPath (pHome);
      sPath += "/.sneeze/storage";
      return sPath;
   }
   return "";
#endif
}

}} // namespace sneeze::storage
