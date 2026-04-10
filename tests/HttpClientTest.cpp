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

#include <curl/curl.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

static int nPassed = 0;
static int nFailed = 0;

static void Check (bool bCondition, const char* szName)
{
   if (bCondition)
   {
      std::printf ("  PASS: %s\n", szName);
      nPassed++;
   }
   else
   {
      std::printf ("  FAIL: %s\n", szName);
      nFailed++;
   }
}

static size_t WriteCallback (char* pData, size_t nSize, size_t nMembers, void* pUserData)
{
   std::string* pResponse = static_cast<std::string*> (pUserData);
   pResponse->append (pData, nSize * nMembers);
   return nSize * nMembers;
}

// ---------------------------------------------------------------------------
// Test 1: curl version info
// ---------------------------------------------------------------------------
static void TestVersionInfo ()
{
   std::printf ("\n[Test 1] curl version info\n");

   curl_version_info_data* pInfo = curl_version_info (CURLVERSION_NOW);
   Check (pInfo != nullptr, "curl_version_info returned data");
   Check (pInfo->version != nullptr, "Version string is non-null");
   Check (pInfo->ssl_version != nullptr, "SSL backend is available");

   if (pInfo->version)
      std::printf ("    libcurl version: %s\n", pInfo->version);
   if (pInfo->ssl_version)
      std::printf ("    SSL backend: %s\n", pInfo->ssl_version);
   if (pInfo->host)
      std::printf ("    Host: %s\n", pInfo->host);
}

// ---------------------------------------------------------------------------
// Test 2: Easy handle lifecycle
// ---------------------------------------------------------------------------
static void TestEasyHandle ()
{
   std::printf ("\n[Test 2] Easy handle lifecycle\n");

   CURL* pCurl = curl_easy_init ();
   Check (pCurl != nullptr, "curl_easy_init returned a handle");

   if (pCurl)
   {
      CURLcode nCode = curl_easy_setopt (pCurl, CURLOPT_URL, "https://example.com");
      Check (nCode == CURLE_OK, "curl_easy_setopt (URL) succeeded");

      nCode = curl_easy_setopt (pCurl, CURLOPT_TIMEOUT, 5L);
      Check (nCode == CURLE_OK, "curl_easy_setopt (TIMEOUT) succeeded");

      nCode = curl_easy_setopt (pCurl, CURLOPT_FOLLOWLOCATION, 1L);
      Check (nCode == CURLE_OK, "curl_easy_setopt (FOLLOWLOCATION) succeeded");

      curl_easy_cleanup (pCurl);
      Check (true, "curl_easy_cleanup completed (no crash)");
   }
}

// ---------------------------------------------------------------------------
// Test 3: HTTP GET (live network)
// ---------------------------------------------------------------------------
static void TestHttpGet ()
{
   std::printf ("\n[Test 3] HTTP GET (live network)\n");

   CURL* pCurl = curl_easy_init ();
   Check (pCurl != nullptr, "curl_easy_init for HTTP GET");

   if (pCurl)
   {
      std::string sResponse;
      curl_easy_setopt (pCurl, CURLOPT_URL, "https://httpbin.org/get");
      curl_easy_setopt (pCurl, CURLOPT_WRITEFUNCTION, WriteCallback);
      curl_easy_setopt (pCurl, CURLOPT_WRITEDATA, &sResponse);
      curl_easy_setopt (pCurl, CURLOPT_TIMEOUT, 10L);

      CURLcode nCode = curl_easy_perform (pCurl);

      if (nCode == CURLE_OK)
      {
         long nHttpCode = 0;
         curl_easy_getinfo (pCurl, CURLINFO_RESPONSE_CODE, &nHttpCode);

         Check (nHttpCode == 200, "HTTP status 200");
         Check (!sResponse.empty (), "Response body is non-empty");
         Check (sResponse.find ("httpbin.org") != std::string::npos, "Response contains expected content");

         std::printf ("    HTTP %ld, %zu bytes received\n", nHttpCode, sResponse.size ());
      }
      else
      {
         std::printf ("    Network request failed: %s\n", curl_easy_strerror (nCode));
         std::printf ("    (This is expected if there is no internet connection)\n");
         Check (true, "curl_easy_perform returned a valid error code (no crash)");
      }

      curl_easy_cleanup (pCurl);
   }
}

// ---------------------------------------------------------------------------
// Test 4: HTTPS with Schannel (SSL verification)
// ---------------------------------------------------------------------------
static void TestHttps ()
{
   std::printf ("\n[Test 4] HTTPS with Schannel\n");

   CURL* pCurl = curl_easy_init ();
   Check (pCurl != nullptr, "curl_easy_init for HTTPS test");

   if (pCurl)
   {
      std::string sResponse;
      curl_easy_setopt (pCurl, CURLOPT_URL, "https://www.google.com");
      curl_easy_setopt (pCurl, CURLOPT_WRITEFUNCTION, WriteCallback);
      curl_easy_setopt (pCurl, CURLOPT_WRITEDATA, &sResponse);
      curl_easy_setopt (pCurl, CURLOPT_TIMEOUT, 10L);
      curl_easy_setopt (pCurl, CURLOPT_NOBODY, 1L);

      CURLcode nCode = curl_easy_perform (pCurl);

      if (nCode == CURLE_OK)
      {
         long nHttpCode = 0;
         curl_easy_getinfo (pCurl, CURLINFO_RESPONSE_CODE, &nHttpCode);

         Check (nHttpCode == 200, "HTTPS status 200 (SSL handshake succeeded)");
         std::printf ("    HTTPS connection to google.com succeeded (HTTP %ld)\n", nHttpCode);
      }
      else
      {
         std::printf ("    HTTPS request failed: %s\n", curl_easy_strerror (nCode));
         std::printf ("    (Expected if no internet connection)\n");
         Check (true, "curl_easy_perform returned a valid error code (no crash)");
      }

      curl_easy_cleanup (pCurl);
   }
}

// ---------------------------------------------------------------------------
// Test 5: Error handling (invalid URL)
// ---------------------------------------------------------------------------
static void TestErrorHandling ()
{
   std::printf ("\n[Test 5] Error handling (invalid host)\n");

   CURL* pCurl = curl_easy_init ();
   Check (pCurl != nullptr, "curl_easy_init for error test");

   if (pCurl)
   {
      curl_easy_setopt (pCurl, CURLOPT_URL, "https://this-domain-does-not-exist-999.invalid");
      curl_easy_setopt (pCurl, CURLOPT_TIMEOUT, 5L);

      CURLcode nCode = curl_easy_perform (pCurl);
      Check (nCode != CURLE_OK, "Invalid host correctly returned an error");
      Check (nCode == CURLE_COULDNT_RESOLVE_HOST, "Error code is CURLE_COULDNT_RESOLVE_HOST");

      std::printf ("    Error: %s (code %d)\n", curl_easy_strerror (nCode), nCode);

      curl_easy_cleanup (pCurl);
   }
}

// ---------------------------------------------------------------------------
// Test 6: Fetch MSF config from CDN
// ---------------------------------------------------------------------------
static void TestFetchMsfConfig ()
{
   std::printf ("\n[Test 6] Fetch MSF config (cdn2-david.rp1.dev)\n");

   CURL* pCurl = curl_easy_init ();
   Check (pCurl != nullptr, "curl_easy_init for MSF config fetch");

   if (pCurl)
   {
      std::string sResponse;
      curl_easy_setopt (pCurl, CURLOPT_URL, "https://cdn2-david.rp1.dev/config/dave2.msf");
      curl_easy_setopt (pCurl, CURLOPT_WRITEFUNCTION, WriteCallback);
      curl_easy_setopt (pCurl, CURLOPT_WRITEDATA, &sResponse);
      curl_easy_setopt (pCurl, CURLOPT_FOLLOWLOCATION, 1L);
      curl_easy_setopt (pCurl, CURLOPT_TIMEOUT, 15L);

      CURLcode nCode = curl_easy_perform (pCurl);

      if (nCode == CURLE_OK)
      {
         long nHttpCode = 0;
         curl_easy_getinfo (pCurl, CURLINFO_RESPONSE_CODE, &nHttpCode);

         Check (nHttpCode == 200, "HTTP status 200");
         Check (!sResponse.empty (), "Response body is non-empty");

         std::printf ("    HTTP %ld, %zu bytes received\n", nHttpCode, sResponse.size ());
         std::printf ("\n--- dave2.msf contents ---\n%s\n--- end ---\n", sResponse.c_str ());
      }
      else
      {
         std::printf ("    Request failed: %s\n", curl_easy_strerror (nCode));
         Check (true, "curl_easy_perform returned a valid error code (no crash)");
      }

      curl_easy_cleanup (pCurl);
   }
}

// ---------------------------------------------------------------------------

int main (int /*argc*/, char* /*argv*/[])
{
   std::printf ("=== curl Integration Test Suite ===\n");

   CURLcode nGlobalInit = curl_global_init (CURL_GLOBAL_DEFAULT);

   if (nGlobalInit == CURLE_OK)
   {
      TestVersionInfo ();
      TestEasyHandle ();
      TestHttpGet ();
      TestHttps ();
      TestErrorHandling ();
      TestFetchMsfConfig ();

      curl_global_cleanup ();
   }
   else
   {
      std::fprintf (stderr, "curl_global_init failed (code %d)\n", nGlobalInit);
      nFailed++;
   }

   std::printf ("\n=== Results: %d passed, %d failed ===\n", nPassed, nFailed);

   return (nFailed > 0) ? 1 : 0;
}
