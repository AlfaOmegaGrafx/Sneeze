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

#include "FileCache.h"
#include <cstdio>
#include <fstream>
#include <filesystem>

#ifdef _WIN32
#include <windows.h>
#include <shlobj.h>
#endif

namespace sneeze { namespace cache {

FILE_CACHE::FILE_CACHE ()
{
}

FILE_CACHE::~FILE_CACHE ()
{
   Shutdown ();
}

bool FILE_CACHE::Initialize ()
{
   m_sCachePath = GetPersistentCachePath ();
   if (m_sCachePath.empty ())
   {
      std::fprintf (stderr, "FILE_CACHE: Failed to determine persistent cache path\n");
      return false;
   }

   std::filesystem::create_directories (m_sCachePath);
   std::fprintf (stdout, "FILE_CACHE: Initialized (persistent path: %s)\n", m_sCachePath.c_str ());
   return true;
}

void FILE_CACHE::Shutdown ()
{
   ClearSession ();

   std::lock_guard<std::mutex> guard (m_moduleMutex);
   m_mapModule.clear ();
}

// ---------------------------------------------------------------------------
// Session-only tiers
// ---------------------------------------------------------------------------

CACHE_ENTRY* FILE_CACHE::Request_Msf (const std::string& sUrl, CACHE_CALLBACK pfnCallback)
{
   std::lock_guard<std::mutex> guard (m_msfMutex);

   auto it = m_mapMsf.find (sUrl);
   if (it != m_mapMsf.end ())
   {
      it->second->AddCallback (std::move (pfnCallback));
      return it->second.get ();
   }

   auto pEntry = std::make_unique<CACHE_ENTRY> (sUrl, "");
   CACHE_ENTRY* pRaw = pEntry.get ();
   m_mapMsf[sUrl] = std::move (pEntry);
   pRaw->AddCallback (std::move (pfnCallback));
   pRaw->SetFetching ();
   return pRaw;
}

CACHE_ENTRY* FILE_CACHE::Request_Asset (const std::string& sUrl, CACHE_CALLBACK pfnCallback)
{
   std::lock_guard<std::mutex> guard (m_assetMutex);

   auto it = m_mapAsset.find (sUrl);
   if (it != m_mapAsset.end ())
   {
      it->second->AddCallback (std::move (pfnCallback));
      return it->second.get ();
   }

   auto pEntry = std::make_unique<CACHE_ENTRY> (sUrl, "");
   CACHE_ENTRY* pRaw = pEntry.get ();
   m_mapAsset[sUrl] = std::move (pEntry);
   pRaw->AddCallback (std::move (pfnCallback));
   pRaw->SetFetching ();
   return pRaw;
}

// ---------------------------------------------------------------------------
// Verified module tier (persistent)
// ---------------------------------------------------------------------------

CACHE_ENTRY* FILE_CACHE::Request_Module (const std::string& sUrl, const std::string& sSha256, CACHE_CALLBACK pfnCallback)
{
   std::string sKey = MakeModuleKey (sUrl, sSha256);

   std::lock_guard<std::mutex> guard (m_moduleMutex);

   auto it = m_mapModule.find (sKey);
   if (it != m_mapModule.end ())
   {
      it->second->AddCallback (std::move (pfnCallback));
      return it->second.get ();
   }

   // Check persistent disk cache
   std::vector<uint8_t> aDiskData;
   if (LoadModuleFromDisk (sUrl, sSha256, aDiskData))
   {
      auto pEntry = std::make_unique<CACHE_ENTRY> (sUrl, sSha256);
      CACHE_ENTRY* pRaw = pEntry.get ();
      m_mapModule[sKey] = std::move (pEntry);
      pRaw->Complete (aDiskData);
      pRaw->AddCallback (std::move (pfnCallback));
      return pRaw;
   }

   auto pEntry = std::make_unique<CACHE_ENTRY> (sUrl, sSha256);
   CACHE_ENTRY* pRaw = pEntry.get ();
   m_mapModule[sKey] = std::move (pEntry);
   pRaw->AddCallback (std::move (pfnCallback));
   pRaw->SetFetching ();
   return pRaw;
}

// ---------------------------------------------------------------------------
// Direct insertion
// ---------------------------------------------------------------------------

void FILE_CACHE::Insert_Msf (const std::string& sUrl, const std::vector<uint8_t>& aData)
{
   std::lock_guard<std::mutex> guard (m_msfMutex);

   auto it = m_mapMsf.find (sUrl);
   if (it != m_mapMsf.end ())
   {
      it->second->Complete (aData);
      return;
   }

   auto pEntry = std::make_unique<CACHE_ENTRY> (sUrl, "");
   pEntry->Complete (aData);
   m_mapMsf[sUrl] = std::move (pEntry);
}

void FILE_CACHE::Insert_Asset (const std::string& sUrl, const std::vector<uint8_t>& aData)
{
   std::lock_guard<std::mutex> guard (m_assetMutex);

   auto it = m_mapAsset.find (sUrl);
   if (it != m_mapAsset.end ())
   {
      it->second->Complete (aData);
      return;
   }

   auto pEntry = std::make_unique<CACHE_ENTRY> (sUrl, "");
   pEntry->Complete (aData);
   m_mapAsset[sUrl] = std::move (pEntry);
}

void FILE_CACHE::Insert_Module (const std::string& sUrl, const std::string& sSha256, const std::vector<uint8_t>& aData)
{
   std::string sKey = MakeModuleKey (sUrl, sSha256);

   std::lock_guard<std::mutex> guard (m_moduleMutex);

   auto it = m_mapModule.find (sKey);
   if (it != m_mapModule.end ())
   {
      it->second->Complete (aData);
   }
   else
   {
      auto pEntry = std::make_unique<CACHE_ENTRY> (sUrl, sSha256);
      pEntry->Complete (aData);
      m_mapModule[sKey] = std::move (pEntry);
   }

   SaveModuleToDisk (sUrl, sSha256, aData);
}

// ---------------------------------------------------------------------------
// Lookup
// ---------------------------------------------------------------------------

CACHE_ENTRY* FILE_CACHE::Find_Msf (const std::string& sUrl) const
{
   std::lock_guard<std::mutex> guard (m_msfMutex);
   auto it = m_mapMsf.find (sUrl);
   return (it != m_mapMsf.end ()) ? it->second.get () : nullptr;
}

CACHE_ENTRY* FILE_CACHE::Find_Asset (const std::string& sUrl) const
{
   std::lock_guard<std::mutex> guard (m_assetMutex);
   auto it = m_mapAsset.find (sUrl);
   return (it != m_mapAsset.end ()) ? it->second.get () : nullptr;
}

CACHE_ENTRY* FILE_CACHE::Find_Module (const std::string& sUrl, const std::string& sSha256) const
{
   std::string sKey = MakeModuleKey (sUrl, sSha256);
   std::lock_guard<std::mutex> guard (m_moduleMutex);
   auto it = m_mapModule.find (sKey);
   return (it != m_mapModule.end ()) ? it->second.get () : nullptr;
}

// ---------------------------------------------------------------------------
// Session management
// ---------------------------------------------------------------------------

void FILE_CACHE::ClearSession ()
{
   {
      std::lock_guard<std::mutex> guard (m_msfMutex);
      m_mapMsf.clear ();
   }
   {
      std::lock_guard<std::mutex> guard (m_assetMutex);
      m_mapAsset.clear ();
   }
}

// ---------------------------------------------------------------------------
// Internals
// ---------------------------------------------------------------------------

std::string FILE_CACHE::MakeModuleKey (const std::string& sUrl, const std::string& sSha256) const
{
   return sUrl + "|" + sSha256;
}

std::string FILE_CACHE::GetPersistentCachePath () const
{
#ifdef _WIN32
   char szPath[MAX_PATH] = {};
   if (SUCCEEDED (SHGetFolderPathA (nullptr, CSIDL_APPDATA, nullptr, 0, szPath)))
   {
      std::string sPath (szPath);
      sPath += "\\Sneeze\\Cache";
      return sPath;
   }
   return "";
#else
   const char* pHome = std::getenv ("HOME");
   if (pHome)
   {
      std::string sPath (pHome);
      sPath += "/.sneeze/cache";
      return sPath;
   }
   return "";
#endif
}

bool FILE_CACHE::LoadModuleFromDisk (const std::string& sUrl, const std::string& sSha256, std::vector<uint8_t>& aOut) const
{
   std::string sFilePath = m_sCachePath + "/" + sSha256;
   std::ifstream file (sFilePath, std::ios::binary | std::ios::ate);
   if (!file.is_open ())
      return false;

   auto nSize = file.tellg ();
   if (nSize <= 0)
      return false;

   aOut.resize (static_cast<size_t> (nSize));
   file.seekg (0, std::ios::beg);
   file.read (reinterpret_cast<char*> (aOut.data ()), nSize);
   return file.good ();
}

void FILE_CACHE::SaveModuleToDisk (const std::string& sUrl, const std::string& sSha256, const std::vector<uint8_t>& aData) const
{
   std::filesystem::create_directories (m_sCachePath);
   std::string sFilePath = m_sCachePath + "/" + sSha256;

   std::ofstream file (sFilePath, std::ios::binary | std::ios::trunc);
   if (file.is_open ())
      file.write (reinterpret_cast<const char*> (aData.data ()), aData.size ());
}

}} // namespace sneeze::cache
