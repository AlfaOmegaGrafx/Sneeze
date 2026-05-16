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

#include "Encoding.h"
#include <fstream>
#include <filesystem>

#include <curl/curl.h>

using namespace SNEEZE;

// ===========================================================================
// NETWORK::FETCH
// ===========================================================================

NETWORK::FETCH::FETCH (ASSET* pAsset, const std::string& sUrl, const std::string& sPath_Temp, const std::string& sPath_Data, const std::string& sHash) :
   m_pAsset       (pAsset),
   m_sUrl         (sUrl),
   m_sPath_Temp   (sPath_Temp),
   m_sPath_Data   (sPath_Data),
   m_sHash        (sHash),
   m_Fetch_Result ({})
{
}

NETWORK::FETCH::~FETCH ()
{
   Join ();
}

const NETWORK::FETCH_RESULT& NETWORK::FETCH::Result () const
{
   return m_Fetch_Result;
}

// ---------------------------------------------------------------------------
// Encoding helpers
// ---------------------------------------------------------------------------

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

// ---------------------------------------------------------------------------
// Curl callbacks
// ---------------------------------------------------------------------------

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

// ---------------------------------------------------------------------------
// Main -- the fetch thread body
// ---------------------------------------------------------------------------

void NETWORK::FETCH::Main ()
{
   Ready ();

   m_Fetch_Result = {};
   m_Fetch_Result.bSuccess    = false;
   m_Fetch_Result.nSizeBytes  = 0;
   m_Fetch_Result.nHttpStatus = 0;

   if (!m_pAsset->IsShuttingDown ())
   {
      std::filesystem::create_directories (std::filesystem::path (m_sPath_Temp).parent_path ());

      CURL* pCurl = curl_easy_init ();

      if (pCurl)
      {
         std::ofstream out (m_sPath_Temp, std::ios::binary);

         if (out.is_open ())
         {
            curl_easy_setopt (pCurl, CURLOPT_URL, m_sUrl.c_str ());
            curl_easy_setopt (pCurl, CURLOPT_WRITEFUNCTION, WriteCallback);
            curl_easy_setopt (pCurl, CURLOPT_WRITEDATA, &out);

            curl_easy_setopt (pCurl, CURLOPT_HEADERFUNCTION, HeaderCallback);
            curl_easy_setopt (pCurl, CURLOPT_HEADERDATA, &m_Fetch_Result.mapHeaders);

            curl_easy_setopt (pCurl, CURLOPT_FOLLOWLOCATION, 1L);
            curl_easy_setopt (pCurl, CURLOPT_TIMEOUT, 300L);

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
            m_Fetch_Result.nHttpStatus = nHttpCode;

            if (m_pAsset->IsShuttingDown ())
            {
            }
            else if (nCode != CURLE_OK  ||  nHttpCode < 200  ||  nHttpCode >= 300)
            {
               std::string sErr = "Fetch failed for " + m_sUrl + " (HTTP " + std::to_string (nHttpCode) + ")";
               if (nCode != CURLE_OK)
                  sErr += " curl=" + std::to_string (nCode) + " (" + curl_easy_strerror (nCode) + ")";
               m_pAsset->Engine ()->Log (IENGINE::kLOGLEVEL_Warning, "NETWORK", sErr);
            }
            else
            {
               if (m_pAsset->VerifyHash (m_sPath_Temp, m_sHash))
               {
                  m_Fetch_Result.sFinalPath = m_sPath_Data;

                  std::error_code ec;
                  std::filesystem::rename (m_sPath_Temp, m_Fetch_Result.sFinalPath, ec);
                  if (!ec)
                  {
                     auto nFsSize = std::filesystem::file_size (m_Fetch_Result.sFinalPath, ec);
                     if (!ec)
                     {
                        m_Fetch_Result.nSizeBytes = static_cast<uint64_t> (nFsSize);
                        m_Fetch_Result.bSuccess = true;
                     }
                  }
                  else m_pAsset->Engine ()->Log (IENGINE::kLOGLEVEL_Warning, "NETWORK", "Failed to rename " + m_sPath_Temp + " -> " + m_Fetch_Result.sFinalPath + ": " + ec.message ());
               }
               else m_pAsset->Engine ()->Log (IENGINE::kLOGLEVEL_Warning, "NETWORK", "Hash mismatch for " + m_sUrl);
            }

            if (!m_Fetch_Result.bSuccess)
            {
               std::error_code ec;
               std::filesystem::remove (m_sPath_Temp, ec);
            }
         }
         else m_pAsset->Engine ()->Log (IENGINE::kLOGLEVEL_Error, "NETWORK", "Failed to open temp file: " + m_sPath_Temp);

         curl_easy_cleanup (pCurl);
      }
      else m_pAsset->Engine ()->Log (IENGINE::kLOGLEVEL_Warning, "NETWORK", "Curl failed to initialize");
   }

   m_pAsset->FetchComplete (m_Fetch_Result);
}
