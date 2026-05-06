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

#include "Network.h"
#include "Sneeze.h"

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

size_t SNEEZE::NETWORK::FetchWriteCallback (char* pData, size_t nSize, size_t nMembers, void* pUser)
{
   FETCH_CONTEXT* pCtx = static_cast<FETCH_CONTEXT*> (pUser);

   return std::fwrite (pData, nSize, nMembers, pCtx->pFile);
}

size_t SNEEZE::NETWORK::FetchHeaderCallback (char* pData, size_t nSize, size_t nMembers, void* pUser)
{
   FETCH_CONTEXT* pCtx = static_cast<FETCH_CONTEXT*> (pUser);
   size_t nTotal = nSize * nMembers;
   std::string sLine (pData, nTotal);

   auto nColon = sLine.find (':');
   if (nColon != std::string::npos)
   {
      std::string sKey   = sLine.substr (0, nColon);
      std::string sValue = sLine.substr (nColon + 1);

      auto nStart = sValue.find_first_not_of (" \t\r\n");
      auto nEnd   = sValue.find_last_not_of  (" \t\r\n");
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
// NETWORK
// ---------------------------------------------------------------------------

SNEEZE::NETWORK::NETWORK (SNEEZE* pSneeze) :
   m_pSneeze         (pSneeze),
   m_bShuttingDown   (false),
   m_bCacheEnabled   (true),
   m_bDisplayEnabled (true),
   m_nNextAssetIx    (1),
   m_nNextFileIx     (1),
   m_tpEpoch         (std::chrono::steady_clock::now ())
{
}

SNEEZE::NETWORK::~NETWORK ()
{
   Shutdown ();
}

bool SNEEZE::NETWORK::Initialize ()
{
   bool bResult = false;

   CURLcode nCurlInit = curl_global_init (CURL_GLOBAL_DEFAULT);
   if (nCurlInit != CURLE_OK)
   {
      m_pSneeze->Log (ISNEEZE::kLOGLEVEL_Error, "NETWORK",
         "curl_global_init failed (code " + std::to_string (static_cast<int> (nCurlInit)) + ")");
   }
   else
   {
      m_tpEpoch    = std::chrono::steady_clock::now ();
      m_sCachePath = GetCachePath ();

      if (!m_sCachePath.empty ())
      {
         std::filesystem::create_directories (m_sCachePath);

         LoadRules ();

         m_pSneeze->Log (ISNEEZE::kLOGLEVEL_Info, "NETWORK", "Initialized (path: " + m_sCachePath + ", rules: " + std::to_string (m_aRules.size ()) + ", nAssetIx: " + std::to_string (m_nNextAssetIx) + ")");

         bResult = true;
      }
      else
      {
         m_pSneeze->Log (ISNEEZE::kLOGLEVEL_Error, "NETWORK", "Failed to determine cache path");
      }
   }

   return bResult;
}

void SNEEZE::NETWORK::Shutdown ()
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

         for (auto& [sUrl, pAsset] : m_mapAssets)
         {
            if (pAsset->GetState () == STATE_READY)
               SaveMeta (pAsset.get ());
         }

         SaveRules ();

         m_mapAssets.clear ();
      }

      DeleteFiles ();

      m_sCachePath.clear ();
   }

   curl_global_cleanup ();
}

// ---------------------------------------------------------------------------
// Timing helpers
// ---------------------------------------------------------------------------

double SNEEZE::NETWORK::SecondsSinceEpoch () const
{
   auto tpNow = std::chrono::steady_clock::now ();

   return std::chrono::duration<double> (tpNow - m_tpEpoch).count ();
}

double SNEEZE::NETWORK::GetEpochAge () const
{
   return SecondsSinceEpoch ();
}

// ---------------------------------------------------------------------------
// File ownership
// ---------------------------------------------------------------------------

void SNEEZE::NETWORK::DeleteFiles ()
{
   for (auto* pFile : m_apFile)
      delete pFile;
   m_apFile.clear ();
}

void SNEEZE::NETWORK::ResetAsset (ASSET* pAsset)
{
   std::string sDiskKey = ComputeDiskKey (pAsset->GetUrl ());
   std::error_code ec;

   std::filesystem::remove (DiskKeyToPath (sDiskKey, DISKFILE_DATA), ec);
   std::filesystem::remove (DiskKeyToPath (sDiskKey, DISKFILE_META), ec);
   std::filesystem::remove (DiskKeyToPath (sDiskKey, DISKFILE_TEMP), ec);

   pAsset->ResetState ();
   pAsset->SetAssetIx (m_nNextAssetIx++);
}

// ---------------------------------------------------------------------------
// Fetch thread management (capped at kMAX_CONCURRENT_FETCHES)
// ---------------------------------------------------------------------------

void SNEEZE::NETWORK::SweepCompletedThreads ()
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

void SNEEZE::NETWORK::DispatchFetch (ASSET* pAsset)
{
   SweepCompletedThreads ();

   if (static_cast<int> (m_apFetchSlots.size ()) < kMAX_CONCURRENT_FETCHES)
   {
      FETCH_SLOT* pSlot = new FETCH_SLOT ();
      pSlot->thread = std::thread ([this, pAsset, pSlot] ()
      {
         FetchAsset (pAsset);
         pSlot->bDone.store (true);
      });
      m_apFetchSlots.push_back (pSlot);
   }
   else
   {
      m_aFetchQueue.push (pAsset);
   }
}

// ---------------------------------------------------------------------------
// Request / Release
// ---------------------------------------------------------------------------

SNEEZE::NETWORK::FILE* SNEEZE::NETWORK::Request (IFILE* pListener, SNEEZE::VIEWPORT* pViewport,
   std::shared_ptr<SNEEZE::VIEWPORT::CONTAINER::NAME> pName, const std::string& sUrl)
{
   return Request (pListener, pViewport, pName, sUrl, std::string (), kREQUEST_DEFAULT, 0);
}

SNEEZE::NETWORK::FILE* SNEEZE::NETWORK::Request (IFILE* pListener, SNEEZE::VIEWPORT* pViewport,
   std::shared_ptr<SNEEZE::VIEWPORT::CONTAINER::NAME> pName, const std::string& sUrl, const std::string& sHash, uint32_t bFlags, uint32_t nAssetIx)
{
   bool bCreate = (bFlags & REQUEST_CREATE) != 0;
   bool bFetch  = (bFlags & REQUEST_FETCH)  != 0;

   FILE* pFile = nullptr;

   {
      std::lock_guard<std::recursive_mutex> guard (m_mutex);

      auto it = m_mapAssets.find (sUrl);

      if (it == m_mapAssets.end ())
      {
         std::string sDiskKey = ComputeDiskKey (sUrl);
         if (LoadMeta (sDiskKey, sUrl))
            it = m_mapAssets.find (sUrl);
      }

      if (it == m_mapAssets.end ()  &&  bCreate  &&  bFetch)
      {
         auto pAssetPtr = std::make_unique<ASSET> (this, sUrl, sHash);
         pAssetPtr->SetAssetIx (m_nNextAssetIx++);
         m_mapAssets[sUrl] = std::move (pAssetPtr);
         it = m_mapAssets.find (sUrl);
      }

      if (it != m_mapAssets.end ())
      {
         ASSET* pAsset = it->second.get ();

         if (nAssetIx != 0  &&  pAsset->GetAssetIx () != nAssetIx)
            it = m_mapAssets.end ();
      }

      if (it != m_mapAssets.end ())
      {
         ASSET* pAsset = it->second.get ();

         if (pAsset->GetState () == STATE_READY  &&  IsAssetStale (pAsset))
         {
            if (!bFetch)
            {
               it = m_mapAssets.end ();
            }
            else
            {
               ResetAsset (pAsset);
            }
         }
      }

      if (it != m_mapAssets.end ())
      {
         ASSET* pAsset = it->second.get ();

         pFile = new FILE (this, pAsset, pName, pViewport, pListener, m_nNextFileIx++);
         pAsset->AttachFile (pFile);
         m_apFile.push_back (pFile);

         STATE bState       = pAsset->GetState ();
         bool  bNeedsFetch  = false;
         bool  bNotifyReady = false;
         bool  bNotifyFailed = false;

         if (bState == STATE_READY  &&  !sHash.empty ()  &&  !pAsset->IsHashed ())
         {
            std::string sAlgo, sDigest;
            if (ParseSriHash (sHash, sAlgo, sDigest))
            {
               std::string sComputed = ComputeFileHash (pAsset->GetDiskPath (), sAlgo);
               if (sComputed == sDigest)
               {
                  pAsset->SetHash (sHash);
                  pAsset->TouchAccess ();
                  pAsset->SetServedFromCache (true);
               }
               else
               {
                  pAsset->SetHash (sHash);
                  bNeedsFetch = true;
               }
            }
         }
         else if (bState != STATE_FETCHING  &&  !sHash.empty ()  &&  pAsset->IsHashed ()  &&  pAsset->GetHash () != sHash)
         {
            pAsset->SetHash (sHash);
            bNeedsFetch = true;
         }
         else if (bState == STATE_READY  &&  !m_bCacheEnabled)
         {
            pAsset->SetServedFromCache (false);
            bNeedsFetch = true;
         }
         else if (bState == STATE_READY)
         {
            pAsset->TouchAccess ();
            pAsset->SetServedFromCache (true);
            bNotifyReady = true;
         }
         else if (bState == STATE_FAILED)
         {
            bNotifyFailed = true;
         }
         else if (bState == STATE_IDLE)
         {
            if (!sHash.empty ())
               pAsset->SetHash (sHash);
            bNeedsFetch = true;
         }

         if (bNeedsFetch  &&  bFetch)
         {
            pAsset->SetFetching ();
            pAsset->SetFetchQueuedTime (SecondsSinceEpoch ());
            pFile->SnapshotProgress ();
            DispatchFetch (pAsset);
         }
         else if (bState != STATE_FETCHING)
         {
            pFile->SnapshotFinal ();
            if (bNeedsFetch)
               bNotifyFailed = true;
         }

         if (bNotifyReady  &&  pListener)
            pListener->OnFileReady (pFile);
         else if (bNotifyFailed  &&  pListener)
            pListener->OnFileFailed (pFile);

         if (!m_bDisplayEnabled)
            pFile->SetPendingClear (true);

         if (!pFile->IsPendingClear ())
            m_pSneeze->OnNetworkFileCreated (pFile);
      }
   }

   return pFile;
}

void SNEEZE::NETWORK::Release (FILE* pFile)
{
   if (pFile)
   {
      std::lock_guard<std::recursive_mutex> guard (m_mutex);

      ASSET* pAsset = pFile->GetAsset ();

      if (pAsset)
      {
         pFile->SnapshotFinal ();
         pAsset->DetachFile (pFile);
         pFile->SetAsset (nullptr);

         if (pAsset->GetFileCount () == 0)
         {
            if (pAsset->IsPendingReset ())
               ResetAsset (pAsset);
            else if (pAsset->GetState () == STATE_READY)
               SaveMeta (pAsset);

            std::string sUrl = pAsset->GetUrl ();
            m_mapAssets.erase (sUrl);
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

bool SNEEZE::NETWORK::ReopenFile (FILE* pFile)
{
   bool bResult = false;

   if (pFile  &&  !pFile->GetUrl ().empty ())
   {
      std::lock_guard<std::recursive_mutex> guard (m_mutex);

      std::string sUrl      = pFile->GetUrl ();
      uint32_t    nAssetIx  = pFile->GetAssetIx ();

      auto it = m_mapAssets.find (sUrl);

      if (it == m_mapAssets.end ())
      {
         std::string sDiskKey = ComputeDiskKey (sUrl);
         if (LoadMeta (sDiskKey, sUrl))
            it = m_mapAssets.find (sUrl);
      }

      if (it != m_mapAssets.end ())
      {
         ASSET* pAsset = it->second.get ();

         if (pAsset->GetAssetIx () == nAssetIx)
         {
            pFile->SetAsset (pAsset);
            pAsset->AttachFile (pFile);
            pFile->SnapshotFinal ();
            bResult = true;

            IFILE* pListener = pFile->GetListener ();
            if (pListener)
            {
               if (pAsset->GetState () == STATE_READY)
                  pListener->OnFileReady (pFile);
               else
                  pListener->OnFileFailed (pFile);
            }
         }
      }
   }

   return bResult;
}

void SNEEZE::NETWORK::Clear (FILE* pFile, bool b)
{
   if (pFile)
   {
      std::lock_guard<std::recursive_mutex> guard (m_mutex);

      if (pFile->SetPendingClear (b))
      {
         if (b)
         {
            m_pSneeze->OnNetworkFileDeleted (pFile);

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
            m_pSneeze->OnNetworkFileCreated (pFile);
         }
      }
   }
}

void SNEEZE::NETWORK::Reset (FILE* pFile, bool b)
{
   if (pFile  &&  pFile->GetAsset ())
   {
      std::lock_guard<std::recursive_mutex> guard (m_mutex);

      pFile->GetAsset ()->SetPendingReset (b);
   }
}

// ---------------------------------------------------------------------------
// Cache management
// ---------------------------------------------------------------------------

void SNEEZE::NETWORK::Clear ()
{
   std::lock_guard<std::recursive_mutex> guard (m_mutex);

   auto it = m_apFile.begin ();
   while (it != m_apFile.end ())
   {
      FILE* pFile = *it;

      if (pFile->SetPendingClear (true))
         m_pSneeze->OnNetworkFileDeleted (pFile);

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

void SNEEZE::NETWORK::Reset ()
{
   std::lock_guard<std::recursive_mutex> guard (m_mutex);

   for (auto& [sUrl, pAssetPtr] : m_mapAssets)
   {
      ASSET* pAsset = pAssetPtr.get ();
      pAsset->SetPendingReset (true);
      if (pAsset->GetFileCount () == 0)
         ResetAsset (pAsset);
   }

   std::error_code ec;
   std::filesystem::remove_all (m_sCachePath, ec);
   std::filesystem::create_directories (m_sCachePath);

   SaveRules ();
}

void SNEEZE::NETWORK::Enumerate (IENUM* pEnum)
{
   std::lock_guard<std::recursive_mutex> guard (m_mutex);

   FILE* pFile = new FILE (this, nullptr, nullptr, nullptr, nullptr, 0);
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

      auto it = m_mapAssets.find (sUrl);
      if (it != m_mapAssets.end ())
      {
         ASSET* pAsset = it->second.get ();
         pFile->SetAsset (pAsset);
         pFile->SnapshotFinal ();
         pAsset->AttachFile (pFile);

         pEnum->OnAsset (pFile);

         pAsset->DetachFile (pFile);
      }
      else
      {
         std::string sDataPath = pDirEntry.path ().string ();
         sDataPath = sDataPath.substr (0, sDataPath.size () - 5) + ".data";

         auto pAsset = std::make_unique<ASSET> (this, sUrl, sHash);
         pAsset->SetDiskPath (sDataPath);
         pAsset->SetSizeBytes (jMeta.value ("sizeBytes", static_cast<uint64_t> (0)));
         pAsset->SetCreatedTime (jMeta.value ("createdAt", ""));
         pAsset->SetAssetIx (jMeta.value ("nMetaIx", static_cast<uint32_t> (0)));
         pAsset->SetHttpStatus (jMeta.value ("httpStatus", static_cast<long> (0)));

         if (jMeta.contains ("headers"))
         {
            std::unordered_map<std::string, std::string> mapHeaders;
            for (auto& [sKey, sVal] : jMeta["headers"].items ())
               mapHeaders[sKey] = sVal.get<std::string> ();
            pAsset->SetHeaders (mapHeaders);
         }

         if (std::filesystem::exists (sDataPath))
            pAsset->Complete (sDataPath, pAsset->GetSizeBytes ());

         ASSET* pRaw = pAsset.get ();
         pFile->SetAsset (pRaw);
         pFile->SnapshotFinal ();
         pRaw->AttachFile (pFile);

         pEnum->OnAsset (pFile);

         pRaw->DetachFile (pFile);
      }
   }

   pFile->SetAsset (nullptr);
   delete pFile;
}

// ---------------------------------------------------------------------------
// Path helpers
// ---------------------------------------------------------------------------

std::string SNEEZE::NETWORK::GetCachePath () const
{
   std::string sResult;
   std::string sSession = m_pSneeze->Host ()->SessionPath ();
   if (!sSession.empty ())
      sResult = (std::filesystem::path (sSession) / "Cache").string ();

   return sResult;
}

std::string SNEEZE::NETWORK::ComputeDiskKey (const std::string& sUrl) const
{
   unsigned char aDigest[SHA_DIGEST_LENGTH];

   SHA1 (reinterpret_cast<const unsigned char*> (sUrl.data ()), sUrl.size (), aDigest);

   static const int kTRUNCATED_BYTES = 12;
   char szHex[kTRUNCATED_BYTES * 2 + 1];
   for (int i = 0; i < kTRUNCATED_BYTES; i++)
      std::sprintf (szHex + i * 2, "%02x", aDigest[i]);
   szHex[kTRUNCATED_BYTES * 2] = '\0';

   return std::string (szHex);
}

std::string SNEEZE::NETWORK::DiskKeyToPath (const std::string& sDiskKey, DISKFILE eType) const
{
   static const char* aExt[] = { ".data", ".temp", ".meta" };

   std::string sDir  = sDiskKey.substr (0, 2);
   std::string sFile = sDiskKey.substr (2);

   return (std::filesystem::path (m_sCachePath) / sDir / sFile).string () + aExt[eType];
}

// ---------------------------------------------------------------------------
// SRI hash parsing and verification
// ---------------------------------------------------------------------------

bool SNEEZE::NETWORK::ParseSriHash (const std::string& sSri, std::string& sAlgo, std::string& sDigest) const
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

std::string SNEEZE::NETWORK::ComputeFileHash (const std::string& sFilePath, const std::string& sAlgo) const
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

std::string SNEEZE::NETWORK::ComputeDataHash (const uint8_t* pData, size_t nLen, const std::string& sAlgo) const
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
// Sidecar metadata (per-asset .meta files)
// ---------------------------------------------------------------------------

void SNEEZE::NETWORK::SaveMeta (ASSET* pAsset)
{
   std::string sDiskKey  = ComputeDiskKey (pAsset->GetUrl ());
   std::string sMetaPath = DiskKeyToPath (sDiskKey, DISKFILE_META);

   std::filesystem::create_directories (std::filesystem::path (sMetaPath).parent_path ());

   nlohmann::json jMeta;
   jMeta["url"]            = pAsset->GetUrl ();
   jMeta["hash"]           = pAsset->GetHash ();
   jMeta["nMetaIx"]        = pAsset->GetAssetIx ();
   jMeta["sizeBytes"]      = pAsset->GetSizeBytes ();
   jMeta["createdAt"]      = pAsset->GetCreatedTime ();
   jMeta["lastAccessedAt"] = pAsset->GetLastAccessTime ();
   jMeta["accessCount"]    = pAsset->GetAccessCount ();
   jMeta["httpStatus"]     = pAsset->GetHttpStatus ();

   nlohmann::json jHeaders = nlohmann::json::object ();
   for (auto& [sKey, sVal] : pAsset->GetHeaders ())
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
         m_pSneeze->Log (ISNEEZE::kLOGLEVEL_Warning, "NETWORK", "Failed to rename meta temp: " + ec.message ());
   }
}

bool SNEEZE::NETWORK::LoadMeta (const std::string& sDiskKey, const std::string& sUrl)
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
         m_pSneeze->Log (ISNEEZE::kLOGLEVEL_Warning, "NETWORK", "Failed to parse sidecar: " + sMetaPath);
      }

      if (bParsed)
      {
         std::string sMetaUrl = jMeta.value ("url", "");
         if (sMetaUrl == sUrl  &&  std::filesystem::exists (sDataPath))
         {
            std::string sHash = jMeta.value ("hash", "");
            auto pAsset = std::make_unique<ASSET> (this, sUrl, sHash);
            pAsset->SetDiskPath (sDataPath);
            pAsset->SetSizeBytes (jMeta.value ("sizeBytes", static_cast<uint64_t> (0)));
            pAsset->SetCreatedTime (jMeta.value ("createdAt", ""));
            pAsset->SetAssetIx (jMeta.value ("nMetaIx", static_cast<uint32_t> (0)));

            if (jMeta.contains ("headers"))
            {
               std::unordered_map<std::string, std::string> mapHeaders;
               for (auto& [sKey, sVal] : jMeta["headers"].items ())
                  mapHeaders[sKey] = sVal.get<std::string> ();
               pAsset->SetHeaders (mapHeaders);
            }

            pAsset->SetHttpStatus (jMeta.value ("httpStatus", static_cast<long> (0)));
            pAsset->SetServedFromCache (true);
            pAsset->Complete (sDataPath, pAsset->GetSizeBytes ());

            m_mapAssets[sUrl] = std::move (pAsset);
            bResult = true;
         }
      }
   }

   return bResult;
}

// ---------------------------------------------------------------------------
// Staleness rules (persisted in rules.json)
// ---------------------------------------------------------------------------

void SNEEZE::NETWORK::LoadRules ()
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
         m_pSneeze->Log (ISNEEZE::kLOGLEVEL_Warning, "NETWORK", "Failed to parse rules.json -- defaulting to stale");
      }

      if (bParsed)
      {
         m_nNextAssetIx = jDoc.value ("nNextMetaIx", static_cast<uint32_t> (1));

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
      m_pSneeze->Log (ISNEEZE::kLOGLEVEL_Info, "NETWORK", "No rules.json -- creating fresh");
      SaveRules ();
   }
}

void SNEEZE::NETWORK::SaveRules ()
{
   if (!m_sCachePath.empty ())
   {
      nlohmann::json jDoc;
      jDoc["nNextMetaIx"] = m_nNextAssetIx;

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
}

void SNEEZE::NETWORK::AddRule (const std::string& sContentType, const std::string& sOlderThan)
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

bool SNEEZE::NETWORK::IsAssetStale (ASSET* pAsset) const
{
   bool bResult = false;

   std::string sContentType = pAsset->GetHeader ("content-type");
   std::string sCreatedAt   = pAsset->GetCreatedTime ();

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

void SNEEZE::NETWORK::FetchAsset (ASSET* pAsset)
{
   std::string sUrl;
   std::string sHash;
   {
      std::lock_guard<std::recursive_mutex> guard (m_mutex);

      sUrl  = pAsset->GetUrl ();
      sHash = pAsset->GetHash ();
      pAsset->SetFetchStartTime (SecondsSinceEpoch ());
   }
   std::string sDiskKey  = ComputeDiskKey (sUrl);
   std::string sTmpPath  = DiskKeyToPath (sDiskKey, DISKFILE_TEMP);
   std::string sFinalPath;
   uint64_t    nSizeBytes = 0;
   bool        bOk        = !m_bShuttingDown.load ();
   bool        bHaveTmp   = false;
   FETCH_CONTEXT ctx = {};
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
         m_pSneeze->Log (ISNEEZE::kLOGLEVEL_Error, "NETWORK", "Failed to open temp file: " + sTmpPath);
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
            m_pSneeze->Log (ISNEEZE::kLOGLEVEL_Warning, "NETWORK", "Fetch failed for " + sUrl + " (HTTP " + std::to_string (ctx.nHttpCode) + ")");
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
            m_pSneeze->Log (ISNEEZE::kLOGLEVEL_Warning, "NETWORK", "Hash mismatch for " + sUrl + " (expected " + sExpected + ", got " + sActual + ")");
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
         m_pSneeze->Log (ISNEEZE::kLOGLEVEL_Warning, "NETWORK", "Failed to rename " + sTmpPath + " -> " + sFinalPath + ": " + ec.message ());
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

      pAsset->SetHttpStatus (ctx.nHttpCode);
      pAsset->SetFetchEndTime (SecondsSinceEpoch ());

      if (bOk)
      {
         pAsset->SetHeaders (ctx.mapHeaders);
         pAsset->Complete (sFinalPath, nSizeBytes);
         NotifyFiles (pAsset->CollectFiles (), STATE_READY);
      }
      else
      {
         if (!ctx.mapHeaders.empty ())
            pAsset->SetHeaders (ctx.mapHeaders);
         pAsset->Fail ();
         NotifyFiles (pAsset->CollectFiles (), STATE_FAILED);
      }
   }

   if (bOk)
   {
      m_pSneeze->Log (ISNEEZE::kLOGLEVEL_Trace, "NETWORK", "Cached " + sUrl + " (" + std::to_string (nSizeBytes) + " bytes)");
   }

   DispatchNextFromQueue ();
}

// ---------------------------------------------------------------------------
// Notification helpers (called under m_mutex -- recursive lock allows re-entry)
// ---------------------------------------------------------------------------

void SNEEZE::NETWORK::NotifyFiles (const std::vector<SNEEZE::NETWORK::FILE*>& apFiles, SNEEZE::NETWORK::STATE bState)
{
   for (auto* pFile : apFiles)
   {
      pFile->SnapshotFinal ();

      IFILE* pListener = pFile->GetListener ();
      if (pListener)
      {
         if (bState == STATE_READY)
            pListener->OnFileReady (pFile);
         else
            pListener->OnFileFailed (pFile);
      }

      if (!pFile->IsPendingClear ())
         m_pSneeze->OnNetworkFileChanged (pFile);
   }
}

void SNEEZE::NETWORK::DispatchNextFromQueue ()
{
   std::lock_guard<std::recursive_mutex> guard (m_mutex);

   SweepCompletedThreads ();

   if (!m_aFetchQueue.empty ()  &&  static_cast<int> (m_apFetchSlots.size ()) < kMAX_CONCURRENT_FETCHES)
   {
      ASSET* pNext = m_aFetchQueue.front ();
      m_aFetchQueue.pop ();

      FETCH_SLOT* pSlot = new FETCH_SLOT ();
      pSlot->thread = std::thread ([this, pNext, pSlot] ()
      {
         FetchAsset (pNext);
         pSlot->bDone.store (true);
      });
      m_apFetchSlots.push_back (pSlot);
   }
}
