// Copyright 2026 Open Metaverse Browser Initiative (OMBI)
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

#include "net/HttpClient.h"

#include <curl/curl.h>

#include <cstdio>

namespace rubidium
{
namespace net
{

static size_t WriteCallback (char* pData, size_t nSize, size_t nMembers, void* pUserData)
{
   std::string* pResponse = static_cast<std::string*> (pUserData);
   pResponse->append (pData, nSize * nMembers);
   return nSize * nMembers;
}

HTTP_CLIENT::HTTP_CLIENT ()
   : bInitialized (false)
{
}

HTTP_CLIENT::~HTTP_CLIENT ()
{
   Shutdown ();
}

bool HTTP_CLIENT::Initialize ()
{
   CURLcode nCode = curl_global_init (CURL_GLOBAL_DEFAULT);
   if (nCode != CURLE_OK)
   {
      std::fprintf (stderr, "HTTP_CLIENT: curl_global_init failed (code %d)\n", nCode);
   }
   else
   {
      bInitialized = true;

      curl_version_info_data* pInfo = curl_version_info (CURLVERSION_NOW);
      std::printf ("HTTP_CLIENT: libcurl %s initialized (SSL: %s)\n",
         pInfo->version, pInfo->ssl_version ? pInfo->ssl_version : "none");
   }

   return bInitialized;
}

void HTTP_CLIENT::Shutdown ()
{
   if (bInitialized)
   {
      curl_global_cleanup ();
      bInitialized = false;
   }
}

bool HTTP_CLIENT::Get (const std::string& sUrl, std::string& sResponse, long& nHttpCode)
{
   bool bOk = false;

   CURL* pCurl = curl_easy_init ();
   if (pCurl)
   {
      curl_easy_setopt (pCurl, CURLOPT_URL, sUrl.c_str ());
      curl_easy_setopt (pCurl, CURLOPT_WRITEFUNCTION, WriteCallback);
      curl_easy_setopt (pCurl, CURLOPT_WRITEDATA, &sResponse);
      curl_easy_setopt (pCurl, CURLOPT_FOLLOWLOCATION, 1L);
      curl_easy_setopt (pCurl, CURLOPT_TIMEOUT, 10L);

      CURLcode nCode = curl_easy_perform (pCurl);

      bOk = (nCode == CURLE_OK);
      if (bOk)
         curl_easy_getinfo (pCurl, CURLINFO_RESPONSE_CODE, &nHttpCode);

      curl_easy_cleanup (pCurl);
   }

   return bOk;
}

} // namespace net
} // namespace rubidium
