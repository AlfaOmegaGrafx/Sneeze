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
#include "Control.h"
#include <fstream>
#include <filesystem>
#include <cstdio>

#include <curl/curl.h>
#include <openssl/evp.h>

using namespace SNEEZE;

void JOB_FETCH::Result (const FETCH_RESULT& result)
{
   m_ResultComplete = result;
}

void JOB_FETCH::Complete_Deliver ()
{
   OnFetch_Complete (m_ResultComplete);
}

JOB_FETCH::JOB_FETCH (bool bFetch, const std::string& sUrl, const std::string& sPath_Temp, const std::string& sPath_Data, const std::string& sHash) :
   m_bFetch         (bFetch),
   m_sUrl           (sUrl),
   m_sPath_Temp     (sPath_Temp),
   m_sPath_Data     (sPath_Data),
   m_sHash          (sHash),
   m_ResultComplete {}
{
}


// ===========================================================================
// Static helpers -- encoding
// ===========================================================================

static bool IsAscii (const std::string& sIn)
{
   for (unsigned char c : sIn)
      if (c >= 0x80)
         return false;
   return true;
}

static bool IsValidUtf8 (const std::string& sIn)
{
   size_t i = 0;
   while (i < sIn.size ())
   {
      unsigned char c = static_cast<unsigned char> (sIn[i]);
      size_t n;
      if      (c < 0x80)            { ++i; continue; }
      else if ((c & 0xE0) == 0xC0)  n = 2;
      else if ((c & 0xF0) == 0xE0)  n = 3;
      else if ((c & 0xF8) == 0xF0)  n = 4;
      else                          return false;

      if (i + n > sIn.size ())
         return false;
      for (size_t k = 1; k < n; ++k)
         if ((static_cast<unsigned char> (sIn[i + k]) & 0xC0) != 0x80)
            return false;
      i += n;
   }
   return true;
}

static std::string ToUtf8 (const std::string& sIn)
{
   if (IsAscii (sIn) || IsValidUtf8 (sIn))
      return sIn;

   std::string sOut;
   sOut.reserve (sIn.size () * 2);
   for (unsigned char c : sIn)
   {
      if (c < 0x80) sOut.push_back (static_cast<char> (c));
      else
      {
         sOut.push_back (static_cast<char> (0xC0 | (c >> 6)));
         sOut.push_back (static_cast<char> (0x80 | (c & 0x3F)));
      }
   }
   return sOut;
}

// ===========================================================================
// Static helpers -- curl callbacks
// ===========================================================================

static size_t WriteCallback (char* pData, size_t nSize, size_t nMembers, void* pUserData)
{
   std::ofstream* stream = static_cast<std::ofstream*> (pUserData);

   stream->write (pData, nSize * nMembers);

   return nSize * nMembers;
}

#if 1
static size_t HeaderCallback (char* pData, size_t nSize, size_t nMembers, void* pUserData)
{
   const size_t bytes = nSize * nMembers;
   auto* pmHeaders = static_cast<std::unordered_map<std::string, std::string>*> (pUserData);

   std::string sLine (pData, bytes);

   while (!sLine.empty ()  &&  (sLine.back () == '\r'  ||  sLine.back () == '\n'))
   {
      sLine.pop_back ();
   }

   const auto pos = sLine.find (':');
   if (pos != std::string::npos)
   {
      std::string sName = sLine.substr (0, pos);
      std::string sValue = sLine.substr (pos + 1);

      std::transform (sName.begin (), sName.end (), sName.begin (), [](unsigned char c) { return std::tolower (c); });

      size_t nStart  = sValue.find_first_not_of (" \t");
      size_t nEnd    = sValue.find_last_not_of (" \t");

      if (nStart == std::string::npos)
      {
         sValue.clear ();
      }
      else
      {
         sValue = sValue.substr (nStart, nEnd - nStart + 1);
      }

      (*pmHeaders)[sName] = sValue;
   }

   return bytes;
}
#else
static size_t HeaderCallback (char* pData, size_t nSize, size_t nMembers, void* pUserData)
{
   auto* pmapHeaders = static_cast<std::unordered_map<std::string, std::string>*> (pUserData);

   size_t nTotal = nSize * nMembers;
   std::string sLine (pData, nTotal);

   auto nColon = sLine.find (':');
   if (nColon != std::string::npos)
   {
      std::string sKey = sLine.substr (0, nColon);
      std::string sValue = sLine.substr (nColon + 1);

      auto nStart = sValue.find_first_not_of (" \t\r\n");
      auto nEnd = sValue.find_last_not_of (" \t\r\n");
      if (nStart != std::string::npos  &&  nEnd != std::string::npos)
         sValue = sValue.substr (nStart, nEnd - nStart + 1);
      else
         sValue.clear ();

      sKey = ToUtf8 (sKey);
      sValue = ToUtf8 (sValue);
      std::transform (sKey.begin (), sKey.end (), sKey.begin (),
         [](unsigned char c) { return static_cast<char> (std::tolower (c)); });
      (*pmapHeaders)[sKey] = sValue;
   }

   return nTotal;
}
#endif

struct PROGRESS_DATA
{
   JOB_FETCH* pJob_Fetch;
};

static int ProgressCallback (void* pClientData, curl_off_t, curl_off_t, curl_off_t, curl_off_t)
{
   auto* pProgress = static_cast<PROGRESS_DATA*> (pClientData);

   if (pProgress->pJob_Fetch->IsCancelled ())
      return 1;

   return 0;
}

// ===========================================================================
// Static helpers -- hash verification
// ===========================================================================

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

static bool VerifyHash (const std::string& sFilePath, const std::string& sHash)
{
   bool bResult = true;

   if (!sHash.empty ())
   {
      bResult = false;
      std::string sAlgo, sExpected;
      if (ParseSriHash (sHash, sAlgo, sExpected))
      {
         std::string sActual = ComputeFileHash (sFilePath, sAlgo);
         bResult = (sActual == sExpected);
      }
   }

   return bResult;
}

// ===========================================================================
// AGENT::FETCH
// ===========================================================================

AGENT::FETCH::FETCH (POOL* pPool, int nAgentIz)
   : AGENT (pPool, nAgentIz)
{
}

AGENT::FETCH::~FETCH ()
{
   Join ();
}

void AGENT::FETCH::Main ()
{
   Ready ();
   Wait ([this] { return Job (); });
}

// ---------------------------------------------------------------------------
// Job -- grab and process jobs until empty, then return IsShutdown.
//
// Owns: IsCancelled checks, Result(), calling Complete (completion + delete).
// Execute does only the download -- Job handles the lifecycle.
// ---------------------------------------------------------------------------

bool AGENT::FETCH::Job ()
{
   bool bResult, bJob;
   JOB_FETCH* pJob_Fetch = nullptr;
   auto* pQueue = static_cast<POOL_QUEUE<JOB_FETCH*>*> (m_pPool);

   while (true)
   {
      bResult = IsShutdown ();
      bJob    = pQueue->Grab (pJob_Fetch);

      m_bBusy.store (bJob, std::memory_order_release);

      if (bJob)  // flush out all jobs before shutdown
      {
         if (!pJob_Fetch->IsCancelled ())
         {
            Execute (pJob_Fetch);
         }

         pJob_Fetch->Complete ();
      }
      else break;
   }

   return bResult;
}

// ---------------------------------------------------------------------------
// Execute -- the blocking curl download. Reads params from the JOB_FETCH.
// Stores results via JOB_FETCH::Result(). Does NOT call Complete() -- Job
// does that once after return.
// ---------------------------------------------------------------------------

void AGENT::FETCH::Execute (JOB_FETCH* pJob_Fetch)
{
   if (pJob_Fetch->IsFetch ())
   {
      FETCH_RESULT result = {};
      result.bSuccess    = false;
      result.nSizeBytes  = 0;
      result.nHttpStatus = 0;

      if (!pJob_Fetch->IsCancelled ())
      {
         std::filesystem::create_directories (std::filesystem::path (pJob_Fetch->Path_Temp ()).parent_path ());

         CURL* pCurl = curl_easy_init ();

         if (pCurl)
         {
            std::ofstream out (pJob_Fetch->Path_Temp (), std::ios::binary);

            if (out.is_open ())
            {
               curl_easy_setopt (pCurl, CURLOPT_URL, pJob_Fetch->Url ().c_str ());
               curl_easy_setopt (pCurl, CURLOPT_WRITEFUNCTION, WriteCallback);
               curl_easy_setopt (pCurl, CURLOPT_WRITEDATA, &out);

               curl_easy_setopt (pCurl, CURLOPT_HEADERFUNCTION, HeaderCallback);
               curl_easy_setopt (pCurl, CURLOPT_HEADERDATA, &result.mapHeaders);

               curl_easy_setopt (pCurl, CURLOPT_FOLLOWLOCATION, 1L);
               curl_easy_setopt (pCurl, CURLOPT_TIMEOUT, 300L);

               PROGRESS_DATA progress = { pJob_Fetch };
               curl_easy_setopt (pCurl, CURLOPT_XFERINFOFUNCTION, ProgressCallback);
               curl_easy_setopt (pCurl, CURLOPT_XFERINFODATA, &progress);
               curl_easy_setopt (pCurl, CURLOPT_NOPROGRESS, 0L);

#if !defined(_WIN32) && !defined(__APPLE__)
               extern const char*        const g_szCaCertPem;
               extern const unsigned long       g_nCaCertPemLen;
               curl_blob caBlob;
               caBlob.data  = const_cast<void*> (static_cast<const void*> (g_szCaCertPem));
               caBlob.len   = g_nCaCertPemLen;
               caBlob.flags = CURL_BLOB_NOCOPY;
               curl_easy_setopt (pCurl, CURLOPT_CAINFO_BLOB, &caBlob);
#endif

               CURLcode nCode = curl_easy_perform (pCurl);

               out.close ();

               long nHttpCode = 0;
               if (nCode == CURLE_OK)
                  curl_easy_getinfo (pCurl, CURLINFO_RESPONSE_CODE, &nHttpCode);
               result.nHttpStatus = nHttpCode;

               if (pJob_Fetch->IsCancelled ())
               {
               }
               else if (nCode == CURLE_ABORTED_BY_CALLBACK)
               {
               }
               else if (nCode != CURLE_OK  ||  nHttpCode < 200  ||  nHttpCode >= 300)
               {
                  std::string sErr = "Fetch failed for " + pJob_Fetch->Url () + " (HTTP " + std::to_string (nHttpCode) + ")";
                  if (nCode != CURLE_OK)
                     sErr += " curl=" + std::to_string (nCode) + " (" + curl_easy_strerror (nCode) + ")";
                  Engine ()->Log (IENGINE::kLOGLEVEL_Warning, "NETWORK", sErr);
               }
               else
               {
                  if (VerifyHash (pJob_Fetch->Path_Temp (), pJob_Fetch->Hash ()))
                  {
                     std::error_code ec;
                     std::filesystem::rename (pJob_Fetch->Path_Temp (), pJob_Fetch->Path_Data (), ec);
                     if (!ec)
                     {
                        auto nFsSize = std::filesystem::file_size (pJob_Fetch->Path_Data (), ec);
                        if (!ec)
                        {
                           result.nSizeBytes = static_cast<uint64_t> (nFsSize);
                           result.bSuccess = true;
                        }
                     }
                     else Engine ()->Log (IENGINE::kLOGLEVEL_Warning, "NETWORK", "Failed to rename " + pJob_Fetch->Path_Temp () + " -> " + pJob_Fetch->Path_Data () + ": " + ec.message ());
                  }
                  else Engine ()->Log (IENGINE::kLOGLEVEL_Warning, "NETWORK", "Hash mismatch for " + pJob_Fetch->Url ());
               }
            }
            else Engine ()->Log (IENGINE::kLOGLEVEL_Error, "NETWORK", "Failed to open temp file: " + pJob_Fetch->Path_Temp ());

            curl_easy_cleanup (pCurl);
         }
         else Engine ()->Log (IENGINE::kLOGLEVEL_Warning, "NETWORK", "Curl failed to initialize");
      }

      if (!result.bSuccess)
      {
         std::error_code ec;
         std::filesystem::remove (pJob_Fetch->Path_Temp (), ec);
      }

      pJob_Fetch->Result (result);
   }
}
