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

#include <cstdio>
#include <cstring>
#include <string>
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
// Minimal ISNEEZE for tests
// ---------------------------------------------------------------------------

class STORAGE_TEST_HOST : public SNEEZE::IENGINE
{
public:
   std::string m_sAppDataPath;
   std::string m_sSessionPath;
   std::string m_sRenderer;

   STORAGE_TEST_HOST ()
   {
      m_sAppDataPath = ".";
      m_sSessionPath = "test_storage_session";
   }

   std::string const& sAppDataPath () const& override { return m_sAppDataPath; }
   std::string const& sRenderer ()    const& override { return m_sRenderer; }

   void Log (eLOGLEVEL, const std::string& sModule, const std::string& sMessage) override
   {
      std::printf ("    [%s] %s\n", sModule.c_str (), sMessage.c_str ());
   }
};

class STORAGE_TEST_VIEWPORT_HOST : public IVIEWPORT
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
      nWidth = 0;
      nHeight = 0;
   }

   void OnFrameReady (const uint32_t*, int, int) override {}

   void OnStorageUnitCreated (STORAGE::SILO*) override { m_nCreatedCount++; }
   void OnStorageUnitChanged (STORAGE::SILO*) override { m_nChangedCount++; }
   void OnStorageUnitDeleted (STORAGE::SILO*) override { m_nDeletedCount++; }
};

static STORAGE_TEST_HOST*          s_pHost     = nullptr;
static STORAGE_TEST_VIEWPORT_HOST* s_pVPHost   = nullptr;
static ENGINE*                     s_pSneeze   = nullptr;
static STORAGE*                    s_pStorage  = nullptr;
static VIEWPORT*                   s_pViewport = nullptr;

static std::shared_ptr<VIEWPORT::CONTAINER::CID> MakeTestCID (const std::string& sContainer = "poker")
{
   auto pCID = std::make_shared<VIEWPORT::CONTAINER::CID> ();
   pCID->sFingerprint   = "abcdef0123456789abcdef0123456789abcdef0123456789abcdef0123456789";
   pCID->sOrganization  = "TestOrg";
   pCID->sCommonName    = "TestOrg";
   pCID->sContainerName = sContainer;
   pCID->sPersonaHash   = "persona_hash_test";
   pCID->bValidated     = true;
   return pCID;
}

static void CleanTestDir ()
{
   std::error_code ec;
   std::filesystem::remove_all ("Sneeze", ec);
   std::filesystem::remove_all ("test_storage_session", ec);
}

// ---------------------------------------------------------------------------
// Test 1: Initialize and Open/Close
// ---------------------------------------------------------------------------

static void TestInitializeAndOpenClose ()
{
   std::printf ("\n[Test 1] Initialize and Open/Close\n");

   STORAGE* pStorage = s_pStorage;
   Check (pStorage != nullptr, "Storage exists");

   auto pCID = MakeTestCID ();
   STORAGE::SILO* pSilo = pStorage->Silo_Open (s_pViewport, pCID);
   Check (pSilo != nullptr, "Open returns SILO");
   Check (pSilo->CID () == pCID, "SILO holds correct CID");
   Check (pSilo->Count_Load () == 0, "Load count is 0 after Open");

   pSilo->Attach ();
   Check (pSilo->Count_Load () == 1, "Load count is 1 after Attach");

   Check (s_pVPHost->m_nCreatedCount == 1, "OnStorageUnitCreated fired");

   pSilo->Detach ();
   pStorage->Silo_Close (s_pViewport, pSilo);
}

// ---------------------------------------------------------------------------
// Test 2: Basic Set/Get/Has/Remove
// ---------------------------------------------------------------------------

static void TestBasicOperations ()
{
   std::printf ("\n[Test 2] Basic Set/Get/Has/Remove\n");

   STORAGE* pStorage = s_pStorage;
   auto pCID = MakeTestCID ();
   STORAGE::SILO* pSilo = pStorage->Silo_Open (s_pViewport, pCID);
   pSilo->Attach ();

   s_pVPHost->m_nChangedCount = 0;

   pSilo->Set (STORAGE::kSCOPE_PERMANENT_COMPANY, "player.name", "Dean");
   Check (s_pVPHost->m_nChangedCount == 1, "OnStorageUnitChanged fired on Set");

   auto jValue = pSilo->Get (STORAGE::kSCOPE_PERMANENT_COMPANY, "player.name");
   Check (jValue.is_string (), "Get returns string type");
   Check (jValue.get<std::string> () == "Dean", "Get returns correct value");

   Check (pSilo->Has (STORAGE::kSCOPE_PERMANENT_COMPANY, "player.name"), "Has returns true for existing key");
   Check (!pSilo->Has (STORAGE::kSCOPE_PERMANENT_COMPANY, "player.missing"), "Has returns false for missing key");

   pSilo->Remove (STORAGE::kSCOPE_PERMANENT_COMPANY, "player.name");
   Check (!pSilo->Has (STORAGE::kSCOPE_PERMANENT_COMPANY, "player.name"), "Remove deletes key");

   pSilo->Detach ();
   pStorage->Silo_Close (s_pViewport, pSilo);
}

// ---------------------------------------------------------------------------
// Test 3: Nested path navigation
// ---------------------------------------------------------------------------

static void TestPathNavigation ()
{
   std::printf ("\n[Test 3] Nested path navigation\n");

   STORAGE* pStorage = s_pStorage;
   auto pCID = MakeTestCID ();
   STORAGE::SILO* pSilo = pStorage->Silo_Open (s_pViewport, pCID);
   pSilo->Attach ();

   pSilo->Set (STORAGE::kSCOPE_PERMANENT_COMPANY, "game.poker.table.color", "green");
   pSilo->Set (STORAGE::kSCOPE_PERMANENT_COMPANY, "game.poker.table.seats", 8);

   auto jColor = pSilo->Get (STORAGE::kSCOPE_PERMANENT_COMPANY, "game.poker.table.color");
   Check (jColor.is_string ()  &&  jColor.get<std::string> () == "green", "Deep nested string");

   auto jSeats = pSilo->Get (STORAGE::kSCOPE_PERMANENT_COMPANY, "game.poker.table.seats");
   Check (jSeats.is_number_integer ()  &&  jSeats.get<int> () == 8, "Deep nested number");

   Check (pSilo->Has (STORAGE::kSCOPE_PERMANENT_COMPANY, "game.poker.table.color"), "Has works for deep path");
   Check (!pSilo->Has (STORAGE::kSCOPE_PERMANENT_COMPANY, "game.poker.table.missing"), "Has fails for missing deep path");

   pSilo->Detach ();
   pStorage->Silo_Close (s_pViewport, pSilo);
}

// ---------------------------------------------------------------------------
// Test 4: Array index access
// ---------------------------------------------------------------------------

static void TestArrayAccess ()
{
   std::printf ("\n[Test 4] Array index access\n");

   STORAGE* pStorage = s_pStorage;
   auto pCID = MakeTestCID ();
   STORAGE::SILO* pSilo = pStorage->Silo_Open (s_pViewport, pCID);
   pSilo->Attach ();

   pSilo->Set (STORAGE::kSCOPE_PERMANENT_COMPANY, "scores[0]", 100);
   pSilo->Set (STORAGE::kSCOPE_PERMANENT_COMPANY, "scores[1]", 200);
   pSilo->Set (STORAGE::kSCOPE_PERMANENT_COMPANY, "scores[2]", 300);

   auto j0 = pSilo->Get (STORAGE::kSCOPE_PERMANENT_COMPANY, "scores[0]");
   auto j1 = pSilo->Get (STORAGE::kSCOPE_PERMANENT_COMPANY, "scores[1]");
   auto j2 = pSilo->Get (STORAGE::kSCOPE_PERMANENT_COMPANY, "scores[2]");

   Check (j0.is_number ()  &&  j0.get<int> () == 100, "Array index 0");
   Check (j1.is_number ()  &&  j1.get<int> () == 200, "Array index 1");
   Check (j2.is_number ()  &&  j2.get<int> () == 300, "Array index 2");

   pSilo->Set (STORAGE::kSCOPE_PERMANENT_COMPANY, "game.players[0].name", "Alice");
   pSilo->Set (STORAGE::kSCOPE_PERMANENT_COMPANY, "game.players[1].name", "Bob");

   auto jAlice = pSilo->Get (STORAGE::kSCOPE_PERMANENT_COMPANY, "game.players[0].name");
   auto jBob   = pSilo->Get (STORAGE::kSCOPE_PERMANENT_COMPANY, "game.players[1].name");

   Check (jAlice.is_string ()  &&  jAlice.get<std::string> () == "Alice", "Nested array object [0]");
   Check (jBob.is_string ()  &&  jBob.get<std::string> () == "Bob", "Nested array object [1]");

   pSilo->Detach ();
   pStorage->Silo_Close (s_pViewport, pSilo);
}

// ---------------------------------------------------------------------------
// Test 5: Persistence across Open/Close cycles
// ---------------------------------------------------------------------------

static void TestPersistence ()
{
   std::printf ("\n[Test 5] Persistence across Open/Close\n");

   STORAGE* pStorage = s_pStorage;
   auto pCID = MakeTestCID ("persist-test");

   {
      STORAGE::SILO* pSilo = pStorage->Silo_Open (s_pViewport, pCID);
      pSilo->Attach ();
      pSilo->Set (STORAGE::kSCOPE_PERMANENT_COMPANY, "saved.value", 42);
      pSilo->Set (STORAGE::kSCOPE_PERMANENT_COMPANY, "saved.text", "hello");
      pSilo->Detach ();
      pStorage->Silo_Close (s_pViewport, pSilo);
   }

   {
      STORAGE::SILO* pSilo = pStorage->Silo_Open (s_pViewport, pCID);
      pSilo->Attach ();
      auto jValue = pSilo->Get (STORAGE::kSCOPE_PERMANENT_COMPANY, "saved.value");
      auto jText  = pSilo->Get (STORAGE::kSCOPE_PERMANENT_COMPANY, "saved.text");

      Check (jValue.is_number ()  &&  jValue.get<int> () == 42, "Number persisted across Open/Close");
      Check (jText.is_string ()  &&  jText.get<std::string> () == "hello", "String persisted across Open/Close");

      pSilo->Detach ();
      pStorage->Silo_Close (s_pViewport, pSilo);
   }
}

// ---------------------------------------------------------------------------
// Test 6: Organization storage shared across containers
// ---------------------------------------------------------------------------

static void TestOrgSharing ()
{
   std::printf ("\n[Test 6] Organization storage shared across containers\n");

   STORAGE* pStorage = s_pStorage;
   auto pCID_A = MakeTestCID ("container-a");
   auto pCID_B = MakeTestCID ("container-b");

   STORAGE::SILO* pSiloA = pStorage->Silo_Open (s_pViewport, pCID_A);
   pSiloA->Attach ();
   pSiloA->Set (STORAGE::kSCOPE_PERMANENT_ORG, "org.setting", "shared-value");

   STORAGE::SILO* pSiloB = pStorage->Silo_Open (s_pViewport, pCID_B);
   pSiloB->Attach ();
   auto jOrgB = pSiloB->Get (STORAGE::kSCOPE_PERMANENT_ORG, "org.setting");

   Check (jOrgB.is_string ()  &&  jOrgB.get<std::string> () == "shared-value",
      "Org storage visible to second container");

   Check (pSiloA->Unit (STORAGE::kSCOPE_PERMANENT_ORG) ==
          pSiloB->Unit (STORAGE::kSCOPE_PERMANENT_ORG),
      "Both containers share the same org UNIT");

   pSiloA->Detach ();
   pStorage->Silo_Close (s_pViewport, pSiloA);
   pSiloB->Detach ();
   pStorage->Silo_Close (s_pViewport, pSiloB);
}

// ---------------------------------------------------------------------------
// Test 7: Scope isolation
// ---------------------------------------------------------------------------

static void TestScopeIsolation ()
{
   std::printf ("\n[Test 7] Scope isolation\n");

   STORAGE* pStorage = s_pStorage;
   auto pCID = MakeTestCID ("scope-test");
   STORAGE::SILO* pSilo = pStorage->Silo_Open (s_pViewport, pCID);
   pSilo->Attach ();

   pSilo->Set (STORAGE::kSCOPE_PERMANENT_COMPANY, "key", "permanent");
   pSilo->Set (STORAGE::kSCOPE_TEMPORARY_COMPANY, "key", "temporary");
   pSilo->Set (STORAGE::kSCOPE_PERMANENT_ORG, "key", "org-permanent");
   pSilo->Set (STORAGE::kSCOPE_TEMPORARY_ORG, "key", "org-temporary");

   auto j1 = pSilo->Get (STORAGE::kSCOPE_PERMANENT_COMPANY, "key");
   auto j2 = pSilo->Get (STORAGE::kSCOPE_TEMPORARY_COMPANY, "key");
   auto j3 = pSilo->Get (STORAGE::kSCOPE_PERMANENT_ORG, "key");
   auto j4 = pSilo->Get (STORAGE::kSCOPE_TEMPORARY_ORG, "key");

   Check (j1.get<std::string> () == "permanent", "PERMANENT_COMPANY isolated");
   Check (j2.get<std::string> () == "temporary", "TEMPORARY_COMPANY isolated");
   Check (j3.get<std::string> () == "org-permanent", "PERMANENT_ORG isolated");
   Check (j4.get<std::string> () == "org-temporary", "TEMPORARY_ORG isolated");

   pSilo->Detach ();
   pStorage->Silo_Close (s_pViewport, pSilo);
}

// ---------------------------------------------------------------------------
// Test 8: JSONL crash recovery
// ---------------------------------------------------------------------------

static void TestCrashRecovery ()
{
   std::printf ("\n[Test 8] JSONL crash recovery\n");

   STORAGE* pStorage = s_pStorage;
   auto pCID = MakeTestCID ("crash-test");

   // Compute the actual path that STORAGE will use
   std::string sFp2  = pCID->sFingerprint.substr (0, 2);
   std::string sFp22 = pCID->sFingerprint.substr (2, 22);
   std::filesystem::path sDir = std::filesystem::path (s_pViewport->sPath_Permanent ()) / "Storage" / pCID->sPersonaHash / (sFp2 + "/" + sFp22);
   std::filesystem::path sJsonPath = sDir / "container-crash-test.json";
   std::filesystem::path sLogPath  = sDir / "container-crash-test.log";

   std::filesystem::create_directories (sDir);

   // Write a base JSON file
   {
      std::ofstream f (sJsonPath, std::ios::trunc);
      f << "{\"base\": 1, \"will-remove\": true}";
   }

   // Write a simulated .log file (as if we crashed mid-session)
   {
      std::ofstream f (sLogPath, std::ios::trunc);
      f << "[\"Set\",\"recovered\",\"yes\"]\n";
      f << "[\"Set\",\"base\",99]\n";
      f << "[\"Remove\",\"will-remove\"]\n";
   }

   STORAGE::SILO* pSilo = pStorage->Silo_Open (s_pViewport, pCID);
   pSilo->Attach ();

   auto jRecovered = pSilo->Get (STORAGE::kSCOPE_PERMANENT_COMPANY, "recovered");
   auto jBase      = pSilo->Get (STORAGE::kSCOPE_PERMANENT_COMPANY, "base");
   auto jRemoved   = pSilo->Get (STORAGE::kSCOPE_PERMANENT_COMPANY, "will-remove");

   Check (jRecovered.is_string ()  &&  jRecovered.get<std::string> () == "yes",
      "Log Set replayed on recovery");
   Check (jBase.is_number ()  &&  jBase.get<int> () == 99,
      "Log Set overwrites base value");
   Check (jRemoved.is_null (),
      "Log Remove replayed on recovery");

   Check (!std::filesystem::exists (sLogPath), "Log file deleted after recovery");

   pSilo->Detach ();
   pStorage->Silo_Close (s_pViewport, pSilo);
}

// ---------------------------------------------------------------------------
// Test 9: Bulk JSON get/set
// ---------------------------------------------------------------------------

static void TestBulkJson ()
{
   std::printf ("\n[Test 9] Bulk JSON get/set\n");

   STORAGE* pStorage = s_pStorage;
   auto pCID = MakeTestCID ("bulk-test");
   STORAGE::SILO* pSilo = pStorage->Silo_Open (s_pViewport, pCID);
   pSilo->Attach ();

   pSilo->Json (STORAGE::kSCOPE_PERMANENT_COMPANY,
      "{\"bulk\": {\"a\": 1, \"b\": 2}, \"list\": [10, 20, 30]}");

   auto jA = pSilo->Get (STORAGE::kSCOPE_PERMANENT_COMPANY, "bulk.a");
   auto jB = pSilo->Get (STORAGE::kSCOPE_PERMANENT_COMPANY, "bulk.b");
   auto jList1 = pSilo->Get (STORAGE::kSCOPE_PERMANENT_COMPANY, "list[1]");

   Check (jA.is_number ()  &&  jA.get<int> () == 1, "Bulk set: nested value a");
   Check (jB.is_number ()  &&  jB.get<int> () == 2, "Bulk set: nested value b");
   Check (jList1.is_number ()  &&  jList1.get<int> () == 20, "Bulk set: array access");

   std::string sJson = pSilo->Json (STORAGE::kSCOPE_PERMANENT_COMPANY);
   Check (sJson.find ("\"bulk\"") != std::string::npos, "Json contains data");

   pSilo->Detach ();
   pStorage->Silo_Close (s_pViewport, pSilo);
}

// ---------------------------------------------------------------------------
// Test 10: Meta sidecar written on Close
// ---------------------------------------------------------------------------

static void TestMetaSidecar ()
{
   std::printf ("\n[Test 10] Meta sidecar\n");

   STORAGE* pStorage = s_pStorage;
   auto pCID = MakeTestCID ("meta-test");

   STORAGE::SILO* pSilo = pStorage->Silo_Open (s_pViewport, pCID);
   pSilo->Attach ();
   pSilo->Set (STORAGE::kSCOPE_PERMANENT_COMPANY, "data", "value");
   pSilo->Detach ();
   pStorage->Silo_Close (s_pViewport, pSilo);

   std::string sFp2  = pCID->sFingerprint.substr (0, 2);
   std::string sFp22 = pCID->sFingerprint.substr (2);
   std::filesystem::path sDir = std::filesystem::path (s_pViewport->sPath_Permanent ()) / pCID->sPersonaHash / (sFp2 + "/" + sFp22);
   std::filesystem::path sMetaPath = sDir / "container-meta-test.json.meta";

   Check (std::filesystem::exists (sMetaPath), "Meta sidecar file created on Close");

   if (std::filesystem::exists (sMetaPath))
   {
      std::ifstream f (sMetaPath);
      nlohmann::json jMeta = nlohmann::json::parse (f);

      Check (jMeta.value ("containerName", "") == "meta-test", "Meta contains containerName");
      Check (jMeta.value ("organization", "") == "TestOrg", "Meta contains organization");
      Check (jMeta.contains ("createdAt"), "Meta contains createdAt");
      Check (jMeta.contains ("sizeBytes"), "Meta contains sizeBytes");
   }
}

// ---------------------------------------------------------------------------
// Entry point
// ---------------------------------------------------------------------------

int RunStorageTests (int nArgc, char** aArgv)
{
   (void) nArgc; (void) aArgv;

   nPassed = 0;
   nFailed = 0;

   std::printf ("=== Storage Test Suite ===\n");

   CleanTestDir ();

   s_pHost = new STORAGE_TEST_HOST ();
   s_pSneeze = new ENGINE (s_pHost);
   s_pVPHost = new STORAGE_TEST_VIEWPORT_HOST ();

   bool bEngineInit = s_pSneeze->Initialize ();
   Check (bEngineInit, "Engine initialized");

   s_pViewport = s_pSneeze->Viewport_Open (s_pVPHost);
   Check (s_pViewport != nullptr, "Viewport opened");

   s_pStorage = s_pSneeze->Storage ();
   Check (s_pStorage != nullptr, "Engine storage exists");

   bool bRun = bEngineInit  &&  s_pViewport  &&  s_pStorage;

   if (bRun)
   {
      TestInitializeAndOpenClose ();
      TestBasicOperations ();
      TestPathNavigation ();
      TestArrayAccess ();
      TestPersistence ();
      TestOrgSharing ();
      TestScopeIsolation ();
      TestCrashRecovery ();
      TestBulkJson ();
      TestMetaSidecar ();
   }

   s_pStorage = nullptr;
   s_pSneeze->Viewport_Close (s_pViewport);
   s_pViewport = nullptr;
   delete s_pSneeze;
   s_pSneeze = nullptr;
   delete s_pVPHost;
   s_pVPHost = nullptr;
   delete s_pHost;
   s_pHost = nullptr;

   CleanTestDir ();

   std::printf ("\n  --- Results: %d passed, %d failed ---\n", nPassed, nFailed);
   return (nFailed > 0) ? 1 : 0;
}
