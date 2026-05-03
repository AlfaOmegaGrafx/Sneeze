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

#include "Manager.h"
#include "Entry.h"
#include "File.h"
#include "Store.h"
#include "core/Sneeze.h"
#include "net/HttpClient.h"

#include <nlohmann/json.hpp>
#include <openssl/evp.h>
#include <openssl/sha.h>
#include <curl/curl.h>

#include <fstream>
#include <filesystem>
#include <sstream>
#include <cstdio>
#include <cstring>
#include <algorithm>


namespace SNEEZE { namespace CACHE {

// ---------------------------------------------------------------------------
// Curl callbacks for streaming to a file with header capture
// ---------------------------------------------------------------------------

struct CURL_FETCH_CONTEXT
{
   std::FILE*   pFile;
   std::unordered_map<std::string, std::string> mapHeaders;
   long         nHttpCode;
};

static size_t FetchWriteCallback (char* pData, size_t nSize, size_t nMembers, void* pUser)
{
   CURL_FETCH_CONTEXT* pCtx = static_cast<CURL_FETCH_CONTEXT*> (pUser);
   return std::fwrite (pData, nSize, nMembers, pCtx->pFile);
}

static size_t FetchHeaderCallback (char* pData, size_t nSize, size_t nMembers, void* pUser)
{
   CURL_FETCH_CONTEXT* pCtx = static_cast<CURL_FETCH_CONTEXT*> (pUser);
   size_t nTotal = nSize * nMembers;
   std::string sLine (pData, nTotal);

   auto nColon = sLine.find (':');
   if (nColon != std::string::npos)
   {
      std::string sKey   = sLine.substr (0, nColon);
      std::string sValue = sLine.substr (nColon + 1);

      auto nStart = sValue.find_first_not_of (" \t\r\n");
      auto nEnd   = sValue.find_last_not_of (" \t\r\n");
      if (nStart != std::string::npos  &&  nEnd != std::string::npos)
         sValue = sValue.substr (nStart, nEnd - nStart + 1);
      else
         sValue.clear ();

      std::transform (sKey.begin (), sKey.end (), sKey.begin (), ::tolower);
      pCtx->mapHeaders[sKey] = sValue;
   }

   return nTotal;
}

// ---------------------------------------------------------------------------
// MANAGER
// ---------------------------------------------------------------------------

MANAGER::MANAGER (CORE::SNEEZE* pSneeze) :
   m_pSneeze         (pSneeze),
   m_bShuttingDown   (false),
   m_bCacheEnabled   (true),
   m_bDisplayEnabled (true),
   m_nNextEntryIx    (1),
   m_nNextFileIx     (1),
   m_tpEpoch         (std::chrono::steady_clock::now ())
{
}

MANAGER::~MANAGER ()
{
   Shutdown ();
}

bool MANAGER::Initialize ()
{
   bool bResult = false;

   m_tpEpoch    = std::chrono::steady_clock::now ();
   m_sCachePath = GetCachePath ();

   if (!m_sCachePath.empty ())
   {
      std::filesystem::create_directories (m_sCachePath);

      LoadRules ();

      m_pSneeze->Log (CORE::ISNEEZE::kLOGLEVEL_Info, "CACHE", "Initialized (path: " + m_sCachePath + ", rules: " + std::to_string (m_aRules.size ()) + ", entryIx: " + std::to_string (m_nNextEntryIx) + ")");

      bResult = true;
   }
   else
   {
      m_pSneeze->Log (CORE::ISNEEZE::kLOGLEVEL_Error, "CACHE", "Failed to determine cache path");
   }

   return bResult;
}

void MANAGER::Shutdown ()
{
   if (!m_sCachePath.empty ())
   {
      m_bShuttingDown.store (true);

      for (auto* pSlot : m_apFetchSlots)
      {
         if (pSlot->thread.joinable ())
            pSlot->thread.join ();
         delete pSlot;
      }
      m_apFetchSlots.clear ();

      while (!m_aFetchQueue.empty ())
         m_aFetchQueue.pop ();

      {
         std::lock_guard<std::recursive_mutex> guard (m_mutex);

         for (auto& [sUrl, pEntry] : m_mapEntries)
         {
            if (pEntry->GetState () == STATE_READY)
               SaveMeta (pEntry.get ());
         }

         SaveRules ();

         m_mapEntries.clear ();
      }

      DeleteFiles ();

      m_sCachePath.clear ();
   }
}

// ---------------------------------------------------------------------------
// Timing helpers
// ---------------------------------------------------------------------------

double MANAGER::SecondsSinceEpoch () const
{
   auto tpNow = std::chrono::steady_clock::now ();
 
   return std::chrono::duration<double> (tpNow - m_tpEpoch).count ();
}

double MANAGER::GetEpochAge () const
{
   return SecondsSinceEpoch ();
}

// ---------------------------------------------------------------------------
// File ownership
// ---------------------------------------------------------------------------

void MANAGER::DeleteFiles ()
{
   for (auto* pFile : m_apFile)
      delete pFile;
   m_apFile.clear ();
}

void MANAGER::ResetEntry (ENTRY* pEntry)
{
   std::string sDiskKey = ComputeDiskKey (pEntry->GetUrl ());
   std::error_code ec;

   std::filesystem::remove (DiskKeyToPath (sDiskKey, DISKFILE_DATA), ec);
   std::filesystem::remove (DiskKeyToPath (sDiskKey, DISKFILE_META), ec);
   std::filesystem::remove (DiskKeyToPath (sDiskKey, DISKFILE_TEMP), ec);

   pEntry->ResetState ();
   pEntry->SetEntryIx (m_nNextEntryIx++);
}

// ---------------------------------------------------------------------------
// Fetch thread management (capped at kMAX_CONCURRENT_FETCHES)
// ---------------------------------------------------------------------------

void MANAGER::SweepCompletedThreads ()
{
   auto it = m_apFetchSlots.begin ();
   while (it != m_apFetchSlots.end ())
   {
      FETCH_SLOT* pSlot = *it;
      if (pSlot->bDone.load ())
      {
         if (pSlot->thread.joinable ())
            pSlot->thread.join ();
         delete pSlot;
         it = m_apFetchSlots.erase (it);
      }
      else
      {
         ++it;
      }
   }
}

void MANAGER::DispatchFetch (ENTRY* pEntry)
{
   SweepCompletedThreads ();

   if (static_cast<int> (m_apFetchSlots.size ()) < kMAX_CONCURRENT_FETCHES)
   {
      FETCH_SLOT* pSlot = new FETCH_SLOT ();
      pSlot->thread = std::thread ([this, pEntry, pSlot] ()
      {
         FetchEntry (pEntry);
         pSlot->bDone.store (true);
      });
      m_apFetchSlots.push_back (pSlot);
   }
   else
   {
      m_aFetchQueue.push (pEntry);
   }
}

// ---------------------------------------------------------------------------
// Request / Release
// ---------------------------------------------------------------------------

FILE* MANAGER::Request (IFILE* pListener, const std::string& sStore, const std::string& sUrl)
{
   return Request (pListener, sStore, sUrl, std::string (), kREQUEST_DEFAULT);
}

FILE* MANAGER::Request (IFILE* pListener, const std::string& sStore, const std::string& sUrl, const std::string& sHash, uint32_t bFlags, uint32_t nEntryIx)
{
   bool bCreate = (bFlags & REQUEST_CREATE) != 0;
   bool bFetch  = (bFlags & REQUEST_FETCH)  != 0;

   FILE* pFile = nullptr;

   {
      std::lock_guard<std::recursive_mutex> guard (m_mutex);

      STORE* pStore = FindOrCreateStore (sStore);

      // Resolve entry: map → disk → create
      auto it = m_mapEntries.find (sUrl);

      if (it == m_mapEntries.end ())
      {
         std::string sDiskKey = ComputeDiskKey (sUrl);
         if (LoadMeta (sDiskKey, sUrl))
            it = m_mapEntries.find (sUrl);
      }

      if (it == m_mapEntries.end ()  &&  bCreate  &&  bFetch)
      {
         auto pEntryPtr = std::make_unique<ENTRY> (this, sUrl, sHash);
         pEntryPtr->SetEntryIx (m_nNextEntryIx++);
         m_mapEntries[sUrl] = std::move (pEntryPtr);
         it = m_mapEntries.find (sUrl);
      }

      if (it != m_mapEntries.end ())
      {
         ENTRY* pEntry = it->second.get ();

         // Entry-index staleness: caller expected a specific version
         if (nEntryIx != 0  &&  pEntry->GetEntryIx () != nEntryIx)
            it = m_mapEntries.end ();
      }

      if (it != m_mapEntries.end ())
      {
         ENTRY* pEntry = it->second.get ();

         // Staleness check for entries loaded from disk
         if (pEntry->GetState () == STATE_READY  &&  IsEntryStale (pEntry))
         {
            if (!bFetch)
            {
               it = m_mapEntries.end ();
            }
            else
            {
               ResetEntry (pEntry);
            }
         }
      }

      if (it != m_mapEntries.end ())
      {
         ENTRY* pEntry = it->second.get ();

         pFile = new FILE (this, pEntry, pStore, pListener, m_nNextFileIx++);
         pEntry->AttachFile (pFile);
         m_apFile.push_back (pFile);

         STATE bState      = pEntry->GetState ();
         bool  bNeedsFetch = false;

         if (bState == STATE_READY  &&  !sHash.empty ()  &&  !pEntry->IsHashed ())
         {
            std::string sAlgo, sDigest;
            if (ParseSriHash (sHash, sAlgo, sDigest))
            {
               std::string sComputed = ComputeFileHash (pEntry->GetDiskPath (), sAlgo);
               if (sComputed == sDigest)
               {
                  pEntry->SetHash (sHash);
                  pEntry->TouchAccess ();
                  pEntry->SetServedFromCache (true);
                  pFile->SnapshotEntry ();
               }
               else
               {
                  pEntry->SetHash (sHash);
                  bNeedsFetch = true;
               }
            }
         }
         else if (bState != STATE_FETCHING  &&  !sHash.empty ()  &&  pEntry->IsHashed ()  &&  pEntry->GetHash () != sHash)
         {
            pEntry->SetHash (sHash);
            bNeedsFetch = true;
         }
         else if (bState == STATE_READY  &&  !m_bCacheEnabled)
         {
            pEntry->SetServedFromCache (false);
            bNeedsFetch = true;
         }
         else if (bState == STATE_READY)
         {
            pEntry->TouchAccess ();
            pEntry->SetServedFromCache (true);
            pFile->SnapshotEntry ();
            if (pListener)
               pListener->OnFileReady (pFile);
         }
         else if (bState == STATE_FAILED)
         {
            pFile->SnapshotEntry ();
            if (pListener)
               pListener->OnFileFailed (pFile);
         }
         else if (bState == STATE_IDLE)
         {
            if (!sHash.empty ())
               pEntry->SetHash (sHash);
            bNeedsFetch = true;
         }

         if (bNeedsFetch  &&  bFetch)
         {
            pEntry->SetFetching ();
            pEntry->SetFetchQueuedTime (SecondsSinceEpoch ());
            pFile->SnapshotEntry ();
            DispatchFetch (pEntry);
         }
         else if (bNeedsFetch  &&  !bFetch)
         {
            pFile->SnapshotEntry ();
            if (pListener)
               pListener->OnFileFailed (pFile);
         }

         if (!m_bDisplayEnabled)
            pFile->SetPendingClear (true);

         if (!pFile->IsPendingClear ())
            m_pSneeze->NotifyCacheFileCreated (pFile);
      }
   }

   return pFile;
}

void MANAGER::Release (FILE* pFile)
{
   if (pFile)
   {
      std::lock_guard<std::recursive_mutex> guard (m_mutex);

      ENTRY* pEntry = pFile->GetEntry ();

      if (pEntry)
      {
         pFile->SnapshotEntry ();
         pEntry->DetachFile (pFile);
         pFile->SetEntry (nullptr);

         if (pEntry->GetFileCount () == 0)
         {
            if (pEntry->IsPendingReset ())
               ResetEntry (pEntry);
            else if (pEntry->GetState () == STATE_READY)
               SaveMeta (pEntry);

            std::string sUrl = pEntry->GetUrl ();
            m_mapEntries.erase (sUrl);
         }
      }

      pFile->SetReleased ();

      if (pFile->IsPendingClear ())
      {
         auto it = std::find (m_apFile.begin (), m_apFile.end (), pFile);
         if (it != m_apFile.end ())
            m_apFile.erase (it);
         delete pFile;
      }
   }
}

bool MANAGER::ReopenFile (FILE* pFile)
{
   bool bResult = false;

   if (pFile  &&  !pFile->GetUrl ().empty ())
   {
      std::lock_guard<std::recursive_mutex> guard (m_mutex);

      std::string sUrl      = pFile->GetUrl ();
      uint32_t    nEntryIx  = pFile->GetEntryIx ();

      auto it = m_mapEntries.find (sUrl);

      if (it == m_mapEntries.end ())
      {
         std::string sDiskKey = ComputeDiskKey (sUrl);
         if (LoadMeta (sDiskKey, sUrl))
            it = m_mapEntries.find (sUrl);
      }

      if (it != m_mapEntries.end ())
      {
         ENTRY* pEntry = it->second.get ();

         if (pEntry->GetEntryIx () == nEntryIx)
         {
            pFile->SetEntry (pEntry);
            pEntry->AttachFile (pFile);
            pFile->SnapshotEntry ();
            bResult = true;

            IFILE* pListener = pFile->GetListener ();
            if (pListener)
            {
               if (pEntry->GetState () == STATE_READY)
                  pListener->OnFileReady (pFile);
               else
                  pListener->OnFileFailed (pFile);
            }
         }
      }
   }

   return bResult;
}

void MANAGER::Clear (FILE* pFile, bool b)
{
   if (pFile)
   {
      std::lock_guard<std::recursive_mutex> guard (m_mutex);

      if (pFile->SetPendingClear (b))
      {
         if (b)
         {
            m_pSneeze->NotifyCacheFileDeleted (pFile);

            if (pFile->IsReleased ())
            {
               auto it = std::find (m_apFile.begin (), m_apFile.end (), pFile);
               if (it != m_apFile.end ())
                  m_apFile.erase (it);
               delete pFile;
            }
         }
         else
         {
            m_pSneeze->NotifyCacheFileCreated (pFile);
         }
      }
   }
}

void MANAGER::Reset (FILE* pFile, bool b)
{
   if (pFile  &&  pFile->GetEntry ())
   {
      std::lock_guard<std::recursive_mutex> guard (m_mutex);

      pFile->GetEntry ()->SetPendingReset (b);
   }
}

// ---------------------------------------------------------------------------
// Cache management
// ---------------------------------------------------------------------------

void MANAGER::Clear ()
{
   std::lock_guard<std::recursive_mutex> guard (m_mutex);

   auto it = m_apFile.begin ();
   while (it != m_apFile.end ())
   {
      FILE* pFile = *it;

      if (pFile->SetPendingClear (true))
         m_pSneeze->NotifyCacheFileDeleted (pFile);

      if (pFile->IsReleased ())
      {
         it = m_apFile.erase (it);
         delete pFile;
      }
      else
      {
         ++it;
      }
   }
}

void MANAGER::Reset ()
{
   std::lock_guard<std::recursive_mutex> guard (m_mutex);

   for (auto& [sUrl, pEntryPtr] : m_mapEntries)
   {
      ENTRY* pEntry = pEntryPtr.get ();
      pEntry->SetPendingReset (true);
      if (pEntry->GetFileCount () == 0)
         ResetEntry (pEntry);
   }

   std::error_code ec;
   std::filesystem::remove_all (m_sCachePath, ec);
   std::filesystem::create_directories (m_sCachePath);

   SaveRules ();
}

void MANAGER::Enumerate (IENUM* pEnum)
{
   std::lock_guard<std::recursive_mutex> guard (m_mutex);

   FILE* pFile = new FILE (this, nullptr, nullptr, nullptr, 0);
   pFile->SetEnumeration (true);

   for (auto& pDirEntry : std::filesystem::recursive_directory_iterator (m_sCachePath))
   {
      if (!pDirEntry.is_regular_file ()  ||  pDirEntry.path ().extension () != ".meta")
         continue;

      std::ifstream metaFile (pDirEntry.path ());
      if (!metaFile.is_open ())
         continue;

      nlohmann::json jMeta;
      bool bParsed = false;

      try
      {
         metaFile >> jMeta;
         bParsed = true;
      }
      catch (...) {}

      if (!bParsed)
         continue;

      std::string sUrl  = jMeta.value ("url", "");
      std::string sHash = jMeta.value ("hash", "");
      if (sUrl.empty ())
         continue;

      auto it = m_mapEntries.find (sUrl);
      if (it != m_mapEntries.end ())
      {
         ENTRY* pEntry = it->second.get ();
         pFile->SetEntry (pEntry);
         pFile->SnapshotEntry ();
         pEntry->AttachFile (pFile);

         pEnum->OnEntry (pFile);

         pEntry->DetachFile (pFile);
      }
      else
      {
         std::string sDataPath = pDirEntry.path ().string ();
         sDataPath = sDataPath.substr (0, sDataPath.size () - 5) + ".data";

         auto pEntry = std::make_unique<ENTRY> (this, sUrl, sHash);
         pEntry->SetDiskPath (sDataPath);
         pEntry->SetSizeBytes (jMeta.value ("sizeBytes", static_cast<uint64_t> (0)));
         pEntry->SetCreatedTime (jMeta.value ("createdAt", ""));
         pEntry->SetEntryIx (jMeta.value ("entryIx", static_cast<uint32_t> (0)));
         pEntry->SetHttpStatus (jMeta.value ("httpStatus", static_cast<long> (0)));

         if (jMeta.contains ("headers"))
         {
            std::unordered_map<std::string, std::string> mapHeaders;
            for (auto& [sKey, sVal] : jMeta["headers"].items ())
               mapHeaders[sKey] = sVal.get<std::string> ();
            pEntry->SetHeaders (mapHeaders);
         }

         if (std::filesystem::exists (sDataPath))
            pEntry->Complete (sDataPath, pEntry->GetSizeBytes ());

         ENTRY* pRaw = pEntry.get ();
         pFile->SetEntry (pRaw);
         pFile->SnapshotEntry ();
         pRaw->AttachFile (pFile);

         pEnum->OnEntry (pFile);

         pRaw->DetachFile (pFile);
      }
   }

   pFile->SetEntry (nullptr);
   delete pFile;
}

// ---------------------------------------------------------------------------
// Path helpers
// ---------------------------------------------------------------------------

std::string MANAGER::GetCachePath () const
{
   std::string sResult;
   std::string sAppData = m_pSneeze->GetHost ()->sAppDataPath;
   if (!sAppData.empty ())
      sResult = (std::filesystem::path (sAppData) / "Cache").string ();
   return sResult;
}

std::string MANAGER::ComputeDiskKey (const std::string& sUrl) const
{
   unsigned char aDigest[SHA_DIGEST_LENGTH];
   SHA1 (reinterpret_cast<const unsigned char*> (sUrl.data ()),
      sUrl.size (), aDigest);

   static const int kTRUNCATED_BYTES = 12;
   char szHex[kTRUNCATED_BYTES * 2 + 1];
   for (int i = 0; i < kTRUNCATED_BYTES; i++)
      std::sprintf (szHex + i * 2, "%02x", aDigest[i]);
   szHex[kTRUNCATED_BYTES * 2] = '\0';

   return std::string (szHex);
}

std::string MANAGER::DiskKeyToPath (const std::string& sDiskKey, DISKFILE eType) const
{
   static const char* aExt[] = { ".data", ".temp", ".meta" };

   std::string sDir  = sDiskKey.substr (0, 2);
   std::string sFile = sDiskKey.substr (2);
   return (std::filesystem::path (m_sCachePath) / sDir / sFile).string () + aExt[eType];
}

// ---------------------------------------------------------------------------
// SRI hash parsing and verification
// ---------------------------------------------------------------------------

bool MANAGER::ParseSriHash (const std::string& sSri, std::string& sAlgo, std::string& sDigest) const
{
   bool bResult = false;

   auto nDash = sSri.find ('-');
   if (nDash != std::string::npos  &&  nDash > 0  &&  nDash < sSri.size () - 1)
   {
      sAlgo  = sSri.substr (0, nDash);
      sDigest = sSri.substr (nDash + 1);
      bResult = (sAlgo == "sha256"  ||  sAlgo == "sha384"  ||  sAlgo == "sha512");
   }

   return bResult;
}

static const EVP_MD* GetEvpMd (const std::string& sAlgo)
{
   const EVP_MD* pMd = nullptr;
   if (sAlgo == "sha256")
      pMd = EVP_sha256 ();
   else if (sAlgo == "sha384")
      pMd = EVP_sha384 ();
   else if (sAlgo == "sha512")
      pMd = EVP_sha512 ();
   return pMd;
}

std::string MANAGER::ComputeFileHash (const std::string& sFilePath, const std::string& sAlgo) const
{
   std::string sResult;

   const EVP_MD* pMd = GetEvpMd (sAlgo);
   if (pMd)
   {
      std::ifstream file (sFilePath, std::ios::binary);
      if (file.is_open ())
      {
         EVP_MD_CTX* pCtx = EVP_MD_CTX_new ();
         if (pCtx)
         {
            EVP_DigestInit_ex (pCtx, pMd, nullptr);

            char aBuffer[8192];
            while (file.read (aBuffer, sizeof (aBuffer))  ||  file.gcount () > 0)
            {
               EVP_DigestUpdate (pCtx, aBuffer, static_cast<size_t> (file.gcount ()));
               if (file.eof ())
                  break;
            }

            unsigned char aDigest[EVP_MAX_MD_SIZE];
            unsigned int nDigestLen = 0;
            EVP_DigestFinal_ex (pCtx, aDigest, &nDigestLen);
            EVP_MD_CTX_free (pCtx);

            std::string sHex;
            sHex.reserve (nDigestLen * 2);
            for (unsigned int i = 0; i < nDigestLen; i++)
            {
               char szByte[3];
               std::sprintf (szByte, "%02x", aDigest[i]);
               sHex += szByte;
            }

            sResult = sHex;
         }
      }
   }

   return sResult;
}

std::string MANAGER::ComputeDataHash (const uint8_t* pData, size_t nLen, const std::string& sAlgo) const
{
   std::string sResult;

   const EVP_MD* pMd = GetEvpMd (sAlgo);
   if (pMd  &&  pData)
   {
      unsigned char aDigest[EVP_MAX_MD_SIZE];
      unsigned int nDigestLen = 0;
      EVP_Digest (pData, nLen, aDigest, &nDigestLen, pMd, nullptr);

      std::string sHex;
      sHex.reserve (nDigestLen * 2);
      for (unsigned int i = 0; i < nDigestLen; i++)
      {
         char szByte[3];
         std::sprintf (szByte, "%02x", aDigest[i]);
         sHex += szByte;
      }

      sResult = sHex;
   }

   return sResult;
}

// ---------------------------------------------------------------------------
// Sidecar metadata (per-entry .meta files)
// ---------------------------------------------------------------------------

void MANAGER::SaveMeta (ENTRY* pEntry)
{
   std::string sDiskKey  = ComputeDiskKey (pEntry->GetUrl ());
   std::string sMetaPath = DiskKeyToPath (sDiskKey, DISKFILE_META);

   std::filesystem::create_directories (std::filesystem::path (sMetaPath).parent_path ());

   nlohmann::json jMeta;
   jMeta["url"]            = pEntry->GetUrl ();
   jMeta["hash"]           = pEntry->GetHash ();
   jMeta["entryIx"]        = pEntry->GetEntryIx ();
   jMeta["sizeBytes"]      = pEntry->GetSizeBytes ();
   jMeta["createdAt"]      = pEntry->GetCreatedTime ();
   jMeta["lastAccessedAt"] = pEntry->GetLastAccessTime ();
   jMeta["accessCount"]    = pEntry->GetAccessCount ();
   jMeta["httpStatus"]     = pEntry->GetHttpStatus ();

   nlohmann::json jHeaders = nlohmann::json::object ();
   for (auto& [sKey, sVal] : pEntry->GetHeaders ())
      jHeaders[sKey] = sVal;
   jMeta["headers"] = jHeaders;

   std::string sTmpPath = sMetaPath + ".temp";
   std::ofstream file (sTmpPath, std::ios::trunc);
   if (file.is_open ())
   {
      file << jMeta.dump (2);
      file.close ();

      std::error_code ec;
      std::filesystem::rename (sTmpPath, sMetaPath, ec);
      if (ec)
         m_pSneeze->Log (CORE::ISNEEZE::kLOGLEVEL_Warning, "CACHE", "Failed to rename meta temp: " + ec.message ());
   }
}

bool MANAGER::LoadMeta (const std::string& sDiskKey, const std::string& sUrl)
{
   bool bResult = false;

   std::string sMetaPath = DiskKeyToPath (sDiskKey, DISKFILE_META);
   std::string sDataPath = DiskKeyToPath (sDiskKey, DISKFILE_DATA);

   std::ifstream file (sMetaPath);
   if (file.is_open ())
   {
      nlohmann::json jMeta;
      bool bParsed = false;

      try
      {
         file >> jMeta;
         bParsed = true;
      }
      catch (...)
      {
         m_pSneeze->Log (CORE::ISNEEZE::kLOGLEVEL_Warning, "CACHE", "Failed to parse sidecar: " + sMetaPath);
      }

      if (bParsed)
      {
         std::string sMetaUrl = jMeta.value ("url", "");
         if (sMetaUrl == sUrl  &&  std::filesystem::exists (sDataPath))
         {
            std::string sHash = jMeta.value ("hash", "");
            auto pEntry = std::make_unique<ENTRY> (this, sUrl, sHash);
            pEntry->SetDiskPath (sDataPath);
            pEntry->SetSizeBytes (jMeta.value ("sizeBytes", static_cast<uint64_t> (0)));
            pEntry->SetCreatedTime (jMeta.value ("createdAt", ""));
            pEntry->SetEntryIx (jMeta.value ("entryIx", static_cast<uint32_t> (0)));

            if (jMeta.contains ("headers"))
            {
               std::unordered_map<std::string, std::string> mapHeaders;
               for (auto& [sKey, sVal] : jMeta["headers"].items ())
                  mapHeaders[sKey] = sVal.get<std::string> ();
               pEntry->SetHeaders (mapHeaders);
            }

            pEntry->SetHttpStatus (jMeta.value ("httpStatus", static_cast<long> (0)));
            pEntry->SetServedFromCache (true);
            pEntry->Complete (sDataPath, pEntry->GetSizeBytes ());

            m_mapEntries[sUrl] = std::move (pEntry);
            bResult = true;
         }
      }
   }

   return bResult;
}

// ---------------------------------------------------------------------------
// Staleness rules (persisted in rules.json)
// ---------------------------------------------------------------------------

void MANAGER::LoadRules ()
{
   std::string sRulesPath = (std::filesystem::path (m_sCachePath) / "rules.json").string ();
   std::ifstream file (sRulesPath);
   if (file.is_open ())
   {
      nlohmann::json jDoc;
      bool bParsed = false;

      try
      {
         file >> jDoc;
         bParsed = true;
      }
      catch (...)
      {
         m_pSneeze->Log (CORE::ISNEEZE::kLOGLEVEL_Warning, "CACHE", "Failed to parse rules.json — defaulting to stale");
      }

      if (bParsed)
      {
         m_nNextEntryIx = jDoc.value ("nextEntryIx", static_cast<uint32_t> (1));

         if (jDoc.contains ("rules"))
         {
            for (auto& jRule : jDoc["rules"])
            {
               RULE rule;
               rule.sContentType = jRule.value ("contentType", "");
               rule.sOlderThan   = jRule.value ("olderThan", "");
               m_aRules.push_back (rule);
            }
         }
      }
   }
   else
   {
      m_pSneeze->Log (CORE::ISNEEZE::kLOGLEVEL_Info, "CACHE", "No rules.json — creating fresh");
      SaveRules ();
   }
}

void MANAGER::SaveRules ()
{
   if (m_sCachePath.empty ())
      return;

   nlohmann::json jDoc;
   jDoc["nextEntryIx"] = m_nNextEntryIx;

   nlohmann::json jRules = nlohmann::json::array ();
   for (auto& rule : m_aRules)
   {
      nlohmann::json jRule;
      jRule["contentType"] = rule.sContentType;
      jRule["olderThan"]   = rule.sOlderThan;
      jRules.push_back (jRule);
   }
   jDoc["rules"] = jRules;

   std::string sRulesPath = (std::filesystem::path (m_sCachePath) / "rules.json"     ).string ();
   std::string sTmpPath   = (std::filesystem::path (m_sCachePath) / "rules.json.temp").string ();

   std::ofstream file (sTmpPath, std::ios::trunc);
   if (file.is_open ())
   {
      file << jDoc.dump (2);
      file.close ();

      std::error_code ec;
      std::filesystem::rename (sTmpPath, sRulesPath, ec);
   }
}

void MANAGER::AddRule (const std::string& sContentType, const std::string& sOlderThan)
{
   std::lock_guard<std::recursive_mutex> guard (m_mutex);

   if (sContentType.empty ())
      m_aRules.clear ();

   RULE rule;
   rule.sContentType = sContentType;
   rule.sOlderThan   = sOlderThan;
   m_aRules.push_back (rule);

   SaveRules ();
}

bool MANAGER::IsEntryStale (ENTRY* pEntry) const
{
   bool bResult = false;

   std::string sContentType = pEntry->GetHeader ("content-type");
   std::string sCreatedAt   = pEntry->GetCreatedTime ();

   for (auto& rule : m_aRules)
   {
      bool bTypeMatch = rule.sContentType.empty ()  ||  rule.sContentType == sContentType;
      bool bTimeMatch = !rule.sOlderThan.empty ()  &&  sCreatedAt < rule.sOlderThan;

      if (bTypeMatch  &&  bTimeMatch)
      {
         bResult = true;
         break;
      }
   }

   return bResult;
}

// ---------------------------------------------------------------------------
// Background fetch
// ---------------------------------------------------------------------------

void MANAGER::FetchEntry (ENTRY* pEntry)
{
   std::string sUrl;
   std::string sHash;
   {
      std::lock_guard<std::recursive_mutex> guard (m_mutex);

      sUrl  = pEntry->GetUrl ();
      sHash = pEntry->GetHash ();
      pEntry->SetFetchStartTime (SecondsSinceEpoch ());
   }
   std::string sDiskKey  = ComputeDiskKey (sUrl);
   std::string sTmpPath  = DiskKeyToPath (sDiskKey, DISKFILE_TEMP);
   std::string sFinalPath;
   uint64_t    nSizeBytes = 0;
   bool        bOk        = !m_bShuttingDown.load ();
   bool        bHaveTmp   = false;
   CURL_FETCH_CONTEXT ctx = {};
   ctx.nHttpCode = 0;

   // Stage 1: Open temp file
   if (bOk)
   {
      std::filesystem::create_directories (std::filesystem::path (sTmpPath).parent_path ());

      std::FILE* pTmpFile = nullptr;
#ifdef _WIN32
      fopen_s (&pTmpFile, sTmpPath.c_str (), "wb");
#else
      pTmpFile = std::fopen (sTmpPath.c_str (), "wb");
#endif

      if (pTmpFile)
      {
         bHaveTmp  = true;
         ctx.pFile = pTmpFile;
      }
      else
      {
         m_pSneeze->Log (CORE::ISNEEZE::kLOGLEVEL_Error, "CACHE", "Failed to open temp file: " + sTmpPath);
         bOk = false;
      }
   }

   // Stage 2: Perform HTTP fetch
   if (bOk)
   {
      CURL* pCurl = curl_easy_init ();
      if (pCurl)
      {
         curl_easy_setopt (pCurl, CURLOPT_URL, sUrl.c_str ());
         curl_easy_setopt (pCurl, CURLOPT_WRITEFUNCTION, FetchWriteCallback);
         curl_easy_setopt (pCurl, CURLOPT_WRITEDATA, &ctx);
         curl_easy_setopt (pCurl, CURLOPT_HEADERFUNCTION, FetchHeaderCallback);
         curl_easy_setopt (pCurl, CURLOPT_HEADERDATA, &ctx);
         curl_easy_setopt (pCurl, CURLOPT_FOLLOWLOCATION, 1L);
         curl_easy_setopt (pCurl, CURLOPT_TIMEOUT, 300L);

         CURLcode nCode = curl_easy_perform (pCurl);

         if (nCode == CURLE_OK)
            curl_easy_getinfo (pCurl, CURLINFO_RESPONSE_CODE, &ctx.nHttpCode);

         curl_easy_cleanup (pCurl);
         std::fclose (ctx.pFile);
         ctx.pFile = nullptr;

         if (m_bShuttingDown.load ())
            bOk = false;
         else if (nCode != CURLE_OK  ||  ctx.nHttpCode < 200  ||  ctx.nHttpCode >= 300)
         {
            m_pSneeze->Log (CORE::ISNEEZE::kLOGLEVEL_Warning, "CACHE", "Fetch failed for " + sUrl + " (HTTP " + std::to_string (ctx.nHttpCode) + ")");
            bOk = false;
         }
      }
      else
      {
         std::fclose (ctx.pFile);
         ctx.pFile = nullptr;
         bOk = false;
      }
   }

   // Stage 3: Hash verification
   if (bOk  &&  !sHash.empty ())
   {
      std::string sAlgo, sExpected;
      if (ParseSriHash (sHash, sAlgo, sExpected))
      {
         std::string sActual = ComputeFileHash (sTmpPath, sAlgo);
         if (sActual != sExpected)
         {
            m_pSneeze->Log (CORE::ISNEEZE::kLOGLEVEL_Warning, "CACHE", "Hash mismatch for " + sUrl + " (expected " + sExpected + ", got " + sActual + ")");
            bOk = false;
         }
      }
   }

   // Stage 4: Rename .temp to .data
   if (bOk)
   {
      sFinalPath = DiskKeyToPath (sDiskKey, DISKFILE_DATA);

      std::error_code ec;
      std::filesystem::rename (sTmpPath, sFinalPath, ec);
      if (ec)
      {
         m_pSneeze->Log (CORE::ISNEEZE::kLOGLEVEL_Warning, "CACHE", "Failed to rename " + sTmpPath + " -> " + sFinalPath + ": " + ec.message ());
         bOk = false;
      }
      else
      {
         bHaveTmp = false;

         std::error_code ecSize;
         auto nFsSize = std::filesystem::file_size (sFinalPath, ecSize);
         if (!ecSize)
            nSizeBytes = static_cast<uint64_t> (nFsSize);
      }
   }

   // Cleanup temp file on failure
   if (bHaveTmp)
   {
      std::error_code ec;
      std::filesystem::remove (sTmpPath, ec);
   }

   // Notify
   {
      std::lock_guard<std::recursive_mutex> guard (m_mutex);

      pEntry->SetHttpStatus (ctx.nHttpCode);
      pEntry->SetFetchEndTime (SecondsSinceEpoch ());

      if (bOk)
      {
         pEntry->SetHeaders (ctx.mapHeaders);
         pEntry->Complete (sFinalPath, nSizeBytes);
         NotifyFiles (pEntry->CollectFiles (), STATE_READY);
      }
      else
      {
         if (!ctx.mapHeaders.empty ())
            pEntry->SetHeaders (ctx.mapHeaders);
         pEntry->Fail ();
         NotifyFiles (pEntry->CollectFiles (), STATE_FAILED);
      }
   }

   if (bOk)
   {
      m_pSneeze->Log (CORE::ISNEEZE::kLOGLEVEL_Trace, "CACHE", "Cached " + sUrl + " (" + std::to_string (nSizeBytes) + " bytes)");
   }

   DispatchNextFromQueue ();
}

// ---------------------------------------------------------------------------
// STORE lookup
// ---------------------------------------------------------------------------

STORE* MANAGER::FindOrCreateStore (const std::string& sName)
{
   STORE* pResult = nullptr;

   auto it = m_mapStores.find (sName);
   if (it != m_mapStores.end ())
   {
      pResult = it->second.get ();
   }
   else
   {
      auto pStore = std::make_unique<STORE> ();
      pStore->sName = sName;
      pResult = pStore.get ();
      m_mapStores[sName] = std::move (pStore);
   }

   return pResult;
}

// ---------------------------------------------------------------------------
// Notification helpers (called under m_mutex — recursive lock allows re-entry)
// ---------------------------------------------------------------------------

void MANAGER::NotifyFiles (const std::vector<FILE*>& apFiles, STATE bState)
{
   for (auto* pFile : apFiles)
   {
      pFile->SnapshotEntry ();

      IFILE* pListener = pFile->GetListener ();
      if (pListener)
      {
         if (bState == STATE_READY)
            pListener->OnFileReady (pFile);
         else
            pListener->OnFileFailed (pFile);
      }

      if (!pFile->IsPendingClear ())
         m_pSneeze->NotifyCacheFileChanged (pFile);
   }
}

void MANAGER::DispatchNextFromQueue ()
{
   std::lock_guard<std::recursive_mutex> guard (m_mutex);
   
   SweepCompletedThreads ();

   if (!m_aFetchQueue.empty ()  &&  static_cast<int> (m_apFetchSlots.size ()) < kMAX_CONCURRENT_FETCHES)
   {
      ENTRY* pNext = m_aFetchQueue.front ();
      m_aFetchQueue.pop ();

      FETCH_SLOT* pSlot = new FETCH_SLOT ();
      pSlot->thread = std::thread ([this, pNext, pSlot] ()
      {
         FetchEntry (pNext);
         pSlot->bDone.store (true);
      });
      m_apFetchSlots.push_back (pSlot);
   }
}

}} // namespace SNEEZE::CACHE
