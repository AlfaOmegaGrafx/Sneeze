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

#include "storage/Storage.h"
#include "container/Container.h"
#include "Sneeze.h"
#include "viewport/Viewport.h"

#include <cstdio>
#include <cstring>
#include <string>
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
// Minimal ISNEEZE for tests
// ---------------------------------------------------------------------------

class STORAGE_TEST_HOST : public SNEEZE::ISNEEZE
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
   std::string const& sSessionPath () const& override { return m_sSessionPath; }
   std::string const& sRenderer ()    const& override { return m_sRenderer; }

   void Log (eLOGLEVEL, const std::string& sModule, const std::string& sMessage) override
   {
      std::printf ("    [%s] %s\n", sModule.c_str (), sMessage.c_str ());
   }
};

class STORAGE_TEST_VIEWPORT_HOST : public SNEEZE::IVIEWPORT
{
public:
   int m_nCreatedCount = 0;
   int m_nChangedCount = 0;
   int m_nDeletedCount = 0;

   void OnFrameReady (const uint32_t*, int, int) override {}

   void OnStorageUnitCreated (SNEEZE::NOTIFICATION*) override { m_nCreatedCount++; }
   void OnStorageUnitChanged (SNEEZE::NOTIFICATION*) override { m_nChangedCount++; }
   void OnStorageUnitDeleted (SNEEZE::NOTIFICATION*) override { m_nDeletedCount++; }
};

static STORAGE_TEST_HOST*          s_pHost    = nullptr;
static STORAGE_TEST_VIEWPORT_HOST* s_pVPHost  = nullptr;
static SNEEZE*                     s_pSneeze  = nullptr;
static SNEEZE::STORAGE*            s_pStorage = nullptr;

static std::shared_ptr<SNEEZE::VIEWPORT::CONTAINER::NAME> MakeTestName (const std::string& sContainer = "poker")
{
   auto pName = std::make_shared<SNEEZE::VIEWPORT::CONTAINER::NAME> ();
   pName->sFingerprint   = "abcdef0123456789abcdef0123456789abcdef0123456789abcdef0123456789";
   pName->sOrganization  = "TestOrg";
   pName->sCommonName    = "TestOrg";
   pName->sContainerName = sContainer;
   pName->sPersonaHash   = "persona_hash_test";
   pName->bValidated     = true;
   return pName;
}

static void CleanTestDir ()
{
   std::error_code ec;
   std::filesystem::remove_all ("test_storage_session", ec);
}

// ---------------------------------------------------------------------------
// Test 1: Initialize and Open/Close
// ---------------------------------------------------------------------------

static void TestInitializeAndOpenClose ()
{
   std::printf ("\n[Test 1] Initialize and Open/Close\n");

   SNEEZE::STORAGE* pStorage = s_pStorage;
   Check (pStorage != nullptr, "Storage exists");

   auto pName = MakeTestName ();
   SNEEZE::STORAGE::ASSET* pAsset = pStorage->Open (pName);
   Check (pAsset != nullptr, "Open returns ASSET");
   Check (pAsset->GetName () == pName, "ASSET holds correct NAME");
   Check (pAsset->GetRefCount () == 1, "Ref count is 1 after Open");

   Check (s_pVPHost->m_nCreatedCount == 1, "OnStorageUnitCreated fired");

   pStorage->Close (pAsset);
}

// ---------------------------------------------------------------------------
// Test 2: Basic Set/Get/Has/Remove
// ---------------------------------------------------------------------------

static void TestBasicOperations ()
{
   std::printf ("\n[Test 2] Basic Set/Get/Has/Remove\n");

   SNEEZE::STORAGE* pStorage = s_pStorage;
   auto pName = MakeTestName ();
   SNEEZE::STORAGE::ASSET* pAsset = pStorage->Open (pName);

   s_pVPHost->m_nChangedCount = 0;

   pAsset->Set (SNEEZE::STORAGE::CONTAINER_PERMANENT, "player.name", "Dean");
   Check (s_pVPHost->m_nChangedCount == 1, "OnStorageUnitChanged fired on Set");

   auto jValue = pAsset->Get (SNEEZE::STORAGE::CONTAINER_PERMANENT, "player.name");
   Check (jValue.is_string (), "Get returns string type");
   Check (jValue.get<std::string> () == "Dean", "Get returns correct value");

   Check (pAsset->Has (SNEEZE::STORAGE::CONTAINER_PERMANENT, "player.name"), "Has returns true for existing key");
   Check (!pAsset->Has (SNEEZE::STORAGE::CONTAINER_PERMANENT, "player.missing"), "Has returns false for missing key");

   pAsset->Remove (SNEEZE::STORAGE::CONTAINER_PERMANENT, "player.name");
   Check (!pAsset->Has (SNEEZE::STORAGE::CONTAINER_PERMANENT, "player.name"), "Remove deletes key");

   pStorage->Close (pAsset);
}

// ---------------------------------------------------------------------------
// Test 3: Nested path navigation
// ---------------------------------------------------------------------------

static void TestPathNavigation ()
{
   std::printf ("\n[Test 3] Nested path navigation\n");

   SNEEZE::STORAGE* pStorage = s_pStorage;
   auto pName = MakeTestName ();
   SNEEZE::STORAGE::ASSET* pAsset = pStorage->Open (pName);

   pAsset->Set (SNEEZE::STORAGE::CONTAINER_PERMANENT, "game.poker.table.color", "green");
   pAsset->Set (SNEEZE::STORAGE::CONTAINER_PERMANENT, "game.poker.table.seats", 8);

   auto jColor = pAsset->Get (SNEEZE::STORAGE::CONTAINER_PERMANENT, "game.poker.table.color");
   Check (jColor.is_string ()  &&  jColor.get<std::string> () == "green", "Deep nested string");

   auto jSeats = pAsset->Get (SNEEZE::STORAGE::CONTAINER_PERMANENT, "game.poker.table.seats");
   Check (jSeats.is_number_integer ()  &&  jSeats.get<int> () == 8, "Deep nested number");

   Check (pAsset->Has (SNEEZE::STORAGE::CONTAINER_PERMANENT, "game.poker.table.color"), "Has works for deep path");
   Check (!pAsset->Has (SNEEZE::STORAGE::CONTAINER_PERMANENT, "game.poker.table.missing"), "Has fails for missing deep path");

   pStorage->Close (pAsset);
}

// ---------------------------------------------------------------------------
// Test 4: Array index access
// ---------------------------------------------------------------------------

static void TestArrayAccess ()
{
   std::printf ("\n[Test 4] Array index access\n");

   SNEEZE::STORAGE* pStorage = s_pStorage;
   auto pName = MakeTestName ();
   SNEEZE::STORAGE::ASSET* pAsset = pStorage->Open (pName);

   pAsset->Set (SNEEZE::STORAGE::CONTAINER_PERMANENT, "scores[0]", 100);
   pAsset->Set (SNEEZE::STORAGE::CONTAINER_PERMANENT, "scores[1]", 200);
   pAsset->Set (SNEEZE::STORAGE::CONTAINER_PERMANENT, "scores[2]", 300);

   auto j0 = pAsset->Get (SNEEZE::STORAGE::CONTAINER_PERMANENT, "scores[0]");
   auto j1 = pAsset->Get (SNEEZE::STORAGE::CONTAINER_PERMANENT, "scores[1]");
   auto j2 = pAsset->Get (SNEEZE::STORAGE::CONTAINER_PERMANENT, "scores[2]");

   Check (j0.is_number ()  &&  j0.get<int> () == 100, "Array index 0");
   Check (j1.is_number ()  &&  j1.get<int> () == 200, "Array index 1");
   Check (j2.is_number ()  &&  j2.get<int> () == 300, "Array index 2");

   pAsset->Set (SNEEZE::STORAGE::CONTAINER_PERMANENT, "game.players[0].name", "Alice");
   pAsset->Set (SNEEZE::STORAGE::CONTAINER_PERMANENT, "game.players[1].name", "Bob");

   auto jAlice = pAsset->Get (SNEEZE::STORAGE::CONTAINER_PERMANENT, "game.players[0].name");
   auto jBob   = pAsset->Get (SNEEZE::STORAGE::CONTAINER_PERMANENT, "game.players[1].name");

   Check (jAlice.is_string ()  &&  jAlice.get<std::string> () == "Alice", "Nested array object [0]");
   Check (jBob.is_string ()  &&  jBob.get<std::string> () == "Bob", "Nested array object [1]");

   pStorage->Close (pAsset);
}

// ---------------------------------------------------------------------------
// Test 5: Persistence across Open/Close cycles
// ---------------------------------------------------------------------------

static void TestPersistence ()
{
   std::printf ("\n[Test 5] Persistence across Open/Close\n");

   SNEEZE::STORAGE* pStorage = s_pStorage;
   auto pName = MakeTestName ("persist-test");

   {
      SNEEZE::STORAGE::ASSET* pAsset = pStorage->Open (pName);
      pAsset->Set (SNEEZE::STORAGE::CONTAINER_PERMANENT, "saved.value", 42);
      pAsset->Set (SNEEZE::STORAGE::CONTAINER_PERMANENT, "saved.text", "hello");
      pStorage->Close (pAsset);
   }

   {
      SNEEZE::STORAGE::ASSET* pAsset = pStorage->Open (pName);
      auto jValue = pAsset->Get (SNEEZE::STORAGE::CONTAINER_PERMANENT, "saved.value");
      auto jText  = pAsset->Get (SNEEZE::STORAGE::CONTAINER_PERMANENT, "saved.text");

      Check (jValue.is_number ()  &&  jValue.get<int> () == 42, "Number persisted across Open/Close");
      Check (jText.is_string ()  &&  jText.get<std::string> () == "hello", "String persisted across Open/Close");

      pStorage->Close (pAsset);
   }
}

// ---------------------------------------------------------------------------
// Test 6: Organization storage shared across containers
// ---------------------------------------------------------------------------

static void TestOrgSharing ()
{
   std::printf ("\n[Test 6] Organization storage shared across containers\n");

   SNEEZE::STORAGE* pStorage = s_pStorage;
   auto pNameA = MakeTestName ("container-a");
   auto pNameB = MakeTestName ("container-b");

   SNEEZE::STORAGE::ASSET* pAssetA = pStorage->Open (pNameA);
   pAssetA->Set (SNEEZE::STORAGE::ORG_PERMANENT, "org.setting", "shared-value");

   SNEEZE::STORAGE::ASSET* pAssetB = pStorage->Open (pNameB);
   auto jOrgB = pAssetB->Get (SNEEZE::STORAGE::ORG_PERMANENT, "org.setting");

   Check (jOrgB.is_string ()  &&  jOrgB.get<std::string> () == "shared-value",
      "Org storage visible to second container");

   Check (pAssetA->GetUnit (SNEEZE::STORAGE::ORG_PERMANENT) ==
          pAssetB->GetUnit (SNEEZE::STORAGE::ORG_PERMANENT),
      "Both containers share the same org UNIT");

   pStorage->Close (pAssetA);
   pStorage->Close (pAssetB);
}

// ---------------------------------------------------------------------------
// Test 7: Scope isolation
// ---------------------------------------------------------------------------

static void TestScopeIsolation ()
{
   std::printf ("\n[Test 7] Scope isolation\n");

   SNEEZE::STORAGE* pStorage = s_pStorage;
   auto pName = MakeTestName ("scope-test");
   SNEEZE::STORAGE::ASSET* pAsset = pStorage->Open (pName);

   pAsset->Set (SNEEZE::STORAGE::CONTAINER_PERMANENT, "key", "permanent");
   pAsset->Set (SNEEZE::STORAGE::CONTAINER_TEMPORARY, "key", "temporary");
   pAsset->Set (SNEEZE::STORAGE::ORG_PERMANENT, "key", "org-permanent");
   pAsset->Set (SNEEZE::STORAGE::ORG_TEMPORARY, "key", "org-temporary");

   auto j1 = pAsset->Get (SNEEZE::STORAGE::CONTAINER_PERMANENT, "key");
   auto j2 = pAsset->Get (SNEEZE::STORAGE::CONTAINER_TEMPORARY, "key");
   auto j3 = pAsset->Get (SNEEZE::STORAGE::ORG_PERMANENT, "key");
   auto j4 = pAsset->Get (SNEEZE::STORAGE::ORG_TEMPORARY, "key");

   Check (j1.get<std::string> () == "permanent", "CONTAINER_PERMANENT isolated");
   Check (j2.get<std::string> () == "temporary", "CONTAINER_TEMPORARY isolated");
   Check (j3.get<std::string> () == "org-permanent", "ORG_PERMANENT isolated");
   Check (j4.get<std::string> () == "org-temporary", "ORG_TEMPORARY isolated");

   pStorage->Close (pAsset);
}

// ---------------------------------------------------------------------------
// Test 8: JSONL crash recovery
// ---------------------------------------------------------------------------

static void TestCrashRecovery ()
{
   std::printf ("\n[Test 8] JSONL crash recovery\n");

   SNEEZE::STORAGE* pStorage = s_pStorage;
   auto pName = MakeTestName ("crash-test");

   // Compute the actual path that STORAGE will use
   std::string sFp2  = pName->sFingerprint.substr (0, 2);
   std::string sFp22 = pName->sFingerprint.substr (2);
   std::filesystem::path sDir = std::filesystem::path (pStorage->GetPermanentPath ())
      / pName->sPersonaHash / (sFp2 + "/" + sFp22);
   std::filesystem::path sJsonPath = sDir / "container-crash-test.json";
   std::filesystem::path sLogPath  = std::filesystem::path (sJsonPath.string () + ".log");

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

   SNEEZE::STORAGE::ASSET* pAsset = pStorage->Open (pName);

   auto jRecovered = pAsset->Get (SNEEZE::STORAGE::CONTAINER_PERMANENT, "recovered");
   auto jBase      = pAsset->Get (SNEEZE::STORAGE::CONTAINER_PERMANENT, "base");
   auto jRemoved   = pAsset->Get (SNEEZE::STORAGE::CONTAINER_PERMANENT, "will-remove");

   Check (jRecovered.is_string ()  &&  jRecovered.get<std::string> () == "yes",
      "Log Set replayed on recovery");
   Check (jBase.is_number ()  &&  jBase.get<int> () == 99,
      "Log Set overwrites base value");
   Check (jRemoved.is_null (),
      "Log Remove replayed on recovery");

   Check (!std::filesystem::exists (sLogPath), "Log file deleted after recovery");

   pStorage->Close (pAsset);
}

// ---------------------------------------------------------------------------
// Test 9: Bulk JSON get/set
// ---------------------------------------------------------------------------

static void TestBulkJson ()
{
   std::printf ("\n[Test 9] Bulk JSON get/set\n");

   SNEEZE::STORAGE* pStorage = s_pStorage;
   auto pName = MakeTestName ("bulk-test");
   SNEEZE::STORAGE::ASSET* pAsset = pStorage->Open (pName);

   pAsset->SetJson (SNEEZE::STORAGE::CONTAINER_PERMANENT,
      "{\"bulk\": {\"a\": 1, \"b\": 2}, \"list\": [10, 20, 30]}");

   auto jA = pAsset->Get (SNEEZE::STORAGE::CONTAINER_PERMANENT, "bulk.a");
   auto jB = pAsset->Get (SNEEZE::STORAGE::CONTAINER_PERMANENT, "bulk.b");
   auto jList1 = pAsset->Get (SNEEZE::STORAGE::CONTAINER_PERMANENT, "list[1]");

   Check (jA.is_number ()  &&  jA.get<int> () == 1, "Bulk set: nested value a");
   Check (jB.is_number ()  &&  jB.get<int> () == 2, "Bulk set: nested value b");
   Check (jList1.is_number ()  &&  jList1.get<int> () == 20, "Bulk set: array access");

   std::string sJson = pAsset->GetJson (SNEEZE::STORAGE::CONTAINER_PERMANENT);
   Check (sJson.find ("\"bulk\"") != std::string::npos, "GetJson contains data");

   pStorage->Close (pAsset);
}

// ---------------------------------------------------------------------------
// Test 10: Meta sidecar written on Close
// ---------------------------------------------------------------------------

static void TestMetaSidecar ()
{
   std::printf ("\n[Test 10] Meta sidecar\n");

   SNEEZE::STORAGE* pStorage = s_pStorage;
   auto pName = MakeTestName ("meta-test");

   SNEEZE::STORAGE::ASSET* pAsset = pStorage->Open (pName);
   pAsset->Set (SNEEZE::STORAGE::CONTAINER_PERMANENT, "data", "value");
   pStorage->Close (pAsset);

   std::string sFp2  = pName->sFingerprint.substr (0, 2);
   std::string sFp22 = pName->sFingerprint.substr (2);
   std::filesystem::path sDir = std::filesystem::path (pStorage->GetPermanentPath ())
      / pName->sPersonaHash / (sFp2 + "/" + sFp22);
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
   s_pSneeze = new SNEEZE (s_pHost);

   s_pVPHost = new STORAGE_TEST_VIEWPORT_HOST ();
   s_pSneeze->OpenViewport (s_pVPHost);

   s_pStorage = new SNEEZE::STORAGE (s_pSneeze);
   bool bInit = s_pStorage->Initialize ();
   Check (bInit, "Storage initialized");

   if (bInit)
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

   s_pStorage->Shutdown ();
   delete s_pStorage;
   s_pStorage = nullptr;
   delete s_pSneeze;
   delete s_pHost;

   CleanTestDir ();

   std::printf ("\n  --- Results: %d passed, %d failed ---\n", nPassed, nFailed);
   return (nFailed > 0) ? 1 : 0;
}
