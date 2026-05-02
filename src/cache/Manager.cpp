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

   m_tpEpoch      = std::chrono::steady_clock::now ();
   m_sCachePath   = GetPersistentCachePath ();
   m_sSessionPath = GetSessionCachePath ();

   if (!m_sCachePath.empty ())
   {
      std::filesystem::create_directories (m_sCachePath);
      std::filesystem::create_directories (m_sSessionPath);

      LoadManifest ();

      m_pSneeze->Log (CORE::ISNEEZE::kLOGLEVEL_Info, "CACHE",
         "Initialized (path: " + m_sCachePath + ", entries: " +
         std::to_string (m_mapEntries.size ()) + ")");
      bResult = true;
   }
   else
   {
      m_pSneeze->Log (CORE::ISNEEZE::kLOGLEVEL_Error, "CACHE",
         "Failed to determine cache path");
   }

   return bResult;
}

void MANAGER::Shutdown ()
{
   if (m_sCachePath.empty ())
      return;

   m_bShuttingDown.store (true);

   // Join all fetch threads
   for (auto* pSlot : m_apFetchSlots)
   {
      if (pSlot->thread.joinable ())
         pSlot->thread.join ();
      delete pSlot;
   }
   m_apFetchSlots.clear ();

   // Drain the fetch queue
   while (!m_aFetchQueue.empty ())
      m_aFetchQueue.pop ();

   SaveManifest ();

   {
      std::lock_guard<std::mutex> guard (m_mutex);
      m_mapEntries.clear ();
   }

   DeleteHistory ();

   m_sCachePath.clear ();
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
// History management
// ---------------------------------------------------------------------------

void MANAGER::DeleteHistory ()
{
   for (auto* pFile : m_apHistory)
      delete pFile;
   m_apHistory.clear ();
}

void MANAGER::NullifyHistoryEntries (ENTRY* pEntry)
{
   for (auto* pFile : m_apHistory)
   {
      if (pFile->GetEntry () == pEntry)
         pFile->NullEntry ();
   }
}

void MANAGER::DestroyEntry (ENTRY* pEntry)
{
   NullifyHistoryEntries (pEntry);

   if (!pEntry->GetDiskPath ().empty ())
   {
      std::error_code ec;
      std::filesystem::remove (pEntry->GetDiskPath (), ec);
   }

   std::string sUrl = pEntry->GetUrl ();
   m_mapEntries.erase (sUrl);
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

FILE* MANAGER::Request (const std::string& sUrl, IFILE* pListener)
{
   return Request (sUrl, std::string (), pListener, kREQUEST_DEFAULT);
}

FILE* MANAGER::Request (const std::string& sUrl, const std::string& sHash, IFILE* pListener,
                        uint32_t bFlags)
{
   bool bCreate = (bFlags & REQUEST_CREATE) != 0;
   bool bFetch  = (bFlags & REQUEST_FETCH)  != 0;

   FILE* pFile = nullptr;
   bool bNotifyReady  = false;
   bool bNotifyFailed = false;

   {
      std::lock_guard<std::mutex> guard (m_mutex);

      auto it = m_mapEntries.find (sUrl);
      if (it != m_mapEntries.end ())
      {
         ENTRY* pEntry = it->second.get ();

         // Upgrade unhashed -> hashed if hash now provided
         if (bFetch  &&  pEntry->GetState () == STATE_READY
             &&  !sHash.empty ()  &&  !pEntry->IsHashed ())
         {
            std::string sAlgo, sDigest;
            if (ParseSriHash (sHash, sAlgo, sDigest))
            {
               std::string sComputed = ComputeFileHash (pEntry->GetDiskPath (), sAlgo);
               if (sComputed == sDigest)
               {
                  pEntry->SetHash (sHash);

                  std::string sDiskKey = ComputeDiskKey (sUrl);
                  std::string sPersistPath = DiskKeyToPath (sDiskKey, true);
                  std::filesystem::create_directories (
                     std::filesystem::path (sPersistPath).parent_path ());

                  std::error_code ec;
                  std::filesystem::rename (pEntry->GetDiskPath (), sPersistPath, ec);
                  if (!ec)
                     pEntry->SetDiskPath (sPersistPath);

                  pEntry->TouchAccess ();
                  pEntry->SetServedFromCache (true);

                  pFile = new FILE (this, pEntry, pListener);
                  pFile->SetSequence (m_nNextSequence++);
                  pEntry->AttachFile (pFile);
                  m_apHistory.push_back (pFile);
               }
               else
               {
                  pEntry->SetHash (sHash);
                  pEntry->SetFetching ();
                  pEntry->SetFetchStartTime (SecondsSinceEpoch ());

                  pFile = new FILE (this, pEntry, pListener);
                  pFile->SetSequence (m_nNextSequence++);
                  pEntry->AttachFile (pFile);
                  m_apHistory.push_back (pFile);

                  DispatchFetch (pEntry);
               }
            }
         }

         if (!pFile  &&  bFetch)
         {
            // Hash changed — re-fetch
            if (!sHash.empty ()  &&  pEntry->IsHashed ()  &&  pEntry->GetHash () != sHash
                &&  pEntry->GetState () != STATE_FETCHING)
            {
               pEntry->SetHash (sHash);
               pEntry->SetFetching ();
               pEntry->SetFetchStartTime (SecondsSinceEpoch ());

               pFile = new FILE (this, pEntry, pListener);
               pFile->SetSequence (m_nNextSequence++);
               pEntry->AttachFile (pFile);
               m_apHistory.push_back (pFile);

               DispatchFetch (pEntry);
            }
         }

         if (!pFile)
         {
            pEntry->TouchAccess ();

            bool bServed = (pEntry->GetState () == STATE_READY);
            if (bServed)
               pEntry->SetServedFromCache (true);

            pFile = new FILE (this, pEntry, pListener);
            pFile->SetSequence (m_nNextSequence++);
            pEntry->AttachFile (pFile);
            m_apHistory.push_back (pFile);

            if (pEntry->GetState () == STATE_READY)
               bNotifyReady = true;
            else if (pEntry->GetState () == STATE_FAILED)
               bNotifyFailed = true;
         }
      }
      else
      {
         if (!bCreate)
            return nullptr;

         // New entry
         auto pEntryPtr = std::make_unique<ENTRY> (this, sUrl, sHash);
         ENTRY* pRaw = pEntryPtr.get ();
         m_mapEntries[sUrl] = std::move (pEntryPtr);

         pFile = new FILE (this, pRaw, pListener);
         pFile->SetSequence (m_nNextSequence++);
         pRaw->AttachFile (pFile);
         m_apHistory.push_back (pFile);

         if (!pRaw->GetDiskPath ().empty ()  &&  std::filesystem::exists (pRaw->GetDiskPath ()))
         {
            pRaw->TouchAccess ();
            pRaw->SetServedFromCache (true);
            pRaw->Complete (pRaw->GetDiskPath (), pRaw->GetSizeBytes ());
            bNotifyReady = true;
         }
         else if (bFetch)
         {
            pRaw->SetFetching ();
            pRaw->SetFetchStartTime (SecondsSinceEpoch ());
            DispatchFetch (pRaw);
         }
      }
   }

   // All notifications dispatched outside m_mutex to prevent deadlock
   if (bNotifyReady  &&  pListener)
      pListener->OnFileReady (pFile);
   else if (bNotifyFailed  &&  pListener)
      pListener->OnFileFailed (pFile);

   if (pFile)
      m_pSneeze->NotifyCacheFileCreated (pFile);
   return pFile;
}

void MANAGER::Release (FILE* pFile)
{
   if (!pFile)
      return;

   bool bSaveManifest = false;
   FILE* pToDelete    = nullptr;

   {
      std::lock_guard<std::mutex> guard (m_mutex);

      ENTRY* pEntry = pFile->GetEntry ();
      if (pEntry)
         pEntry->DetachFile (pFile);

      pFile->SetReleased ();

      if (pFile->IsPendingClear ())
      {
         auto it = std::find (m_apHistory.begin (), m_apHistory.end (), pFile);
         if (it != m_apHistory.end ())
            m_apHistory.erase (it);
         pToDelete = pFile;
      }

      if (pEntry  &&  pEntry->IsPendingReset ()  &&  pEntry->GetFileCount () == 0)
      {
         bSaveManifest = pEntry->IsHashed ();
         DestroyEntry (pEntry);
      }
   }

   if (bSaveManifest)
      SaveManifest ();

   if (pToDelete)
   {
      m_pSneeze->NotifyCacheFileDeleted (pToDelete);
      delete pToDelete;
   }
}

void MANAGER::Clear (FILE* pFile)
{
   if (!pFile)
      return;

   pFile->Clear ();
}

void MANAGER::Reset (FILE* pFile, bool b)
{
   if (!pFile)
      return;

   std::lock_guard<std::mutex> guard (m_mutex);
   ENTRY* pEntry = pFile->GetEntry ();
   if (pEntry)
      pEntry->SetPendingReset (b);
}

// ---------------------------------------------------------------------------
// Cache management
// ---------------------------------------------------------------------------

void MANAGER::ClearSession ()
{
   std::vector<FILE*> apToDelete;

   {
      std::lock_guard<std::mutex> guard (m_mutex);

      auto it = m_apHistory.begin ();
      while (it != m_apHistory.end ())
      {
         FILE* pFile   = *it;
         ENTRY* pEntry = pFile->GetEntry ();

         if (pEntry  &&  !pEntry->IsHashed ())
         {
            if (pFile->IsReleased ())
            {
               apToDelete.push_back (pFile);
               it = m_apHistory.erase (it);
            }
            else
            {
               pFile->Clear ();
               ++it;
            }
         }
         else if (!pEntry  &&  pFile->IsReleased ())
         {
            apToDelete.push_back (pFile);
            it = m_apHistory.erase (it);
         }
         else
         {
            ++it;
         }
      }
   }

   for (auto* p : apToDelete)
   {
      m_pSneeze->NotifyCacheFileDeleted (p);
      delete p;
   }
}

void MANAGER::ClearAll ()
{
   std::vector<FILE*> apToDelete;

   {
      std::lock_guard<std::mutex> guard (m_mutex);

      auto it = m_apHistory.begin ();
      while (it != m_apHistory.end ())
      {
         FILE* pFile = *it;
         if (pFile->IsReleased ())
         {
            apToDelete.push_back (pFile);
            it = m_apHistory.erase (it);
         }
         else
         {
            pFile->Clear ();
            ++it;
         }
      }
   }

   for (auto* p : apToDelete)
   {
      m_pSneeze->NotifyCacheFileDeleted (p);
      delete p;
   }
}

void MANAGER::ResetSession ()
{
   bool bSaveManifest = false;

   {
      std::lock_guard<std::mutex> guard (m_mutex);

      auto it = m_mapEntries.begin ();
      while (it != m_mapEntries.end ())
      {
         ENTRY* pEntry = it->second.get ();
         if (!pEntry->IsHashed ())
         {
            pEntry->SetPendingReset (true);
            if (pEntry->GetFileCount () == 0)
            {
               NullifyHistoryEntries (pEntry);
               if (!pEntry->GetDiskPath ().empty ())
               {
                  std::error_code ec;
                  std::filesystem::remove (pEntry->GetDiskPath (), ec);
               }
               it = m_mapEntries.erase (it);
            }
            else
            {
               ++it;
            }
         }
         else
         {
            ++it;
         }
      }
   }

   std::error_code ec;
   std::filesystem::remove_all (m_sSessionPath, ec);
   std::filesystem::create_directories (m_sSessionPath);
}

void MANAGER::ResetAll ()
{
   {
      std::lock_guard<std::mutex> guard (m_mutex);

      auto it = m_mapEntries.begin ();
      while (it != m_mapEntries.end ())
      {
         ENTRY* pEntry = it->second.get ();
         pEntry->SetPendingReset (true);
         if (pEntry->GetFileCount () == 0)
         {
            NullifyHistoryEntries (pEntry);
            if (!pEntry->GetDiskPath ().empty ())
            {
               std::error_code ec;
               std::filesystem::remove (pEntry->GetDiskPath (), ec);
            }
            it = m_mapEntries.erase (it);
         }
         else
         {
            ++it;
         }
      }
   }

   SaveManifest ();

   std::error_code ec;
   std::filesystem::remove_all (m_sCachePath, ec);
   std::filesystem::create_directories (m_sCachePath);
   std::filesystem::create_directories (m_sSessionPath);
}

// ---------------------------------------------------------------------------
// Path helpers
// ---------------------------------------------------------------------------

std::string MANAGER::GetPersistentCachePath () const
{
   std::string sAppData = m_pSneeze->GetHost ()->sAppDataPath;
   if (sAppData.empty ())
      return "";

   std::filesystem::path pPath = std::filesystem::path (sAppData) / "Cache";
   return pPath.string ();
}

std::string MANAGER::GetSessionCachePath () const
{
   std::filesystem::path pPath = std::filesystem::path (m_sCachePath) / "tmp";
   return pPath.string ();
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

std::string MANAGER::DiskKeyToPath (const std::string& sDiskKey, bool bPersistent) const
{
   std::string sDir = sDiskKey.substr (0, 2);
   std::string sFile = sDiskKey.substr (2);
   std::string sBase = bPersistent ? m_sCachePath : m_sSessionPath;
   std::filesystem::path pPath = std::filesystem::path (sBase) / sDir / sFile;
   return pPath.string ();
}

std::string MANAGER::DiskKeyToTmpPath (const std::string& sDiskKey) const
{
   std::filesystem::path pPath = std::filesystem::path (m_sSessionPath) / (sDiskKey + ".tmp");
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
   if (!pMd)
      return sResult;

   std::ifstream file (sFilePath, std::ios::binary);
   if (!file.is_open ())
      return sResult;

   EVP_MD_CTX* pCtx = EVP_MD_CTX_new ();
   if (!pCtx)
      return sResult;

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
   return sResult;
}

std::string MANAGER::ComputeDataHash (const uint8_t* pData, size_t nLen, const std::string& sAlgo) const
{
   std::string sResult;

   const EVP_MD* pMd = GetEvpMd (sAlgo);
   if (!pMd  ||  !pData)
      return sResult;

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
   return sResult;
}

// ---------------------------------------------------------------------------
// Manifest (persistent entries)
// ---------------------------------------------------------------------------

void MANAGER::LoadManifest ()
{
   std::string sManifestPath = (std::filesystem::path (m_sCachePath) / "manifest.json").string ();
   std::ifstream file (sManifestPath);
   if (!file.is_open ())
      return;

   nlohmann::json jDoc;
   try
   {
      file >> jDoc;
   }
   catch (...)
   {
      m_pSneeze->Log (CORE::ISNEEZE::kLOGLEVEL_Warning, "CACHE",
         "Failed to parse manifest.json — starting fresh");
      return;
   }

   if (!jDoc.contains ("version")  ||  !jDoc.contains ("entries"))
      return;

   for (auto& [sUrl, jEntry] : jDoc["entries"].items ())
   {
      std::string sHash     = jEntry.value ("hash", "");
      std::string sDiskPath = jEntry.value ("diskPath", "");

      if (sHash.empty ()  ||  sDiskPath.empty ())
         continue;

      if (!std::filesystem::exists (sDiskPath))
         continue;

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

void MANAGER::SaveManifest ()
{
   if (m_sCachePath.empty ())
      return;

   // Build the JSON snapshot under lock
   nlohmann::json jDoc;
   jDoc["version"] = 1;
   nlohmann::json jEntries = nlohmann::json::object ();

   {
      std::lock_guard<std::mutex> guard (m_mutex);

      for (auto& [sUrl, pEntry] : m_mapEntries)
      {
         if (!pEntry->IsHashed ()  ||  pEntry->GetState () != STATE_READY)
            continue;

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

   // Write to disk outside the lock
   std::string sManifestPath = (std::filesystem::path (m_sCachePath) / "manifest.json").string ();
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
         m_pSneeze->Log (CORE::ISNEEZE::kLOGLEVEL_Warning, "CACHE",
            "Failed to rename manifest temp file: " + ec.message ());
      }
   }
}

// ---------------------------------------------------------------------------
// Background fetch
// ---------------------------------------------------------------------------

void MANAGER::FetchEntry (ENTRY* pEntry)
{
   if (m_bShuttingDown.load ())
   {
      std::vector<FILE*> apNotify;
      {
         std::lock_guard<std::mutex> guard (m_mutex);
         pEntry->SetFetchEndTime (SecondsSinceEpoch ());
         pEntry->Fail ();
         apNotify = pEntry->CollectFiles ();
      }
      NotifyFiles (apNotify, STATE_FAILED);
      return;
   }

   std::string sUrl  = pEntry->GetUrl ();
   std::string sHash = pEntry->GetHash ();
   bool bPersistent  = !sHash.empty ();

   std::string sDiskKey = ComputeDiskKey (sUrl);
   std::string sTmpPath = DiskKeyToTmpPath (sDiskKey);

   std::filesystem::create_directories (m_sSessionPath);

   std::FILE* pTmpFile = nullptr;
#ifdef _WIN32
   fopen_s (&pTmpFile, sTmpPath.c_str (), "wb");
#else
   pTmpFile = std::fopen (sTmpPath.c_str (), "wb");
#endif

   if (!pTmpFile)
   {
      m_pSneeze->Log (CORE::ISNEEZE::kLOGLEVEL_Error, "CACHE",
         "Failed to open temp file: " + sTmpPath);

      std::vector<FILE*> apNotify;
      {
         std::lock_guard<std::mutex> guard (m_mutex);
         pEntry->SetFetchEndTime (SecondsSinceEpoch ());
         pEntry->Fail ();
         apNotify = pEntry->CollectFiles ();
      }
      NotifyFiles (apNotify, STATE_FAILED);
      DispatchNextFromQueue ();
      return;
   }

   CURL_FETCH_CONTEXT ctx = {};
   ctx.pFile     = pTmpFile;
   ctx.nHttpCode = 0;

   CURL* pCurl = curl_easy_init ();
   if (!pCurl)
   {
      std::fclose (pTmpFile);

      std::vector<FILE*> apNotify;
      {
         std::lock_guard<std::mutex> guard (m_mutex);
         pEntry->SetFetchEndTime (SecondsSinceEpoch ());
         pEntry->Fail ();
         apNotify = pEntry->CollectFiles ();
      }
      NotifyFiles (apNotify, STATE_FAILED);
      DispatchNextFromQueue ();
      return;
   }

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
   std::fclose (pTmpFile);

   pEntry->SetHttpStatus (ctx.nHttpCode);

   if (m_bShuttingDown.load ())
   {
      std::error_code ec;
      std::filesystem::remove (sTmpPath, ec);

      std::vector<FILE*> apNotify;
      {
         std::lock_guard<std::mutex> guard (m_mutex);
         pEntry->SetFetchEndTime (SecondsSinceEpoch ());
         pEntry->Fail ();
         apNotify = pEntry->CollectFiles ();
      }
      NotifyFiles (apNotify, STATE_FAILED);
      DispatchNextFromQueue ();
      return;
   }

   if (nCode != CURLE_OK  ||  ctx.nHttpCode < 200  ||  ctx.nHttpCode >= 300)
   {
      m_pSneeze->Log (CORE::ISNEEZE::kLOGLEVEL_Warning, "CACHE",
         "Fetch failed for " + sUrl + " (HTTP " + std::to_string (ctx.nHttpCode) + ")");
      std::error_code ec;
      std::filesystem::remove (sTmpPath, ec);

      std::vector<FILE*> apNotify;
      {
         std::lock_guard<std::mutex> guard (m_mutex);
         pEntry->SetFetchEndTime (SecondsSinceEpoch ());
         pEntry->SetHeaders (ctx.mapHeaders);
         pEntry->Fail ();
         apNotify = pEntry->CollectFiles ();
      }
      NotifyFiles (apNotify, STATE_FAILED);
      DispatchNextFromQueue ();
      return;
   }

   if (bPersistent)
   {
      std::string sAlgo, sExpected;
      if (ParseSriHash (sHash, sAlgo, sExpected))
      {
         std::string sActual = ComputeFileHash (sTmpPath, sAlgo);
         if (sActual != sExpected)
         {
            m_pSneeze->Log (CORE::ISNEEZE::kLOGLEVEL_Warning, "CACHE",
               "Hash mismatch for " + sUrl + " (expected " + sExpected + ", got " + sActual + ")");
            std::error_code ec;
            std::filesystem::remove (sTmpPath, ec);

            std::vector<FILE*> apNotify;
            {
               std::lock_guard<std::mutex> guard (m_mutex);
               pEntry->SetFetchEndTime (SecondsSinceEpoch ());
               pEntry->SetHeaders (ctx.mapHeaders);
               pEntry->Fail ();
               apNotify = pEntry->CollectFiles ();
            }
            NotifyFiles (apNotify, STATE_FAILED);
            DispatchNextFromQueue ();
            return;
         }
      }
   }

   std::string sFinalPath = DiskKeyToPath (sDiskKey, bPersistent);
   std::filesystem::create_directories (
      std::filesystem::path (sFinalPath).parent_path ());

   std::error_code ec;
   std::filesystem::rename (sTmpPath, sFinalPath, ec);
   if (ec)
   {
      m_pSneeze->Log (CORE::ISNEEZE::kLOGLEVEL_Warning, "CACHE",
         "Failed to rename " + sTmpPath + " -> " + sFinalPath + ": " + ec.message ());
      std::filesystem::remove (sTmpPath, ec);

      std::vector<FILE*> apNotify;
      {
         std::lock_guard<std::mutex> guard (m_mutex);
         pEntry->SetFetchEndTime (SecondsSinceEpoch ());
         pEntry->Fail ();
         apNotify = pEntry->CollectFiles ();
      }
      NotifyFiles (apNotify, STATE_FAILED);
      DispatchNextFromQueue ();
      return;
   }

   uint64_t nSizeBytes = 0;
   {
      std::error_code ecSize;
      auto nFsSize = std::filesystem::file_size (sFinalPath, ecSize);
      if (!ecSize)
         nSizeBytes = static_cast<uint64_t> (nFsSize);
   }

   std::vector<FILE*> apNotify;
   {
      std::lock_guard<std::mutex> guard (m_mutex);
      pEntry->SetHeaders (ctx.mapHeaders);
      pEntry->SetFetchEndTime (SecondsSinceEpoch ());
      pEntry->Complete (sFinalPath, nSizeBytes);
      apNotify = pEntry->CollectFiles ();
   }

   NotifyFiles (apNotify, STATE_READY);

   m_pSneeze->Log (CORE::ISNEEZE::kLOGLEVEL_Trace, "CACHE",
      "Cached " + sUrl + " (" + std::to_string (nSizeBytes) + " bytes" +
      (bPersistent ? ", persistent" : ", session") + ")");

   if (bPersistent)
      SaveManifest ();

   DispatchNextFromQueue ();
}

// ---------------------------------------------------------------------------
// Notification helpers (called outside m_mutex to prevent deadlock)
// ---------------------------------------------------------------------------

void MANAGER::NotifyFiles (const std::vector<FILE*>& apFiles, STATE bState)
{
   for (auto* pFile : apFiles)
   {
      if (!pFile)
         continue;

      IFILE* pListener = pFile->GetListener ();
      if (pListener)
      {
         if (bState == STATE_READY)
            pListener->OnFileReady (pFile);
         else
            pListener->OnFileFailed (pFile);
      }

      m_pSneeze->NotifyCacheFileChanged (pFile);
   }
}

void MANAGER::DispatchNextFromQueue ()
{
   std::lock_guard<std::mutex> guard (m_mutex);
   SweepCompletedThreads ();

   if (!m_aFetchQueue.empty ()
       &&  static_cast<int> (m_apFetchSlots.size ()) < kMAX_CONCURRENT_FETCHES)
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
