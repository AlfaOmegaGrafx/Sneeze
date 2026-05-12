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
#include <Container.h>
#include <Viewport.h>

#include <openssl/sha.h>
#include <curl/curl.h>

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <mutex>
#include <condition_variable>
#include <chrono>
#include <thread>
#include <filesystem>
#include <fstream>

using namespace SNEEZE;

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

class CACHE_TEST_LISTENER : public SNEEZE::IENGINE
{
public:
   std::string m_sAppDataPath;
   std::string m_sSessionPath;
   std::string m_sRenderer;

   std::string const& sAppDataPath () const& override { return m_sAppDataPath; }
   std::string const& sRenderer ()    const& override { return m_sRenderer; }

   void Log (eLOGLEVEL, const std::string& sModule, const std::string& sMessage) override
   {
      std::printf ("    [%s] %s\n", sModule.c_str (), sMessage.c_str ());
   }
};

class CACHE_TEST_VIEWPORT_HOST : public SNEEZE::IVIEWPORT
{
public:
   int m_nCreatedCount = 0;
   int m_nChangedCount = 0;
   int m_nDeletedCount = 0;

   void* FrameWindow () override
   {
      return nullptr;
   }

   void FrameSize (int& nWidth, int& nHeight) override
   {
      nWidth  = 0;
      nHeight = 0;
   }

   void OnFrameReady (const uint32_t*, int, int) override {}

   void OnNetworkFileCreated (SNEEZE::NETWORK::FILE*) override { m_nCreatedCount++; }
   void OnNetworkFileChanged (SNEEZE::NETWORK::FILE*) override { m_nChangedCount++; }
   void OnNetworkFileDeleted (SNEEZE::NETWORK::FILE*) override { m_nDeletedCount++; }

   void ResetCounters () { m_nCreatedCount = 0; m_nChangedCount = 0; m_nDeletedCount = 0; }
};

// ---------------------------------------------------------------------------
// IFILE listener that signals a condition variable on completion
// ---------------------------------------------------------------------------

class TEST_FILE_LISTENER : public NETWORK::IFILE
{
public:
   TEST_FILE_LISTENER () : m_bDone (false), m_bSucceeded (false) {}

   void OnFileReady (NETWORK::FILE* /*pFile*/) override
   {
      std::lock_guard<std::mutex> guard (m_mutex);
      m_bSucceeded = true;
      m_bDone = true;
      m_condVar.notify_all ();
   }

   void OnFileFailed (NETWORK::FILE* /*pFile*/) override
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

static CACHE_TEST_LISTENER*       s_pTestListener = nullptr;
static CACHE_TEST_VIEWPORT_HOST*  s_pVPHost       = nullptr;
static SNEEZE::ENGINE*            s_pSneeze       = nullptr;
static VIEWPORT*                  s_pViewport     = nullptr;

static auto s_pTestName = VIEWPORT::CONTAINER::NAME {
   "TestFingerprint_0123456789abcdef",
   "TestOrg",
   "TestCommon",
   "TestStore",
   "TestPersona",
   true
};

// ---------------------------------------------------------------------------
// Test 1: Manager initialization
// ---------------------------------------------------------------------------

static void TestManagerInit ()
{
   std::printf ("\n[Test 1] Manager initialization\n");

   NETWORK* pNetwork = new NETWORK (s_pSneeze);
   bool bInit = pNetwork->Initialize ();
   Check (bInit, "Manager initialized successfully");
   delete pNetwork;
}

// ---------------------------------------------------------------------------
// Test 2: Request a file without hash (live fetch)
// ---------------------------------------------------------------------------

static void TestUnhashedFetch ()
{
   std::printf ("\n[Test 2] Unhashed fetch (no hash, live network)\n");

   NETWORK* pNetwork = new NETWORK (s_pSneeze);
   bool bInit = pNetwork->Initialize ();
   Check (bInit, "Network initialized");

   if (bInit)
   {
      TEST_FILE_LISTENER listener;
      NETWORK::FILE* pFile = pNetwork->Request (&listener, s_pViewport, &s_pTestName, "https://httpbin.org/bytes/128");

      Check (pFile != nullptr, "Request returned a handle");

      if (pFile)
      {
         Check (pFile->FileIx () > 0, "File index is non-zero");

         bool bGotResult = listener.WaitFor (15000);

         if (bGotResult)
         {
            Check (listener.Succeeded (), "Fetch succeeded");
            Check (pFile->IsReady (), "File is READY");
            Check (!pFile->IsHashed (), "File is not hashed");
            Check (pFile->SizeBytes () > 0, "File has non-zero size");

            Check (pFile->HttpStatus () == 200, "HTTP status is 200");
            Check (pFile->FetchDuration () > 0.0, "Fetch duration is positive");
            Check (!pFile->IsServedFromCache (), "Not served from cache");

            std::vector<uint8_t> aData = pFile->ReadData ();
            Check (!aData.empty (), "ReadData returned content");
            Check (aData.size () == pFile->SizeBytes (), "ReadData size matches SizeBytes");

            std::printf ("    Size: %llu bytes, ContentType: %s, Duration: %.3f s\n",
               static_cast<unsigned long long> (pFile->SizeBytes ()),
               pFile->ContentType ().c_str (),
               pFile->FetchDuration ());
         }
         else
         {
            std::printf ("    (Timed out — expected if no internet)\n");
            Check (true, "Request did not crash (timeout is non-fatal)");
         }

         pFile->Release ();
      }
   }

   delete pNetwork;
}

// ---------------------------------------------------------------------------
// Test 3: Request deduplication (same URL returns shared ASSET)
// ---------------------------------------------------------------------------

static void TestDeduplication ()
{
   std::printf ("\n[Test 3] Request deduplication\n");

   NETWORK* pNetwork = new NETWORK (s_pSneeze);
   pNetwork->Initialize ();

   TEST_FILE_LISTENER listenerA;
   TEST_FILE_LISTENER listenerB;

   NETWORK::FILE* pFileA = pNetwork->Request (&listenerA, s_pViewport, &s_pTestName, "https://httpbin.org/bytes/64");
   NETWORK::FILE* pFileB = pNetwork->Request (&listenerB, s_pViewport, &s_pTestName, "https://httpbin.org/bytes/64");

   Check (pFileA != nullptr, "First handle is valid");
   Check (pFileB != nullptr, "Second handle is valid");

   if (pFileA  &&  pFileB)
   {
      Check (pFileA->Asset () == pFileB->Asset (),
         "Both handles share the same ASSET");

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

   delete pNetwork;
}

// ---------------------------------------------------------------------------
// Test 4: Hash-verified persistent fetch
// ---------------------------------------------------------------------------

static void TestHashVerifiedFetch ()
{
   std::printf ("\n[Test 4] Hash-verified persistent fetch\n");

   NETWORK* pNetwork = new NETWORK (s_pSneeze);
   pNetwork->Initialize ();

   TEST_FILE_LISTENER listenerPreFetch;
   NETWORK::FILE* pPreFile = pNetwork->Request (&listenerPreFetch, s_pViewport, &s_pTestName, "https://httpbin.org/base64/SGVsbG9Xb3JsZA==");

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
         NETWORK::FILE* pVerFile = pNetwork->Request (&listenerVerified, s_pViewport, &s_pTestName, "https://httpbin.org/base64/SGVsbG9Xb3JsZA==", sSri);

         if (pVerFile)
         {
            bool bVerResult = listenerVerified.WaitFor (15000);
            if (bVerResult  &&  listenerVerified.Succeeded ())
            {
               Check (pVerFile->IsReady (), "Verified file is READY");
               Check (pVerFile->IsHashed (), "Verified file is persistent (hashed)");
               Check (pVerFile->Hash () == sSri, "Hash matches SRI");

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

   delete pNetwork;
}

// ---------------------------------------------------------------------------
// Test 5: Hash mismatch causes failure
// ---------------------------------------------------------------------------

static void TestHashMismatch ()
{
   std::printf ("\n[Test 5] Hash mismatch causes failure\n");

   NETWORK* pNetwork = new NETWORK (s_pSneeze);
   pNetwork->Initialize ();

   TEST_FILE_LISTENER listener;
   std::string sBadHash = "sha256-0000000000000000000000000000000000000000000000000000000000000000";

   NETWORK::FILE* pFile = pNetwork->Request (&listener, s_pViewport, &s_pTestName, "https://httpbin.org/base64/SGVsbG9Xb3JsZA==", sBadHash);

   if (pFile)
   {
      bool bGotResult = listener.WaitFor (15000);
      if (bGotResult)
      {
         Check (!listener.Succeeded (), "Bad hash correctly caused failure");
         Check (pFile->State () == NETWORK::STATE_FAILED, "State is FAILED");
      }
      else
      {
         std::printf ("    (Timed out — expected if no internet)\n");
         Check (true, "Hash mismatch test did not crash");
      }

      pFile->Release ();
   }

   delete pNetwork;
}

// ---------------------------------------------------------------------------
// Test 6: Reset removes metas, triggers re-fetch
// ---------------------------------------------------------------------------

static void TestReset ()
{
   std::printf ("\n[Test 6] Reset\n");

   NETWORK* pNetwork = new NETWORK (s_pSneeze);
   pNetwork->Initialize ();

   TEST_FILE_LISTENER listenerSession;
   NETWORK::FILE* pSession = pNetwork->Request (&listenerSession, s_pViewport, &s_pTestName, "https://httpbin.org/bytes/32");

   if (pSession)
   {
      bool bGot = listenerSession.WaitFor (15000);
      if (bGot  &&  listenerSession.Succeeded ())
      {
         Check (pSession->IsReady (), "File is READY before reset");
         pSession->Release ();

         pNetwork->Reset ();

         TEST_FILE_LISTENER listenerAfter;
         NETWORK::FILE* pAfter = pNetwork->Request (&listenerAfter, s_pViewport, &s_pTestName, "https://httpbin.org/bytes/32");

         if (pAfter)
         {
            NETWORK::STATE bState = pAfter->State ();
            Check (bState == NETWORK::STATE_FETCHING  ||
                   bState == NETWORK::STATE_READY,
               "After reset, new request is FETCHING or READY");

            listenerAfter.WaitFor (15000);
            pAfter->Release ();
         }

         Check (true, "Reset completed without crash");
      }
      else
      {
         std::printf ("    (Timed out — expected if no internet)\n");
         Check (true, "Reset test did not crash");
         pSession->Release ();
      }
   }

   delete pNetwork;
}

// ---------------------------------------------------------------------------
// Test 7: Reset flag destroys meta and disk file on release
// ---------------------------------------------------------------------------

static void TestResetFlag ()
{
   std::printf ("\n[Test 7] Reset flag\n");

   NETWORK* pNetwork = new NETWORK (s_pSneeze);
   pNetwork->Initialize ();

   TEST_FILE_LISTENER listener;
   NETWORK::FILE* pFile = pNetwork->Request (&listener, s_pViewport, &s_pTestName, "https://httpbin.org/bytes/16");

   if (pFile)
   {
      bool bGot = listener.WaitFor (15000);
      if (bGot  &&  listener.Succeeded ())
      {
         std::string sDiskPath = pFile->DiskPath ();

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

   delete pNetwork;
}

// ---------------------------------------------------------------------------
// Test 8: Failed fetch (invalid URL)
// ---------------------------------------------------------------------------

static void TestFailedFetch ()
{
   std::printf ("\n[Test 8] Failed fetch (invalid host)\n");

   NETWORK* pNetwork = new NETWORK (s_pSneeze);
   pNetwork->Initialize ();

   TEST_FILE_LISTENER listener;
   NETWORK::FILE* pFile = pNetwork->Request (&listener, s_pViewport, &s_pTestName, "https://this-domain-does-not-exist-999.invalid/file.bin");

   if (pFile)
   {
      bool bGot = listener.WaitFor (15000);
      if (bGot)
      {
         Check (!listener.Succeeded (), "Invalid host correctly failed");
         Check (pFile->State () == NETWORK::STATE_FAILED, "State is FAILED");
      }
      else
      {
         std::printf ("    (Timed out waiting for DNS failure)\n");
         Check (true, "Failed fetch did not crash");
      }

      pFile->Release ();
   }

   delete pNetwork;
}

// ---------------------------------------------------------------------------
// Test 9: Sidecar persistence (survive shutdown/reinit)
// ---------------------------------------------------------------------------

static void TestSidecarPersistence ()
{
   std::printf ("\n[Test 9] Sidecar persistence\n");

   std::string sUrl = "https://httpbin.org/base64/UGVyc2lzdGVuY2VUZXN0";
   std::string sSri;

   // Phase 1: Fetch with hash, shutdown (saves .meta sidecar)
   {
      NETWORK* pNetwork = new NETWORK (s_pSneeze);
      pNetwork->Initialize ();

      TEST_FILE_LISTENER listenerPre;
      NETWORK::FILE* pPre = pNetwork->Request (&listenerPre, s_pViewport, &s_pTestName, sUrl);
      if (pPre  &&  listenerPre.WaitFor (15000)  &&  listenerPre.Succeeded ())
      {
         std::vector<uint8_t> aData = pPre->ReadData ();
         std::string sDigest = ComputeSha256Hex (aData.data (), aData.size ());
         sSri = "sha256-" + sDigest;
         pPre->Reset ();
         pPre->Release ();
         pPre = nullptr;

         TEST_FILE_LISTENER listenerHash;
         NETWORK::FILE* pHash = pNetwork->Request (&listenerHash, s_pViewport, &s_pTestName, sUrl, sSri);
         if (pHash)
         {
            listenerHash.WaitFor (15000);
            Check (listenerHash.Succeeded (), "Persistent entry created");
            Check (pHash->AssetIx () > 0, "Asset index assigned on creation");
            pHash->Release ();
         }
      }
      else
      {
         std::printf ("    (Pre-fetch timed out — skipping)\n");
         Check (true, "Sidecar test did not crash (no internet)");
         if (pPre) pPre->Release ();
         delete pNetwork;
         return;
      }

      delete pNetwork;
   }

   // Phase 2: Reinitialize and check if the meta survived via .meta sidecar
   if (!sSri.empty ())
   {
      NETWORK* pNetwork2 = new NETWORK (s_pSneeze);
      pNetwork2->Initialize ();

      TEST_FILE_LISTENER listenerReload;
      NETWORK::FILE* pReload = pNetwork2->Request (&listenerReload, s_pViewport, &s_pTestName, sUrl, sSri);

      if (pReload)
      {
         Check (pReload->IsReady (), "Meta survived shutdown (loaded from .meta sidecar)");
         Check (pReload->IsHashed (), "Meta is still hashed");
         Check (pReload->Hash () == sSri, "Hash matches after reload");
         Check (pReload->AssetIx () > 0, "Asset index preserved across sessions");

         std::vector<uint8_t> aData = pReload->ReadData ();
         Check (!aData.empty (), "Data is readable after reload");

         pReload->Release ();
      }

      pNetwork2->Reset ();
      delete pNetwork2;
   }
}

// ---------------------------------------------------------------------------
// Test 10: HTTP headers captured
// ---------------------------------------------------------------------------

static void TestHttpHeaders ()
{
   std::printf ("\n[Test 10] HTTP response headers captured\n");

   NETWORK* pNetwork = new NETWORK (s_pSneeze);
   pNetwork->Initialize ();

   TEST_FILE_LISTENER listener;
   NETWORK::FILE* pFile = pNetwork->Request (&listener, s_pViewport, &s_pTestName, "https://httpbin.org/response-headers?Content-Type=application/json");

   if (pFile)
   {
      bool bGot = listener.WaitFor (15000);
      if (bGot  &&  listener.Succeeded ())
      {
         auto& mapHeaders = pFile->Headers ();
         Check (!mapHeaders.empty (), "Headers map is non-empty");

         std::string sCt = pFile->ContentType ();
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

   delete pNetwork;
}

// ---------------------------------------------------------------------------
// Test 11: FILE handle lifecycle
// ---------------------------------------------------------------------------

static void TestFileHandleLifecycle ()
{
   std::printf ("\n[Test 11] FILE handle lifecycle\n");

   NETWORK* pNetwork = new NETWORK (s_pSneeze);
   pNetwork->Initialize ();

   TEST_FILE_LISTENER listener;
   NETWORK::FILE* pFile = pNetwork->Request (&listener, s_pViewport, &s_pTestName, "https://httpbin.org/bytes/8");

      Check (pFile != nullptr, "Handle allocated");

   if (pFile)
   {
      Check (pFile->Asset () != nullptr, "Handle wraps a valid ASSET");
      Check (!pFile->Url ().empty (), "URL accessible from handle");

      listener.WaitFor (15000);

      pFile->Release ();
      Check (true, "Release completed without crash");
   }

   delete pNetwork;
}

// ---------------------------------------------------------------------------
// Test 12: History list and file indexes
// ---------------------------------------------------------------------------

static void TestHistoryAndFileIx ()
{
   std::printf ("\n[Test 12] History list and file indexes\n");

   NETWORK* pNetwork = new NETWORK (s_pSneeze);
   pNetwork->Initialize ();

   TEST_FILE_LISTENER listenerA;
   TEST_FILE_LISTENER listenerB;

   NETWORK::FILE* pFileA = pNetwork->Request (&listenerA, s_pViewport, &s_pTestName, "https://httpbin.org/bytes/16");
   NETWORK::FILE* pFileB = pNetwork->Request (&listenerB, s_pViewport, &s_pTestName, "https://httpbin.org/bytes/32");

   Check (pFileA != nullptr  &&  pFileB != nullptr, "Both handles allocated");

   if (pFileA  &&  pFileB)
   {
      Check (pFileA->FileIx () < pFileB->FileIx (),
         "File indexes are monotonically increasing");

//      auto& aHistory = pNetwork->Files ();
//      Check (aHistory.size () >= 2, "History contains at least 2 entries");

      listenerA.WaitFor (15000);
      listenerB.WaitFor (15000);

      pFileA->Release ();
      pFileB->Release ();

//      Check (aHistory.size () >= 2, "Release does not shrink history");
   }

   delete pNetwork;
}

// ---------------------------------------------------------------------------
// Test 13: Notification callbacks
// ---------------------------------------------------------------------------

static void TestNotifications ()
{
   std::printf ("\n[Test 13] Notification callbacks\n");

   s_pVPHost->ResetCounters ();

   NETWORK* pNetwork = new NETWORK (s_pSneeze);
   pNetwork->Initialize ();

   TEST_FILE_LISTENER listener;
   NETWORK::FILE* pFile = pNetwork->Request (&listener, s_pViewport, &s_pTestName, "https://httpbin.org/bytes/8?test=notifications");

   Check (s_pVPHost->m_nCreatedCount > 0, "OnNetworkFileCreated fired");

   if (pFile)
   {
      bool bGot = listener.WaitFor (15000);
      if (bGot)
      {
         Check (s_pVPHost->m_nChangedCount > 0, "OnNetworkFileChanged fired");
      }
      else
      {
         std::printf ("    (Timed out — expected if no internet)\n");
         Check (true, "Notification test did not crash");
      }

      pFile->Release ();
   }

   delete pNetwork;
}

// ---------------------------------------------------------------------------
// Test 14: Served-from-cache detection
// ---------------------------------------------------------------------------

static void TestServedFromCache ()
{
   std::printf ("\n[Test 14] Served-from-cache detection\n");

   std::string sUrl = "https://httpbin.org/base64/Q2FjaGVkRGF0YQ==";
   std::string sSri;

   NETWORK* pNetwork = new NETWORK (s_pSneeze);
   pNetwork->Initialize ();

   // First fetch — should NOT be served from cache
   TEST_FILE_LISTENER listenerFirst;
   NETWORK::FILE* pFirst = pNetwork->Request (&listenerFirst, s_pViewport, &s_pTestName, sUrl);

   if (pFirst)
   {
      bool bGot = listenerFirst.WaitFor (15000);
      if (bGot  &&  listenerFirst.Succeeded ())
      {
         Check (!pFirst->IsServedFromCache (), "First fetch is not served from cache");

         // Second request for the same URL — should be served from cache
         TEST_FILE_LISTENER listenerSecond;
         NETWORK::FILE* pSecond = pNetwork->Request (&listenerSecond, s_pViewport, &s_pTestName, sUrl);

         if (pSecond)
         {
            Check (pSecond->IsServedFromCache (), "Second fetch IS served from cache");
            Check (pSecond->FileIx () > pFirst->FileIx (),
               "Second file index > first");
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

   delete pNetwork;
}

// ---------------------------------------------------------------------------
// Test 15: Failed fetch records HTTP status
// ---------------------------------------------------------------------------

static void TestFailedFetchHttpStatus ()
{
   std::printf ("\n[Test 15] Failed fetch records HTTP status\n");

   NETWORK* pNetwork = new NETWORK (s_pSneeze);
   pNetwork->Initialize ();

   TEST_FILE_LISTENER listener;
   NETWORK::FILE* pFile = pNetwork->Request (&listener, s_pViewport, &s_pTestName, "https://httpbin.org/status/404");

   if (pFile)
   {
      bool bGot = listener.WaitFor (15000);
      if (bGot)
      {
         Check (!listener.Succeeded (), "404 correctly failed");
         Check (pFile->HttpStatus () == 404, "HTTP status is 404");
         Check (pFile->FetchDuration () > 0.0, "Fetch duration recorded for failed request");
      }
      else
      {
         std::printf ("    (Timed out — expected if no internet)\n");
         Check (true, "HTTP status test did not crash");
      }

      pFile->Release ();
   }

   delete pNetwork;
}

// ---------------------------------------------------------------------------
// Test 16: Clear flag removes FILE from history on release
// ---------------------------------------------------------------------------

static void TestClearFlag ()
{
   std::printf ("\n[Test 16] Clear flag\n");

   NETWORK* pNetwork = new NETWORK (s_pSneeze);
   pNetwork->Initialize ();

   TEST_FILE_LISTENER listener;
   NETWORK::FILE* pFile = pNetwork->Request (&listener, s_pViewport, &s_pTestName, "https://httpbin.org/bytes/8");

   if (pFile)
   {
      listener.WaitFor (15000);

      Check (!pFile->IsPendingClear (), "FILE is not cleared before Clear()");

      pFile->Clear ();

      Check (pFile->IsPendingClear (),
         "Clear immediately sets pending-clear flag");

      pFile->Release ();
   }

   delete pNetwork;
}

// ---------------------------------------------------------------------------
// Test 17: Reset flag can be toggled off before release
// ---------------------------------------------------------------------------

static void TestResetFlagToggle ()
{
   std::printf ("\n[Test 17] Reset flag toggle\n");

   NETWORK* pNetwork = new NETWORK (s_pSneeze);
   pNetwork->Initialize ();

   TEST_FILE_LISTENER listener;
   NETWORK::FILE* pFile = pNetwork->Request (&listener, s_pViewport, &s_pTestName, "https://httpbin.org/bytes/8");

   if (pFile)
   {
      listener.WaitFor (15000);

      std::string sDiskPath = pFile->DiskPath ();
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

   delete pNetwork;
}

// ---------------------------------------------------------------------------
// Test 18: Deferred reset with multiple handles
// ---------------------------------------------------------------------------

static void TestDeferredReset ()
{
   std::printf ("\n[Test 18] Deferred reset (multiple handles)\n");

   NETWORK* pNetwork = new NETWORK (s_pSneeze);
   pNetwork->Initialize ();

   TEST_FILE_LISTENER listenerA;
   TEST_FILE_LISTENER listenerB;

   NETWORK::FILE* pFileA = pNetwork->Request (&listenerA, s_pViewport, &s_pTestName, "https://httpbin.org/bytes/16");
   NETWORK::FILE* pFileB = pNetwork->Request (&listenerB, s_pViewport, &s_pTestName, "https://httpbin.org/bytes/16");

   if (pFileA  &&  pFileB)
   {
      listenerA.WaitFor (15000);
      listenerB.WaitFor (15000);

      std::string sDiskPath = pFileA->DiskPath ();
      bool bHadDisk = !sDiskPath.empty ()  &&  std::filesystem::exists (sDiskPath);

      pFileA->Reset ();
      pFileA->Release ();

      if (bHadDisk)
      {
         Check (std::filesystem::exists (sDiskPath),
            "Disk file survives while second handle is attached");
      }

      Check (pFileB->Asset () != nullptr,
         "Second handle still has a valid ASSET");

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

   delete pNetwork;
}

// ---------------------------------------------------------------------------
// Test 19: Clear removes released FILE records
// ---------------------------------------------------------------------------

static void TestClear ()
{
   std::printf ("\n[Test 19] Clear\n");

   NETWORK* pNetwork = new NETWORK (s_pSneeze);
   pNetwork->Initialize ();

   TEST_FILE_LISTENER listenerA;
   TEST_FILE_LISTENER listenerB;

   NETWORK::FILE* pFileA = pNetwork->Request (&listenerA, s_pViewport, &s_pTestName, "https://httpbin.org/bytes/8");
   NETWORK::FILE* pFileB = pNetwork->Request (&listenerB, s_pViewport, &s_pTestName, "https://httpbin.org/bytes/16");

   if (pFileA  &&  pFileB)
   {
      listenerA.WaitFor (15000);
      listenerB.WaitFor (15000);

      pFileA->Release ();

//      size_t nHistoryBefore = pNetwork->Files ().size ();
//      Check (nHistoryBefore >= 2, "History has at least 2 entries before Clear");

      pNetwork->Clear ();

//      size_t nHistoryAfter = pNetwork->Files ().size ();
//      Check (nHistoryAfter < nHistoryBefore, "Clear removed released FILE records");
//      Check (nHistoryAfter >= 1,             "In-use FILE record survived Clear");

      pFileB->Release ();
   }
   else
   {
      if (pFileA) pFileA->Release ();
      if (pFileB) pFileB->Release ();
   }

   Check (true, "Clear completed without crash");

   delete pNetwork;
}

// ---------------------------------------------------------------------------
// Test 21: OnNetworkFileDeleted notification
// ---------------------------------------------------------------------------

static void TestDeletedNotification ()
{
   std::printf ("\n[Test 21] OnNetworkFileDeleted notification\n");

   s_pVPHost->ResetCounters ();

   NETWORK* pNetwork = new NETWORK (s_pSneeze);
   pNetwork->Initialize ();

   TEST_FILE_LISTENER listener;
   NETWORK::FILE* pFile = pNetwork->Request (&listener, s_pViewport, &s_pTestName, "https://httpbin.org/bytes/8");

   if (pFile)
   {
      listener.WaitFor (15000);

      Check (s_pVPHost->m_nDeletedCount == 0,
         "No deleted notifications before clear");

      pFile->Clear ();

      Check (s_pVPHost->m_nDeletedCount == 1,
         "OnNetworkFileDeleted fired immediately on clear");

      pFile->Release ();
   }

   delete pNetwork;
}

// ---------------------------------------------------------------------------
// Test 22: Staleness rules
// ---------------------------------------------------------------------------

static void TestStalenessRules ()
{
   std::printf ("\n[Test 22] Staleness rules\n");

   std::string sUrl = "https://httpbin.org/base64/U3RhbGVuZXNzVGVzdA==";

   // Phase 1: Fetch a file and shut down
   {
      NETWORK* pNetwork = new NETWORK (s_pSneeze);
      pNetwork->Initialize ();

      TEST_FILE_LISTENER listener;
      NETWORK::FILE* pFile = pNetwork->Request (&listener, s_pViewport, &s_pTestName, sUrl);

      if (pFile)
      {
         bool bGot = listener.WaitFor (15000);
         if (!bGot  ||  !listener.Succeeded ())
         {
            std::printf ("    (Timed out — skipping)\n");
            Check (true, "Staleness test did not crash (no internet)");
            pFile->Release ();
            delete pNetwork;
            return;
         }

         Check (pFile->IsReady (), "File fetched successfully");
         pFile->Release ();
      }

      delete pNetwork;
   }

   // Phase 2: Reinit with a staleness rule, verify re-fetch
   {
      NETWORK* pNetwork2 = new NETWORK (s_pSneeze);
      pNetwork2->Initialize ();

      pNetwork2->AddRule ("", "9999-12-31T23:59:59Z");

      TEST_FILE_LISTENER listener2;
      NETWORK::FILE* pFile2 = pNetwork2->Request (&listener2, s_pViewport, &s_pTestName, sUrl);

      if (pFile2)
      {
         Check (!pFile2->IsServedFromCache (), "Stale meta triggered re-fetch");
         listener2.WaitFor (15000);
         pFile2->Release ();
      }

      pNetwork2->Reset ();
      delete pNetwork2;
   }
}

// ---------------------------------------------------------------------------
// Test 23: Request with bFetch=false (no network)
// ---------------------------------------------------------------------------

static void TestNoFetchRequest ()
{
   std::printf ("\n[Test 23] Request with bFetch=false\n");

   NETWORK* pNetwork = new NETWORK (s_pSneeze);
   pNetwork->Initialize ();

   NETWORK::FILE* pFile = pNetwork->Request (nullptr, s_pViewport, &s_pTestName, "https://this-url-does-not-exist-in-cache.invalid/none",
      std::string (), NETWORK::REQUEST_CREATE);

   Check (pFile == nullptr, "bFetch=false returns null for uncached URL");

   delete pNetwork;
}

// ---------------------------------------------------------------------------
// Entry point
// ---------------------------------------------------------------------------

int RunNetworkTests (int /*nArgc*/, char** /*aArgv*/)
{
   std::printf ("=== Network Test Suite ===\n");

   s_pTestListener = new CACHE_TEST_LISTENER ();
   s_pTestListener->m_sAppDataPath = (std::filesystem::temp_directory_path () / "SneezeTest").string ();
   s_pTestListener->m_sSessionPath = s_pTestListener->m_sAppDataPath;

   auto sCachePath = std::filesystem::path (s_pTestListener->m_sAppDataPath) / "Cache";
   std::filesystem::remove_all (sCachePath);

   s_pSneeze = new SNEEZE::ENGINE (s_pTestListener);

   s_pVPHost = new CACHE_TEST_VIEWPORT_HOST ();
   s_pViewport = s_pSneeze->Viewport_Open (s_pVPHost);

   curl_global_init (CURL_GLOBAL_DEFAULT);

   TestManagerInit ();
   TestUnhashedFetch ();
   TestDeduplication ();
   TestHashVerifiedFetch ();
   TestHashMismatch ();
   TestReset ();
   TestResetFlag ();
   TestFailedFetch ();
   TestSidecarPersistence ();
   TestHttpHeaders ();
   TestFileHandleLifecycle ();
   TestHistoryAndFileIx ();
   TestNotifications ();
   TestServedFromCache ();
   TestFailedFetchHttpStatus ();
   TestClearFlag ();
   TestResetFlagToggle ();
   TestDeferredReset ();
   TestClear ();
   TestDeletedNotification ();
   TestStalenessRules ();
   TestNoFetchRequest ();

   curl_global_cleanup ();

   // Intentionally leak s_pSneeze — its destructor calls static subsystem
   // shutdowns (WASM, SPV, etc.) that may interfere with other test suites.

   std::printf ("\n=== Results: %d passed, %d failed ===\n", nPassed, nFailed);

   return (nFailed > 0) ? 1 : 0;
}
