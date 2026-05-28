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

#include <openssl/evp.h>

using namespace SNEEZE;

// ---------------------------------------------------------------------------
// ASSET_FETCH -- local job class bridging the control-layer fetch pool
//                back to ASSET::FetchComplete.
// ---------------------------------------------------------------------------

class ASSET_FETCH : public JOB_FETCH
{
public:
   ASSET_FETCH (ASSET* pAsset, const std::string& sUrl, const std::string& sPath_Temp, const std::string& sPath_Data, const std::string& sHash)
      : JOB_FETCH (true, sUrl, sPath_Temp, sPath_Data, sHash),
        m_pAsset  (pAsset),
        m_pFile   (nullptr),
        m_bState  (NETWORK::STATE_FETCHING)
   {}

   ASSET_FETCH (ASSET* pAsset, NETWORK::FILE* pFile, NETWORK::STATE bState)
      : JOB_FETCH (false, "", "", "", ""),
        m_pAsset  (pAsset),
        m_pFile   (pFile),
        m_bState  (bState)
   {}

   void OnFetch_Complete (const FETCH_RESULT& Fetch_Result) override
   {
      if (IsFetch ())
         m_pAsset->FetchComplete (Fetch_Result);
      else m_pAsset->FetchComplete (m_pFile, m_bState);
   }

private:
   ASSET*        m_pAsset;
   NETWORK::FILE* m_pFile;
   NETWORK::STATE m_bState;
};

// ---------------------------------------------------------------------------

class ASSET::Impl
{
public:
   Impl (INETWORK_IMPL* pINetwork_Impl, const std::string& sUrl, const std::string& sPathname, uint32_t nAssetIx) :
      m_pINetwork_Impl (pINetwork_Impl),
      m_sUrl (sUrl),
      m_sPathname (sPathname),
      m_bState (NETWORK::STATE_IDLE),
      m_nSizeBytes (0),
      m_nAccessCount (0),
      m_nAssetIx (nAssetIx),
      m_nHttpStatus (0),
      m_dFetchQueuedTime (0.0),
      m_dFetchStartTime (0.0),
      m_dFetchEndTime (0.0),
      m_bServedFromCache (false),
      m_bReset (false),
      m_nCount_Open (0),
      m_nCount_Attach (0),
      m_pAsset_Fetch (nullptr)
   {
      m_sCreatedAt      = NowIso8601 ();
      m_sLastAccessedAt = m_sCreatedAt;
   }

   ~Impl ()
   {
   }

   void TouchAccess ()
   {
      m_sLastAccessedAt = NowIso8601 ();
      m_nAccessCount++;
   }

   // ---------------------------------------------------------------------------
   // Meta file helpers
   // ---------------------------------------------------------------------------

   void Meta_Load ()
   {
      std::string sMetaPath = Path (NETWORK::DISKFILE_META);
      std::string sDataPath = Path (NETWORK::DISKFILE_DATA);

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
            m_pINetwork_Impl->Log (IENGINE::kLOGLEVEL_Warning, "NETWORK", "Failed to parse sidecar: " + sMetaPath);
         }

         if (bParsed)
         {
            std::string sMetaUrl = jMeta.value ("url", "");
            if (sMetaUrl == m_sUrl  &&  std::filesystem::exists (sDataPath))
            {
               m_sHash              = jMeta.value ("hash", "");
               m_nAssetIx           = jMeta.value ("nMetaIx", static_cast<uint32_t> (0));
               // Data file confirmed on disk — state is READY
               m_nSizeBytes         = jMeta.value ("sizeBytes", static_cast<uint64_t> (0));
               m_sCreatedAt         = jMeta.value ("createdAt", "");
               m_nHttpStatus        = jMeta.value ("httpStatus", static_cast<long> (0));
               m_bReset             = jMeta.value ("reset", false);
               m_bServedFromCache   = true;
               m_bState             = NETWORK::STATE_READY;

               if (jMeta.contains ("headers"))
               {
                  for (auto& [sKey, sVal] : jMeta["headers"].items ())
                     m_mapHeaders[sKey] = sVal.get<std::string> ();
               }
            }
         }
      }
   }

   void Meta_Save ()
   {
      std::string sMetaPath = Path (NETWORK::DISKFILE_META);

      std::filesystem::create_directories (std::filesystem::path (sMetaPath).parent_path ());

      nlohmann::json jMeta;
      jMeta["url"]            = m_sUrl;
      jMeta["hash"]           = m_sHash;
      jMeta["nMetaIx"]        = m_nAssetIx;
      jMeta["sizeBytes"]      = m_nSizeBytes;
      jMeta["createdAt"]      = m_sCreatedAt;
      jMeta["lastAccessedAt"] = m_sLastAccessedAt;
      jMeta["accessCount"]    = m_nAccessCount;
      jMeta["httpStatus"]     = m_nHttpStatus;
      jMeta["reset"]          = m_bReset;

      nlohmann::json jHeaders = nlohmann::json::object ();
      for (auto& [sKey, sVal] : m_mapHeaders)
         jHeaders[sKey] = sVal;
      jMeta["headers"] = jHeaders;

      std::string sTmpPath = sMetaPath + ".temp";
      std::ofstream file (sTmpPath, std::ios::trunc);
      if (file.is_open ())
      {
         std::string sDump;
         bool bDumped = false;

         try
         {
            sDump = jMeta.dump (2);
            bDumped = true;
         }
         catch (const nlohmann::json::exception& ex)
         {
            m_pINetwork_Impl->Log (IENGINE::kLOGLEVEL_Warning, "NETWORK", "Meta_Save dump failed for " + m_sUrl + ": " + ex.what ());
            file.close ();
            std::error_code ec;
            std::filesystem::remove (sTmpPath, ec);
         }

         if (bDumped)
         {
            file << sDump;
            file.close ();

            std::error_code ec;
            std::filesystem::rename (sTmpPath, sMetaPath, ec);
            if (ec)
               m_pINetwork_Impl->Log (IENGINE::kLOGLEVEL_Warning, "NETWORK", "Failed to rename meta temp: " + ec.message ());
         }
      }
   }

   void Meta_Reset ()
   {
      std::error_code ec;

      std::filesystem::remove (Path (NETWORK::DISKFILE_DATA), ec);
      std::filesystem::remove (Path (NETWORK::DISKFILE_META), ec);
      std::filesystem::remove (Path (NETWORK::DISKFILE_TEMP), ec);

      ResetState ();

      m_nAssetIx = m_pINetwork_Impl->Asset_Index ();
   }

   void ResetState ()
   {
      m_bState = NETWORK::STATE_IDLE;
      m_sHash.clear ();
      m_nSizeBytes = 0;
      m_nHttpStatus = 0;
      m_dFetchQueuedTime = 0.0;
      m_dFetchStartTime = 0.0;
      m_dFetchEndTime = 0.0;
      m_bServedFromCache = false;
      m_bReset = false;
      m_nAssetIx = 0;
      m_mapHeaders.clear ();
   }

   void Evict ()
   {
      m_bState = NETWORK::STATE_IDLE;
      m_nSizeBytes = 0;
      m_nHttpStatus = 0;
      m_dFetchQueuedTime = 0.0;
      m_dFetchStartTime = 0.0;
      m_dFetchEndTime = 0.0;
      m_bServedFromCache = false;
      m_mapHeaders.clear ();
   }

   // ---------------------------------------------------------------------------
   // Disk path helpers
   // ---------------------------------------------------------------------------

   std::string Path (NETWORK::DISKFILE eType) const
   {
      static const char* aExt[] = { ".data", ".temp", ".meta" };

      return m_sPathname + aExt[eType];
   }

   // ---------------------------------------------------------------------------
   // Hash helpers
   // ---------------------------------------------------------------------------

   static bool ParseSriHash (const std::string& sSri, std::string& sAlgo, std::string& sDigest)
   {
      bool bResult = false;

      auto nDash = sSri.find ('-');
      if (nDash != std::string::npos  &&  nDash > 0  &&  nDash < sSri.size () - 1)
      {
         sAlgo   = sSri.substr (0, nDash);
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

   static std::string ComputeFileHash (const std::string& sFilePath, const std::string& sAlgo)
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

public:
   INETWORK_IMPL*             m_pINetwork_Impl;
   std::string                m_sUrl;
   std::string                m_sHash;
   std::string                m_sPathname;
   NETWORK::STATE             m_bState;

   uint64_t                   m_nSizeBytes;
   std::string                m_sCreatedAt;
   std::string                m_sLastAccessedAt;
   uint32_t                   m_nAccessCount;
   uint32_t                   m_nAssetIx;

   long                       m_nHttpStatus;
   double                     m_dFetchQueuedTime;
   double                     m_dFetchStartTime;
   double                     m_dFetchEndTime;
   bool                       m_bServedFromCache;
   bool                       m_bReset;
   uint32_t                   m_nCount_Open;
   uint32_t                   m_nCount_Attach;

   IJOB*                      m_pAsset_Fetch;

   std::vector<NETWORK::FILE*> m_apFiles;

   std::recursive_mutex       m_mxAsset;

   std::unordered_map<std::string, std::string> m_mapHeaders;
};

// ---------------------------------------------------------------------------
// ASSET
// ---------------------------------------------------------------------------

ASSET::ASSET (INETWORK_IMPL* pINetwork_Impl, const std::string& sUrl, const std::string& sPathname, uint32_t nAssetIx) :
   m_pImpl (new Impl (pINetwork_Impl, sUrl, sPathname, nAssetIx))
{
}

ASSET::~ASSET ()
{
   if (m_pImpl->m_pAsset_Fetch)
   {
      m_pImpl->m_pAsset_Fetch->Cancel ();
      m_pImpl->m_pAsset_Fetch = nullptr;
   }

   delete m_pImpl;
}

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

void ASSET::Open (NETWORK::FILE* pFile)
{
   std::lock_guard<std::recursive_mutex> guard (m_pImpl->m_mxAsset);

   m_pImpl->m_apFiles.push_back (pFile);

   m_pImpl->m_nCount_Open++;
}

size_t ASSET::Close (NETWORK::FILE* pFile)
{
   // if pFile == nullptr, the fetch thread is releasing its implicit lock

   std::lock_guard<std::recursive_mutex> guard (m_pImpl->m_mxAsset);

   m_pImpl->m_nCount_Open--;

   if (pFile)
   {
      auto it = std::find (m_pImpl->m_apFiles.begin (), m_pImpl->m_apFiles.end (), pFile);
      if (it != m_pImpl->m_apFiles.end ())
         m_pImpl->m_apFiles.erase (it);
   }

   return m_pImpl->m_nCount_Open;
}

bool ASSET::Attach (NETWORK::FILE* pFile, bool bFetch_Allowed)
{
   std::lock_guard<std::recursive_mutex> guard (m_pImpl->m_mxAsset);

   bool bResult = false;

   if (pFile->AssetIx () == 0  ||  pFile->AssetIx () == m_pImpl->m_nAssetIx)
   {
      m_pImpl->m_nCount_Attach++;

      if (m_pImpl->m_nCount_Attach == 1)
         m_pImpl->Meta_Load ();

      pFile->SnapshotInitial ();

      std::string sHash         = pFile->OpenHash ();
      bool        bCacheEnabled = pFile->CacheEnabled ();
      bool        bStale        = m_pImpl->m_bState == NETWORK::STATE_READY  &&  m_pImpl->m_pINetwork_Impl->Rules_Stale (this);

      bool bFetch  = false;
      bool bReady  = false;
      bool bFailed = false;

      NETWORK::STATE bState = m_pImpl->m_bState;

      if (bState == NETWORK::STATE_READY  &&  bStale)
      {
         // Cached but stale per rules — discard and re-fetch
         m_pImpl->Meta_Reset ();
         bFetch = true;
      }
      else if (bState == NETWORK::STATE_READY  &&  !sHash.empty ()  &&  !IsHashed ())
      {
         // Cached without hash — caller now requires integrity verification
         if (VerifyHash (m_pImpl->Path (NETWORK::DISKFILE_DATA), sHash))
         {
            m_pImpl->m_sHash = sHash;
            m_pImpl->TouchAccess ();
            m_pImpl->m_bServedFromCache = true;
            bReady  = true;
         }
         else
         {
            // Hash mismatch — cached data is stale or corrupt, re-fetch
            m_pImpl->m_sHash = sHash;
            bFetch = true;
         }
      }
      else if (bState != NETWORK::STATE_FETCHING  &&  !sHash.empty ()  &&  IsHashed ()  &&  Hash () != sHash)
      {
         // Caller's hash differs from the asset's — content has been revised, re-fetch
         m_pImpl->m_sHash = sHash;
         bFetch = true;
      }
      else if (bState == NETWORK::STATE_READY  &&  !bCacheEnabled)
      {
         // Cached but caller has caching disabled — force a fresh fetch
         m_pImpl->m_bServedFromCache = false;
         bFetch = true;
      }
      else if (bState == NETWORK::STATE_READY)
      {
         // Cached and valid — serve from disk
         m_pImpl->TouchAccess ();
         m_pImpl->m_bServedFromCache = true;
         bReady  = true;
      }
      else if (bState == NETWORK::STATE_FAILED)
      {
         // Previous fetch failed — propagate the failure to this caller
         bFailed = true;
      }
      else if (bState == NETWORK::STATE_IDLE)
      {
         // Never fetched — first request for this URL
         if (!sHash.empty ())
            m_pImpl->m_sHash = sHash;

         bFetch = true;
      }
      // TODO: second attach with a different hash triggers re-fetch, which will
      // notify the first file again with OnFileReady — need to decide whether
      // to suppress re-notification or version-gate callbacks.

      else if (bState == NETWORK::STATE_FETCHING)
      {
         // Already in flight — this caller will be notified when it completes
      }

      if (bFetch  &&  bFetch_Allowed)
      {
         if (m_pImpl->m_pAsset_Fetch)
         {
            m_pImpl->m_pAsset_Fetch->Cancel ();
            m_pImpl->m_pAsset_Fetch = nullptr;
         }

         m_pImpl->m_bState = NETWORK::STATE_FETCHING;
         m_pImpl->m_dFetchStartTime = m_pImpl->m_pINetwork_Impl->SecondsSinceEpoch ();
         pFile->SnapshotProgress ();

         m_pImpl->m_nCount_Open++;
         m_pImpl->m_nCount_Attach++;

         auto* pJob = new ASSET_FETCH (this, m_pImpl->m_sUrl, m_pImpl->Path (NETWORK::DISKFILE_TEMP), m_pImpl->Path (NETWORK::DISKFILE_DATA), m_pImpl->m_sHash);

         m_pImpl->m_pAsset_Fetch = pJob;
         m_pImpl->m_pINetwork_Impl->Queue_Post_Fetch (pJob);
      }
      else if (bReady  ||  bFailed)
      {
         pFile->SnapshotFinal ();

         m_pImpl->m_nCount_Open++;
         m_pImpl->m_nCount_Attach++;

         auto* pJob = new ASSET_FETCH (this, pFile, bState);

         m_pImpl->m_pAsset_Fetch = pJob;
         m_pImpl->m_pINetwork_Impl->Queue_Post_Fetch (pJob);
      }

      bResult = true;
   }
   else
   {
      // Asset index mismatch — caller holds a stale version, reject the attach
   }

   return bResult;
}

void ASSET::Detach (NETWORK::FILE* pFile)
{
   std::lock_guard<std::recursive_mutex> guard (m_pImpl->m_mxAsset);

   m_pImpl->m_nCount_Attach--;

   if (m_pImpl->m_nCount_Attach == 0)
   {
      if (m_pImpl->m_bState == NETWORK::STATE_READY)
         m_pImpl->Meta_Save ();
      else if (m_pImpl->m_bState == NETWORK::STATE_FAILED)
         m_pImpl->Meta_Reset ();

      m_pImpl->Evict ();
   }
}

void ASSET::Reset ()
{
   std::lock_guard<std::recursive_mutex> guard (m_pImpl->m_mxAsset);

   if (m_pImpl->m_bState == NETWORK::STATE_READY)
   {
      m_pImpl->m_bReset = true;

      m_pImpl->Meta_Save ();
   }
   else m_pImpl->Meta_Reset ();
}

// ---------------------------------------------------------------------------
// Fetch
// ---------------------------------------------------------------------------

void ASSET::FetchComplete (const FETCH_RESULT& Fetch_Result)
{
   {
      std::lock_guard<std::recursive_mutex> guard (m_pImpl->m_mxAsset);

      m_pImpl->m_nHttpStatus   = Fetch_Result.nHttpStatus;
      m_pImpl->m_dFetchEndTime = m_pImpl->m_pINetwork_Impl->SecondsSinceEpoch ();

      if (Fetch_Result.bSuccess)
      {
         m_pImpl->m_nSizeBytes = Fetch_Result.nSizeBytes;
         m_pImpl->m_mapHeaders = Fetch_Result.mapHeaders;
         m_pImpl->m_bState     = NETWORK::STATE_READY;
      }
      else
      {
         if (!Fetch_Result.mapHeaders.empty ())
            m_pImpl->m_mapHeaders = Fetch_Result.mapHeaders;
         m_pImpl->m_bState     = NETWORK::STATE_FAILED;
      }

      for (auto* pFile : m_pImpl->m_apFiles)
      {
         pFile->SnapshotFinal ();
         pFile->Notify_Changed ();

         NETWORK::IFILE* pListener = pFile->Listener ();
         if (pListener)
         {
            if (Fetch_Result.bSuccess)
               pListener->OnFileReady (pFile);
            else pListener->OnFileFailed (pFile);
         }   

         // the listener may close the file, so it may not be referenced after the notification is called
      }

      m_pImpl->m_pINetwork_Impl->Log (IENGINE::kLOGLEVEL_Trace, "NETWORK", (Fetch_Result.bSuccess ? "Cached " : "Failed ") + m_pImpl->m_sUrl + " (" + std::to_string (Fetch_Result.nSizeBytes) + " bytes)");

      m_pImpl->m_pAsset_Fetch = nullptr;
   }
   
   Detach (nullptr);
   m_pImpl->m_pINetwork_Impl->Asset_Close (this, nullptr);
}

void ASSET::FetchComplete (NETWORK::FILE* pFile, NETWORK::STATE bState)
{
   {
      std::lock_guard<std::recursive_mutex> guard (m_pImpl->m_mxAsset);

      NETWORK::IFILE* pListener = pFile->Listener ();
      if (pListener)
      {
         if (bState == NETWORK::STATE_READY)
            pListener->OnFileReady (pFile);
         else pListener->OnFileFailed (pFile);

         // the listener may close the file, so it may not be referenced after the notification is called
      }

      m_pImpl->m_pAsset_Fetch = nullptr;
   }
   
   Detach (nullptr);
   m_pImpl->m_pINetwork_Impl->Asset_Close (this, nullptr);
}


// ---------------------------------------------------------------------------
// Data access
// ---------------------------------------------------------------------------

void ASSET::ReadData (std::vector<uint8_t>& aData) const
{
   aData.clear ();

   std::lock_guard<std::recursive_mutex> guard (m_pImpl->m_mxAsset);

   if (m_pImpl->m_bState == NETWORK::STATE_READY)
   {
      std::ifstream file (m_pImpl->Path (NETWORK::DISKFILE_DATA), std::ios::binary | std::ios::ate);
      if (file.is_open ())
      {
         auto nSize = file.tellg ();
         if (nSize > 0)
         {
            aData.resize (static_cast<size_t> (nSize));
            file.seekg (0, std::ios::beg);
            file.read (reinterpret_cast<char*> (aData.data ()), nSize);
         }
      }
   }
}

std::string ASSET::Header (const std::string& sName) const
{
   std::lock_guard<std::recursive_mutex> guard (m_pImpl->m_mxAsset);

   std::string sResult;

   auto it = m_pImpl->m_mapHeaders.find (sName);
   if (it != m_pImpl->m_mapHeaders.end ())
      sResult = it->second;

   return sResult;
}

// ---------------------------------------------------------------------------
// Hash verification
// ---------------------------------------------------------------------------

bool ASSET::VerifyHash (const std::string& sFilePath, const std::string& sHash) const
{
   bool bResult = true;

   if (!sHash.empty ())
   {
      bResult = false;
      std::string sAlgo, sExpected;
      if (Impl::ParseSriHash (sHash, sAlgo, sExpected))
      {
         std::string sActual = Impl::ComputeFileHash (sFilePath, sAlgo);
         bResult = (sActual == sExpected);
      }
   }

   return bResult;
}

// ---------------------------------------------------------------------------
// Accessors
// ---------------------------------------------------------------------------

NETWORK::STATE       ASSET::State ()                        const { return m_pImpl->m_bState;            }
bool                 ASSET::IsReset ()                      const { return m_pImpl->m_bReset;            }
size_t               ASSET::File_Count ()                   const { return m_pImpl->m_apFiles.size ();   }
const std::string&   ASSET::Url ()                          const { return m_pImpl->m_sUrl;              }
uint64_t             ASSET::SizeBytes ()                    const { return m_pImpl->m_nSizeBytes;        }
std::string          ASSET::CreatedTime ()                  const { return m_pImpl->m_sCreatedAt;        }
std::string          ASSET::LastAccessTime ()               const { return m_pImpl->m_sLastAccessedAt;   }
uint32_t             ASSET::AccessCount ()                  const { return m_pImpl->m_nAccessCount;      }
uint32_t             ASSET::AssetIx ()                      const { return m_pImpl->m_nAssetIx;          }
const std::string&   ASSET::Hash ()                         const { return m_pImpl->m_sHash;             }
bool                 ASSET::IsHashed ()                     const { return !m_pImpl->m_sHash.empty ();   }
std::string          ASSET::DiskPath ()                     const { return m_pImpl->Path (NETWORK::DISKFILE_DATA); }
const std::string&   ASSET::Pathname ()                     const { return m_pImpl->m_sPathname;         }
std::string          ASSET::Path (NETWORK::DISKFILE eType)  const { return m_pImpl->Path (eType);        }
long                 ASSET::HttpStatus ()                   const { return m_pImpl->m_nHttpStatus;       }
double               ASSET::FetchStartTime ()               const { return m_pImpl->m_dFetchStartTime;   }
double               ASSET::FetchEndTime ()                 const { return m_pImpl->m_dFetchEndTime;     }
double               ASSET::FetchDuration ()                const { return m_pImpl->m_dFetchEndTime - m_pImpl->m_dFetchStartTime; }
double               ASSET::FetchQueuedTime ()              const { return m_pImpl->m_dFetchQueuedTime;  }
double               ASSET::QueueDuration ()                const { return m_pImpl->m_dFetchStartTime - m_pImpl->m_dFetchQueuedTime; }
bool                 ASSET::IsServedFromCache ()            const { return m_pImpl->m_bServedFromCache;  }

const std::unordered_map<std::string, std::string>&   ASSET::Headers ()      const { return m_pImpl->m_mapHeaders; }
