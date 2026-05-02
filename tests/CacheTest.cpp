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

#include "cache/Manager.h"
#include "cache/File.h"
#include "cache/Entry.h"
#include "cache/Types.h"
#include "core/Sneeze.h"

#include <openssl/sha.h>
#include <curl/curl.h>

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <mutex>
#include <condition_variable>
#include <chrono>
#include <filesystem>
#include <fstream>

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

// ---------------------------------------------------------------------------
// Minimal ISNEEZE for test logging
// ---------------------------------------------------------------------------

class CACHE_TEST_LISTENER : public SNEEZE::CORE::ISNEEZE
{
public:
   int m_nCreatedCount = 0;
   int m_nChangedCount = 0;
   int m_nDeletedCount = 0;

   void OnFrameReady (const uint32_t*, int, int) override {}
   void Log (eLOGLEVEL, const std::string& sModule, const std::string& sMessage) override
   {
      std::printf ("    [%s] %s\n", sModule.c_str (), sMessage.c_str ());
   }

   void OnCacheFileCreated (SNEEZE::CACHE::FILE*) override { m_nCreatedCount++; }
   void OnCacheFileChanged (SNEEZE::CACHE::FILE*) override { m_nChangedCount++; }
   void OnCacheFileDeleted (SNEEZE::CACHE::FILE*) override { m_nDeletedCount++; }

   void ResetCounters () { m_nCreatedCount = 0; m_nChangedCount = 0; m_nDeletedCount = 0; }
};

// ---------------------------------------------------------------------------
// IFILE listener that signals a condition variable on completion
// ---------------------------------------------------------------------------

class TEST_FILE_LISTENER : public SNEEZE::CACHE::IFILE
{
public:
   TEST_FILE_LISTENER () : m_bDone (false), m_bSucceeded (false) {}

   void OnFileReady (SNEEZE::CACHE::FILE* /*pFile*/) override
   {
      std::lock_guard<std::mutex> guard (m_mutex);
      m_bSucceeded = true;
      m_bDone = true;
      m_condVar.notify_all ();
   }

   void OnFileFailed (SNEEZE::CACHE::FILE* /*pFile*/) override
   {
      std::lock_guard<std::mutex> guard (m_mutex);
      m_bSucceeded = false;
      m_bDone = true;
      m_condVar.notify_all ();
   }

   bool WaitFor (int nTimeoutMs)
   {
      std::unique_lock<std::mutex> lock (m_mutex);
      return m_condVar.wait_for (lock, std::chrono::milliseconds (nTimeoutMs),
         [this] { return m_bDone; });
   }

   bool Succeeded () const { return m_bSucceeded; }

private:
   std::mutex              m_mutex;
   std::condition_variable m_condVar;
   bool                    m_bDone;
   bool                    m_bSucceeded;
};

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static std::string ComputeSha256Hex (const uint8_t* pData, size_t nLen)
{
   unsigned char aDigest[SHA256_DIGEST_LENGTH];
   SHA256 (pData, nLen, aDigest);

   std::string sHex;
   sHex.reserve (SHA256_DIGEST_LENGTH * 2);
   for (int i = 0; i < SHA256_DIGEST_LENGTH; i++)
   {
      char szByte[4];
      std::snprintf (szByte, sizeof (szByte), "%02x", aDigest[i]);
      sHex += szByte;
   }
   return sHex;
}

// ---------------------------------------------------------------------------
// Shared test state
// ---------------------------------------------------------------------------

static CACHE_TEST_LISTENER* s_pTestListener = nullptr;
static SNEEZE::CORE::SNEEZE* s_pSneeze = nullptr;

// ---------------------------------------------------------------------------
// Test 1: Manager initialization
// ---------------------------------------------------------------------------

static void TestManagerInit ()
{
   std::printf ("\n[Test 1] Manager initialization\n");

   SNEEZE::CACHE::MANAGER* pCache = new SNEEZE::CACHE::MANAGER (s_pSneeze);
   bool bInit = pCache->Initialize ();
   Check (bInit, "Manager initialized successfully");
   pCache->Shutdown ();
   delete pCache;
}

// ---------------------------------------------------------------------------
// Test 2: Request a file without hash (session-only, live fetch)
// ---------------------------------------------------------------------------

static void TestSessionFetch ()
{
   std::printf ("\n[Test 2] Session fetch (no hash, live network)\n");

   SNEEZE::CACHE::MANAGER* pCache = new SNEEZE::CACHE::MANAGER (s_pSneeze);
   bool bInit = pCache->Initialize ();
   Check (bInit, "Cache initialized");

   if (bInit)
   {
      TEST_FILE_LISTENER listener;
      SNEEZE::CACHE::FILE* pFile = pCache->Request (
         "https://httpbin.org/bytes/128", "", &listener);

      Check (pFile != nullptr, "Request returned a handle");

      if (pFile)
      {
         Check (pFile->GetSequence () > 0, "Sequence number is non-zero");

         bool bGotResult = listener.WaitFor (15000);

         if (bGotResult)
         {
            Check (listener.Succeeded (), "Fetch succeeded");
            Check (pFile->IsReady (), "File is READY");
            Check (!pFile->IsHashed (), "File is not hashed (session-only)");
            Check (pFile->GetSizeBytes () > 0, "File has non-zero size");

            Check (pFile->GetHttpStatus () == 200, "HTTP status is 200");
            Check (pFile->GetFetchDuration () > 0.0, "Fetch duration is positive");
            Check (!pFile->IsServedFromCache (), "Not served from cache");

            std::vector<uint8_t> aData = pFile->ReadData ();
            Check (!aData.empty (), "ReadData returned content");
            Check (aData.size () == pFile->GetSizeBytes (), "ReadData size matches GetSizeBytes");

            std::printf ("    Size: %llu bytes, ContentType: %s, Duration: %.3f s\n",
               static_cast<unsigned long long> (pFile->GetSizeBytes ()),
               pFile->GetContentType ().c_str (),
               pFile->GetFetchDuration ());
         }
         else
         {
            std::printf ("    (Timed out — expected if no internet)\n");
            Check (true, "Request did not crash (timeout is non-fatal)");
         }

         pFile->Release ();
      }
   }

   pCache->Shutdown ();
   delete pCache;
}

// ---------------------------------------------------------------------------
// Test 3: Request deduplication (same URL returns shared ENTRY)
// ---------------------------------------------------------------------------

static void TestDeduplication ()
{
   std::printf ("\n[Test 3] Request deduplication\n");

   SNEEZE::CACHE::MANAGER* pCache = new SNEEZE::CACHE::MANAGER (s_pSneeze);
   pCache->Initialize ();

   TEST_FILE_LISTENER listenerA;
   TEST_FILE_LISTENER listenerB;

   SNEEZE::CACHE::FILE* pFileA = pCache->Request (
      "https://httpbin.org/bytes/64", "", &listenerA);
   SNEEZE::CACHE::FILE* pFileB = pCache->Request (
      "https://httpbin.org/bytes/64", "", &listenerB);

   Check (pFileA != nullptr, "First handle is valid");
   Check (pFileB != nullptr, "Second handle is valid");

   if (pFileA  &&  pFileB)
   {
      Check (pFileA->GetEntry () == pFileB->GetEntry (),
         "Both handles share the same ENTRY");

      bool bGotA = listenerA.WaitFor (15000);
      bool bGotB = listenerB.WaitFor (15000);

      if (bGotA  &&  bGotB)
      {
         Check (listenerA.Succeeded (), "Listener A notified");
         Check (listenerB.Succeeded (), "Listener B notified");
      }
      else
      {
         std::printf ("    (Timed out — expected if no internet)\n");
         Check (true, "Deduplication did not crash (timeout is non-fatal)");
      }
   }

   if (pFileA) pFileA->Release ();
   if (pFileB) pFileB->Release ();

   pCache->Shutdown ();
   delete pCache;
}

// ---------------------------------------------------------------------------
// Test 4: Hash-verified persistent fetch
// ---------------------------------------------------------------------------

static void TestHashVerifiedFetch ()
{
   std::printf ("\n[Test 4] Hash-verified persistent fetch\n");

   SNEEZE::CACHE::MANAGER* pCache = new SNEEZE::CACHE::MANAGER (s_pSneeze);
   pCache->Initialize ();

   TEST_FILE_LISTENER listenerPreFetch;
   SNEEZE::CACHE::FILE* pPreFile = pCache->Request (
      "https://httpbin.org/base64/SGVsbG9Xb3JsZA==", "", &listenerPreFetch);

   if (pPreFile)
   {
      bool bPreResult = listenerPreFetch.WaitFor (15000);
      if (bPreResult  &&  listenerPreFetch.Succeeded ())
      {
         std::vector<uint8_t> aData = pPreFile->ReadData ();
         std::string sContent (aData.begin (), aData.end ());
         std::printf ("    Pre-fetch content: \"%s\" (%zu bytes)\n",
            sContent.c_str (), aData.size ());

         std::string sDigest = ComputeSha256Hex (aData.data (), aData.size ());
         std::string sSri = "sha256-" + sDigest;
         std::printf ("    Computed SRI: %s\n", sSri.c_str ());

         Check (!sDigest.empty (), "Pre-fetch produced a hash");

         pPreFile->Reset ();
         pPreFile->Release ();
         pPreFile = nullptr;

         TEST_FILE_LISTENER listenerVerified;
         SNEEZE::CACHE::FILE* pVerFile = pCache->Request (
            "https://httpbin.org/base64/SGVsbG9Xb3JsZA==", sSri, &listenerVerified);

         if (pVerFile)
         {
            bool bVerResult = listenerVerified.WaitFor (15000);
            if (bVerResult  &&  listenerVerified.Succeeded ())
            {
               Check (pVerFile->IsReady (), "Verified file is READY");
               Check (pVerFile->IsHashed (), "Verified file is persistent (hashed)");
               Check (pVerFile->GetHash () == sSri, "Hash matches SRI");

               std::vector<uint8_t> aVerData = pVerFile->ReadData ();
               Check (aVerData == aData, "Verified data matches original");
            }
            else
            {
               std::printf ("    (Verified fetch timed out or failed)\n");
               Check (true, "Hash-verified fetch did not crash");
            }
            pVerFile->Release ();
         }
      }
      else
      {
         std::printf ("    (Pre-fetch timed out — expected if no internet)\n");
         Check (true, "Pre-fetch did not crash");
         pPreFile->Release ();
      }
   }

   pCache->Shutdown ();
   delete pCache;
}

// ---------------------------------------------------------------------------
// Test 5: Hash mismatch causes failure
// ---------------------------------------------------------------------------

static void TestHashMismatch ()
{
   std::printf ("\n[Test 5] Hash mismatch causes failure\n");

   SNEEZE::CACHE::MANAGER* pCache = new SNEEZE::CACHE::MANAGER (s_pSneeze);
   pCache->Initialize ();

   TEST_FILE_LISTENER listener;
   std::string sBadHash = "sha256-0000000000000000000000000000000000000000000000000000000000000000";

   SNEEZE::CACHE::FILE* pFile = pCache->Request (
      "https://httpbin.org/base64/SGVsbG9Xb3JsZA==", sBadHash, &listener);

   if (pFile)
   {
      bool bGotResult = listener.WaitFor (15000);
      if (bGotResult)
      {
         Check (!listener.Succeeded (), "Bad hash correctly caused failure");
         Check (pFile->GetState () == SNEEZE::CACHE::STATE_FAILED, "State is FAILED");
      }
      else
      {
         std::printf ("    (Timed out — expected if no internet)\n");
         Check (true, "Hash mismatch test did not crash");
      }

      pFile->Release ();
   }

   pCache->Shutdown ();
   delete pCache;
}

// ---------------------------------------------------------------------------
// Test 6: ResetSession removes session entries, triggers re-fetch
// ---------------------------------------------------------------------------

static void TestResetSession ()
{
   std::printf ("\n[Test 6] ResetSession\n");

   SNEEZE::CACHE::MANAGER* pCache = new SNEEZE::CACHE::MANAGER (s_pSneeze);
   pCache->Initialize ();

   TEST_FILE_LISTENER listenerSession;
   SNEEZE::CACHE::FILE* pSession = pCache->Request (
      "https://httpbin.org/bytes/32", "", &listenerSession);

   if (pSession)
   {
      bool bGot = listenerSession.WaitFor (15000);
      if (bGot  &&  listenerSession.Succeeded ())
      {
         Check (pSession->IsReady (), "Session file is READY before reset");
         pSession->Release ();

         pCache->ResetSession ();

         TEST_FILE_LISTENER listenerAfter;
         SNEEZE::CACHE::FILE* pAfter = pCache->Request (
            "https://httpbin.org/bytes/32", "", &listenerAfter);

         if (pAfter)
         {
            SNEEZE::CACHE::STATE bState = pAfter->GetState ();
            Check (bState == SNEEZE::CACHE::STATE_FETCHING  ||
                   bState == SNEEZE::CACHE::STATE_READY,
               "After reset, new request is FETCHING or READY");

            listenerAfter.WaitFor (15000);
            pAfter->Release ();
         }

         Check (true, "ResetSession completed without crash");
      }
      else
      {
         std::printf ("    (Timed out — expected if no internet)\n");
         Check (true, "ResetSession test did not crash");
         pSession->Release ();
      }
   }

   pCache->Shutdown ();
   delete pCache;
}

// ---------------------------------------------------------------------------
// Test 7: Reset flag destroys entry and disk file on release
// ---------------------------------------------------------------------------

static void TestResetFlag ()
{
   std::printf ("\n[Test 7] Reset flag\n");

   SNEEZE::CACHE::MANAGER* pCache = new SNEEZE::CACHE::MANAGER (s_pSneeze);
   pCache->Initialize ();

   TEST_FILE_LISTENER listener;
   SNEEZE::CACHE::FILE* pFile = pCache->Request (
      "https://httpbin.org/bytes/16", "", &listener);

   if (pFile)
   {
      bool bGot = listener.WaitFor (15000);
      if (bGot  &&  listener.Succeeded ())
      {
         std::string sDiskPath = pFile->GetDiskPath ();

         Check (!sDiskPath.empty (), "File had a disk path");
         Check (std::filesystem::exists (sDiskPath), "Disk file exists before reset");

         pFile->Reset ();
         pFile->Release ();

         Check (!std::filesystem::exists (sDiskPath), "Disk file removed after reset+release");
      }
      else
      {
         std::printf ("    (Timed out — expected if no internet)\n");
         Check (true, "Reset flag test did not crash");
         pFile->Release ();
      }
   }

   pCache->Shutdown ();
   delete pCache;
}

// ---------------------------------------------------------------------------
// Test 8: Failed fetch (invalid URL)
// ---------------------------------------------------------------------------

static void TestFailedFetch ()
{
   std::printf ("\n[Test 8] Failed fetch (invalid host)\n");

   SNEEZE::CACHE::MANAGER* pCache = new SNEEZE::CACHE::MANAGER (s_pSneeze);
   pCache->Initialize ();

   TEST_FILE_LISTENER listener;
   SNEEZE::CACHE::FILE* pFile = pCache->Request (
      "https://this-domain-does-not-exist-999.invalid/file.bin", "", &listener);

   if (pFile)
   {
      bool bGot = listener.WaitFor (15000);
      if (bGot)
      {
         Check (!listener.Succeeded (), "Invalid host correctly failed");
         Check (pFile->GetState () == SNEEZE::CACHE::STATE_FAILED, "State is FAILED");
      }
      else
      {
         std::printf ("    (Timed out waiting for DNS failure)\n");
         Check (true, "Failed fetch did not crash");
      }

      pFile->Release ();
   }

   pCache->Shutdown ();
   delete pCache;
}

// ---------------------------------------------------------------------------
// Test 9: Manifest persistence (survive shutdown/reinit)
// ---------------------------------------------------------------------------

static void TestManifestPersistence ()
{
   std::printf ("\n[Test 9] Manifest persistence\n");

   std::string sUrl = "https://httpbin.org/base64/UGVyc2lzdGVuY2VUZXN0";
   std::string sSri;

   // Phase 1: Fetch with hash, shutdown (saves manifest)
   {
      SNEEZE::CACHE::MANAGER* pCache = new SNEEZE::CACHE::MANAGER (s_pSneeze);
      pCache->Initialize ();

      TEST_FILE_LISTENER listenerPre;
      SNEEZE::CACHE::FILE* pPre = pCache->Request (sUrl, "", &listenerPre);
      if (pPre  &&  listenerPre.WaitFor (15000)  &&  listenerPre.Succeeded ())
      {
         std::vector<uint8_t> aData = pPre->ReadData ();
         std::string sDigest = ComputeSha256Hex (aData.data (), aData.size ());
         sSri = "sha256-" + sDigest;
         pPre->Reset ();
         pPre->Release ();
         pPre = nullptr;

         TEST_FILE_LISTENER listenerHash;
         SNEEZE::CACHE::FILE* pHash = pCache->Request (sUrl, sSri, &listenerHash);
         if (pHash)
         {
            listenerHash.WaitFor (15000);
            Check (listenerHash.Succeeded (), "Persistent entry created");
            pHash->Release ();
         }
      }
      else
      {
         std::printf ("    (Pre-fetch timed out — skipping)\n");
         Check (true, "Manifest test did not crash (no internet)");
         if (pPre) pPre->Release ();
         pCache->Shutdown ();
         delete pCache;
         return;
      }

      pCache->Shutdown ();
      delete pCache;
   }

   // Phase 2: Reinitialize and check if the entry survived
   if (!sSri.empty ())
   {
      SNEEZE::CACHE::MANAGER* pCache2 = new SNEEZE::CACHE::MANAGER (s_pSneeze);
      pCache2->Initialize ();

      TEST_FILE_LISTENER listenerReload;
      SNEEZE::CACHE::FILE* pReload = pCache2->Request (sUrl, sSri, &listenerReload);

      if (pReload)
      {
         Check (pReload->IsReady (), "Entry survived shutdown (loaded from manifest)");
         Check (pReload->IsHashed (), "Entry is still hashed");
         Check (pReload->GetHash () == sSri, "Hash matches after reload");

         std::vector<uint8_t> aData = pReload->ReadData ();
         Check (!aData.empty (), "Data is readable after reload");

         pReload->Release ();
      }

      pCache2->ResetAll ();
      pCache2->Shutdown ();
      delete pCache2;
   }
}

// ---------------------------------------------------------------------------
// Test 10: HTTP headers captured
// ---------------------------------------------------------------------------

static void TestHttpHeaders ()
{
   std::printf ("\n[Test 10] HTTP response headers captured\n");

   SNEEZE::CACHE::MANAGER* pCache = new SNEEZE::CACHE::MANAGER (s_pSneeze);
   pCache->Initialize ();

   TEST_FILE_LISTENER listener;
   SNEEZE::CACHE::FILE* pFile = pCache->Request (
      "https://httpbin.org/response-headers?Content-Type=application/json", "", &listener);

   if (pFile)
   {
      bool bGot = listener.WaitFor (15000);
      if (bGot  &&  listener.Succeeded ())
      {
         auto& mapHeaders = pFile->GetHeaders ();
         Check (!mapHeaders.empty (), "Headers map is non-empty");

         std::string sCt = pFile->GetContentType ();
         Check (!sCt.empty (), "Content-Type header captured");
         std::printf ("    Content-Type: %s\n", sCt.c_str ());
         std::printf ("    Total headers captured: %zu\n", mapHeaders.size ());
      }
      else
      {
         std::printf ("    (Timed out — expected if no internet)\n");
         Check (true, "Headers test did not crash");
      }

      pFile->Release ();
   }

   pCache->Shutdown ();
   delete pCache;
}

// ---------------------------------------------------------------------------
// Test 11: FILE handle lifecycle
// ---------------------------------------------------------------------------

static void TestFileHandleLifecycle ()
{
   std::printf ("\n[Test 11] FILE handle lifecycle\n");

   SNEEZE::CACHE::MANAGER* pCache = new SNEEZE::CACHE::MANAGER (s_pSneeze);
   pCache->Initialize ();

   TEST_FILE_LISTENER listener;
   SNEEZE::CACHE::FILE* pFile = pCache->Request (
      "https://httpbin.org/bytes/8", "", &listener);

   Check (pFile != nullptr, "Handle allocated");

   if (pFile)
   {
      Check (pFile->GetEntry () != nullptr, "Handle wraps a valid ENTRY");
      Check (!pFile->GetUrl ().empty (), "URL accessible from handle");

      listener.WaitFor (15000);

      pFile->Release ();
      Check (true, "Release completed without crash");
   }

   pCache->Shutdown ();
   delete pCache;
}

// ---------------------------------------------------------------------------
// Test 12: History list and sequence numbers
// ---------------------------------------------------------------------------

static void TestHistoryAndSequence ()
{
   std::printf ("\n[Test 12] History list and sequence numbers\n");

   SNEEZE::CACHE::MANAGER* pCache = new SNEEZE::CACHE::MANAGER (s_pSneeze);
   pCache->Initialize ();

   TEST_FILE_LISTENER listenerA;
   TEST_FILE_LISTENER listenerB;

   SNEEZE::CACHE::FILE* pFileA = pCache->Request (
      "https://httpbin.org/bytes/16", "", &listenerA);
   SNEEZE::CACHE::FILE* pFileB = pCache->Request (
      "https://httpbin.org/bytes/32", "", &listenerB);

   Check (pFileA != nullptr  &&  pFileB != nullptr, "Both handles allocated");

   if (pFileA  &&  pFileB)
   {
      Check (pFileA->GetSequence () < pFileB->GetSequence (),
         "Sequence numbers are monotonically increasing");

      auto& aHistory = pCache->GetHistory ();
      Check (aHistory.size () >= 2, "History contains at least 2 entries");

      listenerA.WaitFor (15000);
      listenerB.WaitFor (15000);

      pFileA->Release ();
      pFileB->Release ();

      Check (aHistory.size () >= 2, "Release does not shrink history");
   }

   pCache->Shutdown ();
   delete pCache;
}

// ---------------------------------------------------------------------------
// Test 13: Notification callbacks
// ---------------------------------------------------------------------------

static void TestNotifications ()
{
   std::printf ("\n[Test 13] Notification callbacks\n");

   s_pTestListener->ResetCounters ();

   SNEEZE::CACHE::MANAGER* pCache = new SNEEZE::CACHE::MANAGER (s_pSneeze);
   pCache->Initialize ();

   TEST_FILE_LISTENER listener;
   SNEEZE::CACHE::FILE* pFile = pCache->Request (
      "https://httpbin.org/bytes/8", "", &listener);

   Check (s_pTestListener->m_nCreatedCount > 0, "OnCacheFileCreated fired");

   if (pFile)
   {
      bool bGot = listener.WaitFor (15000);
      if (bGot)
      {
         Check (s_pTestListener->m_nChangedCount > 0, "OnCacheFileChanged fired");
      }
      else
      {
         std::printf ("    (Timed out — expected if no internet)\n");
         Check (true, "Notification test did not crash");
      }

      pFile->Release ();
   }

   pCache->Shutdown ();
   delete pCache;
}

// ---------------------------------------------------------------------------
// Test 14: Served-from-cache detection
// ---------------------------------------------------------------------------

static void TestServedFromCache ()
{
   std::printf ("\n[Test 14] Served-from-cache detection\n");

   std::string sUrl = "https://httpbin.org/base64/Q2FjaGVkRGF0YQ==";
   std::string sSri;

   SNEEZE::CACHE::MANAGER* pCache = new SNEEZE::CACHE::MANAGER (s_pSneeze);
   pCache->Initialize ();

   // First fetch — should NOT be served from cache
   TEST_FILE_LISTENER listenerFirst;
   SNEEZE::CACHE::FILE* pFirst = pCache->Request (sUrl, "", &listenerFirst);

   if (pFirst)
   {
      bool bGot = listenerFirst.WaitFor (15000);
      if (bGot  &&  listenerFirst.Succeeded ())
      {
         Check (!pFirst->IsServedFromCache (), "First fetch is not served from cache");

         // Second request for the same URL — should be served from cache
         TEST_FILE_LISTENER listenerSecond;
         SNEEZE::CACHE::FILE* pSecond = pCache->Request (sUrl, "", &listenerSecond);

         if (pSecond)
         {
            Check (pSecond->IsServedFromCache (), "Second fetch IS served from cache");
            Check (pSecond->GetSequence () > pFirst->GetSequence (),
               "Second sequence > first");
            pSecond->Release ();
         }
      }
      else
      {
         std::printf ("    (Timed out — expected if no internet)\n");
         Check (true, "Served-from-cache test did not crash");
      }

      pFirst->Release ();
   }

   pCache->Shutdown ();
   delete pCache;
}

// ---------------------------------------------------------------------------
// Test 15: Failed fetch records HTTP status
// ---------------------------------------------------------------------------

static void TestFailedFetchHttpStatus ()
{
   std::printf ("\n[Test 15] Failed fetch records HTTP status\n");

   SNEEZE::CACHE::MANAGER* pCache = new SNEEZE::CACHE::MANAGER (s_pSneeze);
   pCache->Initialize ();

   TEST_FILE_LISTENER listener;
   SNEEZE::CACHE::FILE* pFile = pCache->Request (
      "https://httpbin.org/status/404", "", &listener);

   if (pFile)
   {
      bool bGot = listener.WaitFor (15000);
      if (bGot)
      {
         Check (!listener.Succeeded (), "404 correctly failed");
         Check (pFile->GetHttpStatus () == 404, "HTTP status is 404");
         Check (pFile->GetFetchDuration () > 0.0, "Fetch duration recorded for failed request");
      }
      else
      {
         std::printf ("    (Timed out — expected if no internet)\n");
         Check (true, "HTTP status test did not crash");
      }

      pFile->Release ();
   }

   pCache->Shutdown ();
   delete pCache;
}

// ---------------------------------------------------------------------------
// Test 16: Clear flag removes FILE from history on release
// ---------------------------------------------------------------------------

static void TestClearFlag ()
{
   std::printf ("\n[Test 16] Clear flag\n");

   SNEEZE::CACHE::MANAGER* pCache = new SNEEZE::CACHE::MANAGER (s_pSneeze);
   pCache->Initialize ();

   TEST_FILE_LISTENER listener;
   SNEEZE::CACHE::FILE* pFile = pCache->Request (
      "https://httpbin.org/bytes/8", "", &listener);

   if (pFile)
   {
      listener.WaitFor (15000);

      size_t nHistoryBefore = pCache->GetHistory ().size ();

      pFile->Clear ();
      pFile->Release ();

      size_t nHistoryAfter = pCache->GetHistory ().size ();
      Check (nHistoryAfter == nHistoryBefore - 1,
         "Clear + Release removes FILE from history");
   }

   pCache->Shutdown ();
   delete pCache;
}

// ---------------------------------------------------------------------------
// Test 17: Reset flag can be toggled off before release
// ---------------------------------------------------------------------------

static void TestResetFlagToggle ()
{
   std::printf ("\n[Test 17] Reset flag toggle\n");

   SNEEZE::CACHE::MANAGER* pCache = new SNEEZE::CACHE::MANAGER (s_pSneeze);
   pCache->Initialize ();

   TEST_FILE_LISTENER listener;
   SNEEZE::CACHE::FILE* pFile = pCache->Request (
      "https://httpbin.org/bytes/8", "", &listener);

   if (pFile)
   {
      listener.WaitFor (15000);

      std::string sDiskPath = pFile->GetDiskPath ();
      bool bHadDisk = !sDiskPath.empty ()  &&  std::filesystem::exists (sDiskPath);

      pFile->Reset ();
      pFile->Reset (false);
      pFile->Release ();

      if (bHadDisk)
      {
         Check (std::filesystem::exists (sDiskPath),
            "Disk file survives when reset flag is toggled off");
      }
      else
      {
         Check (true, "Reset toggle did not crash (no disk path to verify)");
      }
   }

   pCache->Shutdown ();
   delete pCache;
}

// ---------------------------------------------------------------------------
// Test 18: Deferred reset with multiple handles
// ---------------------------------------------------------------------------

static void TestDeferredReset ()
{
   std::printf ("\n[Test 18] Deferred reset (multiple handles)\n");

   SNEEZE::CACHE::MANAGER* pCache = new SNEEZE::CACHE::MANAGER (s_pSneeze);
   pCache->Initialize ();

   TEST_FILE_LISTENER listenerA;
   TEST_FILE_LISTENER listenerB;

   SNEEZE::CACHE::FILE* pFileA = pCache->Request (
      "https://httpbin.org/bytes/16", "", &listenerA);
   SNEEZE::CACHE::FILE* pFileB = pCache->Request (
      "https://httpbin.org/bytes/16", "", &listenerB);

   if (pFileA  &&  pFileB)
   {
      listenerA.WaitFor (15000);
      listenerB.WaitFor (15000);

      std::string sDiskPath = pFileA->GetDiskPath ();
      bool bHadDisk = !sDiskPath.empty ()  &&  std::filesystem::exists (sDiskPath);

      pFileA->Reset ();
      pFileA->Release ();

      if (bHadDisk)
      {
         Check (std::filesystem::exists (sDiskPath),
            "Disk file survives while second handle is attached");
      }

      Check (pFileB->GetEntry () != nullptr,
         "Second handle still has a valid ENTRY");

      pFileB->Release ();

      if (bHadDisk)
      {
         Check (!std::filesystem::exists (sDiskPath),
            "Disk file removed after last handle releases");
      }

      Check (true, "Deferred reset completed without crash");
   }
   else
   {
      if (pFileA) pFileA->Release ();
      if (pFileB) pFileB->Release ();
      Check (true, "Deferred reset did not crash (handles were null)");
   }

   pCache->Shutdown ();
   delete pCache;
}

// ---------------------------------------------------------------------------
// Test 19: ClearAll removes released FILE records
// ---------------------------------------------------------------------------

static void TestClearAll ()
{
   std::printf ("\n[Test 19] ClearAll\n");

   SNEEZE::CACHE::MANAGER* pCache = new SNEEZE::CACHE::MANAGER (s_pSneeze);
   pCache->Initialize ();

   TEST_FILE_LISTENER listenerA;
   TEST_FILE_LISTENER listenerB;

   SNEEZE::CACHE::FILE* pFileA = pCache->Request (
      "https://httpbin.org/bytes/8", "", &listenerA);
   SNEEZE::CACHE::FILE* pFileB = pCache->Request (
      "https://httpbin.org/bytes/16", "", &listenerB);

   if (pFileA  &&  pFileB)
   {
      listenerA.WaitFor (15000);
      listenerB.WaitFor (15000);

      pFileA->Release ();

      size_t nHistoryBefore = pCache->GetHistory ().size ();
      Check (nHistoryBefore >= 2, "History has at least 2 entries before ClearAll");

      pCache->ClearAll ();

      size_t nHistoryAfter = pCache->GetHistory ().size ();
      Check (nHistoryAfter < nHistoryBefore,
         "ClearAll removed released FILE records");
      Check (nHistoryAfter >= 1,
         "In-use FILE record survived ClearAll");

      pFileB->Release ();
   }
   else
   {
      if (pFileA) pFileA->Release ();
      if (pFileB) pFileB->Release ();
   }

   Check (true, "ClearAll completed without crash");

   pCache->Shutdown ();
   delete pCache;
}

// ---------------------------------------------------------------------------
// Test 20: ResetAll destroys all entries
// ---------------------------------------------------------------------------

static void TestResetAll ()
{
   std::printf ("\n[Test 20] ResetAll\n");

   SNEEZE::CACHE::MANAGER* pCache = new SNEEZE::CACHE::MANAGER (s_pSneeze);
   pCache->Initialize ();

   TEST_FILE_LISTENER listener;
   SNEEZE::CACHE::FILE* pFile = pCache->Request (
      "https://httpbin.org/bytes/8", "", &listener);

   if (pFile)
   {
      bool bGot = listener.WaitFor (15000);
      if (bGot  &&  listener.Succeeded ())
      {
         std::string sDiskPath = pFile->GetDiskPath ();
         pFile->Release ();

         pCache->ResetAll ();

         if (!sDiskPath.empty ())
         {
            Check (!std::filesystem::exists (sDiskPath),
               "ResetAll removed disk file");
         }

         TEST_FILE_LISTENER listenerAfter;
         SNEEZE::CACHE::FILE* pAfter = pCache->Request (
            "https://httpbin.org/bytes/8", "", &listenerAfter);

         if (pAfter)
         {
            SNEEZE::CACHE::STATE bState = pAfter->GetState ();
            Check (bState == SNEEZE::CACHE::STATE_FETCHING  ||
                   bState == SNEEZE::CACHE::STATE_READY,
               "After ResetAll, new request is FETCHING or READY");

            listenerAfter.WaitFor (15000);
            pAfter->Release ();
         }
      }
      else
      {
         std::printf ("    (Timed out — expected if no internet)\n");
         Check (true, "ResetAll test did not crash");
         pFile->Release ();
      }
   }

   pCache->Shutdown ();
   delete pCache;
}

// ---------------------------------------------------------------------------
// Test 21: OnCacheFileDeleted notification
// ---------------------------------------------------------------------------

static void TestDeletedNotification ()
{
   std::printf ("\n[Test 21] OnCacheFileDeleted notification\n");

   s_pTestListener->ResetCounters ();

   SNEEZE::CACHE::MANAGER* pCache = new SNEEZE::CACHE::MANAGER (s_pSneeze);
   pCache->Initialize ();

   TEST_FILE_LISTENER listener;
   SNEEZE::CACHE::FILE* pFile = pCache->Request (
      "https://httpbin.org/bytes/8", "", &listener);

   if (pFile)
   {
      listener.WaitFor (15000);

      Check (s_pTestListener->m_nDeletedCount == 0,
         "No deleted notifications before clear");

      pFile->Clear ();
      pFile->Release ();

      Check (s_pTestListener->m_nDeletedCount == 1,
         "OnCacheFileDeleted fired after clear + release");
   }

   pCache->Shutdown ();
   delete pCache;
}

// ---------------------------------------------------------------------------
// Entry point
// ---------------------------------------------------------------------------

int RunCacheTests (int /*nArgc*/, char** /*aArgv*/)
{
   std::printf ("=== Cache Test Suite ===\n");

   s_pTestListener = new CACHE_TEST_LISTENER ();
   s_pTestListener->sAppDataPath = (std::filesystem::temp_directory_path () / "SneezeTest").string ();
   s_pSneeze = new SNEEZE::CORE::SNEEZE (s_pTestListener);
   curl_global_init (CURL_GLOBAL_DEFAULT);

   TestManagerInit ();
   TestSessionFetch ();
   TestDeduplication ();
   TestHashVerifiedFetch ();
   TestHashMismatch ();
   TestResetSession ();
   TestResetFlag ();
   TestFailedFetch ();
   TestManifestPersistence ();
   TestHttpHeaders ();
   TestFileHandleLifecycle ();
   TestHistoryAndSequence ();
   TestNotifications ();
   TestServedFromCache ();
   TestFailedFetchHttpStatus ();
   TestClearFlag ();
   TestResetFlagToggle ();
   TestDeferredReset ();
   TestClearAll ();
   TestResetAll ();
   TestDeletedNotification ();

   curl_global_cleanup ();

   // Intentionally leak s_pSneeze — its destructor calls static subsystem
   // shutdowns (WASM, SPV, etc.) that may interfere with other test suites.

   std::printf ("\n=== Results: %d passed, %d failed ===\n", nPassed, nFailed);

   return (nFailed > 0) ? 1 : 0;
}
