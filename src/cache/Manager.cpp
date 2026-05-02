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
   m_pSneeze       (pSneeze),
   m_bShuttingDown (false),
   m_bCacheEnabled   (true),
   m_bDisplayEnabled (true),
   m_nNextSequence (1),
   m_tpEpoch       (std::chrono::steady_clock::now ())
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

      LoadManifest ();

      m_pSneeze->Log (CORE::ISNEEZE::kLOGLEVEL_Info, "CACHE", "Initialized (path: " + m_sCachePath + ", entries: " + std::to_string (m_mapEntries.size ()) + ")");

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

      SaveManifest ();

      {
         std::lock_guard<std::recursive_mutex> guard (m_mutex);
         
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
   if (!pEntry->GetDiskPath ().empty ())
   {
      std::error_code ec;
      std::filesystem::remove (pEntry->GetDiskPath (), ec);
   }

   pEntry->ResetState ();
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

FILE* MANAGER::Request (IFILE* pListener, const std::string& sStore, const std::string& sUrl, const std::string& sHash, uint32_t bFlags)
{
   bool bCreate = (bFlags & REQUEST_CREATE) != 0;

   FILE* pFile = nullptr;

   {
      std::lock_guard<std::recursive_mutex> guard (m_mutex);

      STORE* pStore = FindOrCreateStore (sStore);

      // Resolve entry: find existing or create new
      auto it = m_mapEntries.find (sUrl);
      if (it == m_mapEntries.end ()  &&  bCreate)
      {
         auto pEntryPtr = std::make_unique<ENTRY> (this, sUrl, sHash);
         m_mapEntries[sUrl] = std::move (pEntryPtr);
         it = m_mapEntries.find (sUrl);
      }

      if (it != m_mapEntries.end ())
      {
         ENTRY* pEntry = it->second.get ();

         // Create the FILE — once
         pFile = new FILE (this, pEntry, pStore, pListener, m_nNextSequence++);
         pEntry->AttachFile (pFile);
         m_apFile.push_back (pFile);

         // Decide what the entry needs
         STATE bState      = pEntry->GetState ();
         bool  bNeedsFetch = false;

         if (bState == STATE_READY  &&  !sHash.empty ()  &&  !pEntry->IsHashed ())
         {
            // Upgrade unhashed -> hashed
            std::string sAlgo, sDigest;
            if (ParseSriHash (sHash, sAlgo, sDigest))
            {
               std::string sComputed = ComputeFileHash (pEntry->GetDiskPath (), sAlgo);
               if (sComputed == sDigest)
               {
                  pEntry->SetHash (sHash);
                  pEntry->TouchAccess ();
                  pEntry->SetServedFromCache (true);
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
            // Hash changed — re-fetch
            pEntry->SetHash (sHash);
            bNeedsFetch = true;
         }
         else if (bState == STATE_READY  &&  !m_bCacheEnabled)
         {
            // Cache bypass
            pEntry->SetServedFromCache (false);
            bNeedsFetch = true;
         }
         else if (bState == STATE_READY)
         {
            pEntry->TouchAccess ();
            pEntry->SetServedFromCache (true);
            if (pListener)
               pListener->OnFileReady (pFile);
         }
         else if (bState == STATE_FAILED)
         {
            if (pListener)
               pListener->OnFileFailed (pFile);
         }
         else if (bState == STATE_IDLE)
         {
            if (!sHash.empty ())
               pEntry->SetHash (sHash);
            bNeedsFetch = true;
         }

         if (bNeedsFetch)
         {
            pEntry->SetFetching ();
            pEntry->SetFetchStartTime (SecondsSinceEpoch ());
            DispatchFetch (pEntry);
         }

         // Display toggle: auto-clear when display is off
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
      pEntry->DetachFile (pFile);

      pFile->SetReleased ();

      if (pFile->IsPendingClear ())
      {
         auto it = std::find (m_apFile.begin (), m_apFile.end (), pFile);
         if (it != m_apFile.end ())
            m_apFile.erase (it);
         delete pFile;
      }

      if (pEntry->IsPendingReset ()  &&  pEntry->GetFileCount () == 0)
         ResetEntry (pEntry);
   }
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
   if (pFile)
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

   SaveManifest ();
}

void MANAGER::Enumerate (IENUM* pEnum)
{
   std::lock_guard<std::recursive_mutex> guard (m_mutex);

   FILE* pFile = new FILE (this, nullptr, nullptr, nullptr, 0);
   pFile->SetEnumeration (true);

   for (auto& [sUrl, pEntryPtr] : m_mapEntries)
   {
      ENTRY* pEntry = pEntryPtr.get ();
      pFile->SetEntry (pEntry);
      pEntry->AttachFile (pFile);

      pEnum->OnEntry (pFile);

      pEntry->DetachFile (pFile);
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
   unsigned char aDigest[SHA256_DIGEST_LENGTH];
   SHA256 (reinterpret_cast<const unsigned char*> (sUrl.data ()),
      sUrl.size (), aDigest);

   char szHex[SHA256_DIGEST_LENGTH * 2 + 1];
   for (int i = 0; i < SHA256_DIGEST_LENGTH; i++)
      std::sprintf (szHex + i * 2, "%02x", aDigest[i]);
   szHex[SHA256_DIGEST_LENGTH * 2] = '\0';

   return std::string (szHex);
}

std::string MANAGER::DiskKeyToPath (const std::string& sDiskKey) const
{
   std::string sDir = sDiskKey.substr (0, 2);
   std::string sFile = sDiskKey.substr (2);
   std::filesystem::path pPath = std::filesystem::path (m_sCachePath) / sDir / sFile;
   return pPath.string ();
}

std::string MANAGER::DiskKeyToTmpPath (const std::string& sDiskKey) const
{
   std::filesystem::path pPath = std::filesystem::path (m_sCachePath) / (sDiskKey + ".tmp");
   return pPath.string ();
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
// Manifest (persistent entries)
// ---------------------------------------------------------------------------

void MANAGER::LoadManifest ()
{
   std::string sManifestPath = (std::filesystem::path (m_sCachePath) / "manifest.json").string ();
   std::ifstream file (sManifestPath);
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
         m_pSneeze->Log (CORE::ISNEEZE::kLOGLEVEL_Warning, "CACHE", "Failed to parse manifest.json — starting fresh");
      }

      if (bParsed  &&  jDoc.contains ("version")  &&  jDoc.contains ("entries"))
      {
         for (auto& [sUrl, jEntry] : jDoc["entries"].items ())
         {
            std::string sHash     = jEntry.value ("hash", "");
            std::string sDiskPath = jEntry.value ("diskPath", "");

            if (!sDiskPath.empty ()  &&  std::filesystem::exists (sDiskPath))
            {
               auto pEntry = std::make_unique<ENTRY> (this, sUrl, sHash);
               pEntry->SetDiskPath (sDiskPath);
               pEntry->SetSizeBytes (jEntry.value ("sizeBytes", static_cast<uint64_t> (0)));
               pEntry->SetCreatedTime (jEntry.value ("createdAt", ""));

               if (jEntry.contains ("headers"))
               {
                  std::unordered_map<std::string, std::string> mapHeaders;
                  for (auto& [sKey, sVal] : jEntry["headers"].items ())
                     mapHeaders[sKey] = sVal.get<std::string> ();
                  pEntry->SetHeaders (mapHeaders);
               }

               pEntry->SetServedFromCache (true);
               pEntry->Complete (sDiskPath, pEntry->GetSizeBytes ());

               m_mapEntries[sUrl] = std::move (pEntry);
            }
         }
      }
   }
}

void MANAGER::SaveManifest ()
{
   if (!m_sCachePath.empty ())
   {
      std::lock_guard<std::recursive_mutex> guard (m_mutex);

      nlohmann::json jDoc;
      jDoc["version"] = 1;
      nlohmann::json jEntries = nlohmann::json::object ();

      for (auto& [sUrl, pEntry] : m_mapEntries)
      {
         if (pEntry->GetState () == STATE_READY)
         {
            nlohmann::json jEntry;
            jEntry["hash"]           = pEntry->GetHash ();
            jEntry["diskPath"]       = pEntry->GetDiskPath ();
            jEntry["sizeBytes"]      = pEntry->GetSizeBytes ();
            jEntry["createdAt"]      = pEntry->GetCreatedTime ();
            jEntry["lastAccessedAt"] = pEntry->GetLastAccessTime ();
            jEntry["accessCount"]    = pEntry->GetAccessCount ();

            nlohmann::json jHeaders = nlohmann::json::object ();
            for (auto& [sKey, sVal] : pEntry->GetHeaders ())
               jHeaders[sKey] = sVal;
            jEntry["headers"] = jHeaders;

            jEntries[sUrl] = jEntry;
         }
      }

      jDoc["entries"] = jEntries;

      std::string sManifestPath = (std::filesystem::path (m_sCachePath) / "manifest.json"    ).string ();
      std::string sTmpPath      = (std::filesystem::path (m_sCachePath) / "manifest.json.tmp").string ();

      std::ofstream file (sTmpPath, std::ios::trunc);
      if (file.is_open ())
      {
         file << jDoc.dump (2);
         file.close ();

         std::error_code ec;
         std::filesystem::rename (sTmpPath, sManifestPath, ec);
         if (ec)
         {
            m_pSneeze->Log (CORE::ISNEEZE::kLOGLEVEL_Warning, "CACHE", "Failed to rename manifest temp file: " + ec.message ());
         }
      }
   }
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
   }
   std::string sDiskKey  = ComputeDiskKey (sUrl);
   std::string sTmpPath  = DiskKeyToTmpPath (sDiskKey);
   std::string sFinalPath;
   uint64_t    nSizeBytes = 0;
   bool        bOk        = !m_bShuttingDown.load ();
   bool        bHaveTmp   = false;
   CURL_FETCH_CONTEXT ctx = {};
   ctx.nHttpCode = 0;

   // Stage 1: Open temp file
   if (bOk)
   {
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

   // Stage 4: Rename temp to final path
   if (bOk)
   {
      sFinalPath = DiskKeyToPath (sDiskKey);
      std::filesystem::create_directories (std::filesystem::path (sFinalPath).parent_path ());

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
