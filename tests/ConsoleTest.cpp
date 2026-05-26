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
#include <thread>
#include <filesystem>
#include <deque>

using namespace SNEEZE;

// ---------------------------------------------------------------------------
// Test helpers
// ---------------------------------------------------------------------------

static int g_nPassed = 0;
static int g_nFailed = 0;

#define ASSERT(expr, msg) \
   do { if (expr) { g_nPassed++; std::printf ("  PASS: %s\n", msg); } \
        else      { g_nFailed++; std::printf ("  FAIL: %s\n", msg); } } while (0)

// ---------------------------------------------------------------------------
// Minimal ENGINE host
// ---------------------------------------------------------------------------

class TEST_HOST : public IENGINE
{
public:
   TEST_HOST (const std::string& sPath) : m_sPath (sPath), m_sRenderer ("halogen") {}

   std::string const& sAppDataPath () const& override { return m_sPath; }
   std::string const& sRenderer ()    const& override { return m_sRenderer; }

   void Log (eLOGLEVEL, const std::string& sModule, const std::string& sMessage) override
   {
      std::printf ("    [%s] %s\n", sModule.c_str (), sMessage.c_str ());
   }

private:
   std::string m_sPath;
   std::string m_sRenderer;
};

// ---------------------------------------------------------------------------
// ICONTEXT host that tracks console notifications
// ---------------------------------------------------------------------------

class TEST_CONTEXT_HOST : public ICONTEXT
{
public:
   int m_nEntryCount = 0;
   int m_nClearCount = 0;

   bool OnConsoleEntryCreated (std::shared_ptr<const CONSOLE::ENTRY>) override { m_nEntryCount++; return true; }
   void OnConsoleEntryDeleted (std::shared_ptr<const CONSOLE::ENTRY>) override { m_nClearCount++; }
};

// ---------------------------------------------------------------------------
// IENUM collector
// ---------------------------------------------------------------------------

class ENTRY_COLLECTOR : public CONSOLE::IENUM
{
public:
   std::vector<std::shared_ptr<const CONSOLE::ENTRY>> m_aEntry;

   void OnEntry (std::shared_ptr<const CONSOLE::ENTRY> pEntry) override
   {
      m_aEntry.push_back (pEntry);
   }
};

// ---------------------------------------------------------------------------
// RunConsoleTests
// ---------------------------------------------------------------------------

int RunConsoleTests (int nArgc, char** aArgv)
{
   (void) nArgc; (void) aArgv;

   g_nPassed = 0;
   g_nFailed = 0;

   std::printf ("Console Tests\n");
   std::printf ("---------------------------------------------------------\n");

   // Clean test directory
   std::string sTestPath = (std::filesystem::path (
#ifdef _WIN32
      std::getenv ("APPDATA")
#else
      "/tmp"
#endif
   ) / "Metaversal" / "Sneeze" / "Test").string ();

   std::error_code ec;
   std::filesystem::remove_all (sTestPath, ec);
   std::filesystem::create_directories (sTestPath, ec);

   // Create engine
   TEST_HOST host (sTestPath);
   ENGINE engine (&host);
   bool bInit = engine.Initialize ();
   ASSERT (bInit, "Engine initialized");

   if (!bInit)
   {
      std::printf ("\n  --- Results: %d passed, %d failed ---\n", g_nPassed, g_nFailed);
      return (g_nFailed > 0) ? 1 : 0;
   }

   engine.Login ("Test", "User");

   // Create a CID for testing
   CONTEXT::CONTAINER::CID cid;
   cid.sFingerprint    = "abcdef0123456789abcdef01234567890123456789abcdef0123456789abcd";
   cid.sOrganization   = "TestOrg";
   cid.sCommonName     = "TestProvider";
   cid.sContainerName  = "TestContainer";
   cid.sPersonaHash    = "a1b2c3d4e5f6a1b2c3d4e5f6a1b2c3d4e5f6a1b2c3d4e5f6a1b2c3d4e5f6a1b2";
   cid.bValidated      = true;

   // -----------------------------------------------------------------------
   // Test 1: Console initialization
   // -----------------------------------------------------------------------

   std::printf ("\n[Test 1] Console initialization\n");

   TEST_CONTEXT_HOST contextHost;
   CONTEXT* pContext = engine.Context_Open (&contextHost);
   ASSERT (pContext != nullptr, "Context created");
   ASSERT (pContext->Console () != nullptr, "Console exists");

   CONSOLE* pConsole = pContext->Console ();

   // -----------------------------------------------------------------------
   // Test 2: Basic logging
   // -----------------------------------------------------------------------

   std::printf ("\n[Test 2] Basic logging\n");

   pConsole->Log   (&cid, "Hello from log");
   pConsole->Debug (&cid, "Hello from debug");
   pConsole->Info  (&cid, "Hello from info");
   pConsole->Warn  (&cid, "Hello from warn");
   pConsole->Error (&cid, "Hello from error");

   ENTRY_COLLECTOR collector;
   pConsole->Entry_Enum (&collector);
   ASSERT (collector.m_aEntry.size () == 5, "Five entries in ring buffer");
   ASSERT (collector.m_aEntry[0]->Level () == CONSOLE::kLEVEL_LOG,   "First entry is LOG");
   ASSERT (collector.m_aEntry[4]->Level () == CONSOLE::kLEVEL_ERROR, "Last entry is ERROR");
   ASSERT (collector.m_aEntry[0]->Message () == "Hello from log", "Log message correct");
   ASSERT (contextHost.m_nEntryCount == 5, "Five OnConsoleEntry notifications");

   // -----------------------------------------------------------------------
   // Test 3: Engine-internal logging (null CID)
   // -----------------------------------------------------------------------

   std::printf ("\n[Test 3] Engine-internal logging (null CID)\n");

   pConsole->Info (nullptr, "Engine startup complete");

   ENTRY_COLLECTOR collector2;
   pConsole->Entry_Enum (&collector2);
   ASSERT (collector2.m_aEntry.size () == 6, "Six entries total");
   ASSERT (collector2.m_aEntry[5]->CID () == nullptr, "Engine entry has null CID");

   // -----------------------------------------------------------------------
   // Test 4: Assert
   // -----------------------------------------------------------------------

   std::printf ("\n[Test 4] Assert\n");

   pConsole->Assert (&cid, true,  "This should not appear");
   pConsole->Assert (&cid, false, "This should appear");

   ENTRY_COLLECTOR collector3;
   pConsole->Entry_Enum (&collector3);
   ASSERT (collector3.m_aEntry.size () == 7, "Seven entries (true assert produces nothing)");
   ASSERT (collector3.m_aEntry[6]->Level () == CONSOLE::kLEVEL_ERROR, "Failed assert is ERROR");
   ASSERT (collector3.m_aEntry[6]->Message ().find ("Assertion failed") != std::string::npos, "Assert message contains prefix");

   // -----------------------------------------------------------------------
   // Test 5: Grouping
   // -----------------------------------------------------------------------

   std::printf ("\n[Test 5] Grouping\n");

   pConsole->Group (&cid, "Group A");
   pConsole->Log   (&cid, "Inside group A");
   pConsole->GroupEnd (&cid);
   pConsole->Log   (&cid, "Outside group A");

   ENTRY_COLLECTOR collector4;
   pConsole->Entry_Enum (&collector4);
   size_t nBase = 7;
   ASSERT (collector4.m_aEntry[nBase + 0]->GroupDepth () == 0, "Group label at depth 0");
   ASSERT (collector4.m_aEntry[nBase + 1]->GroupDepth () == 1, "Entry inside group at depth 1");
   ASSERT (collector4.m_aEntry[nBase + 2]->GroupDepth () == 0, "Entry after GroupEnd at depth 0");

   // -----------------------------------------------------------------------
   // Test 6: Counting
   // -----------------------------------------------------------------------

   std::printf ("\n[Test 6] Counting\n");

   pConsole->Count (&cid, "clicks");
   pConsole->Count (&cid, "clicks");
   pConsole->Count (&cid, "clicks");

   ENTRY_COLLECTOR collector5;
   pConsole->Entry_Enum (&collector5);
   size_t nCountBase = nBase + 3;
   ASSERT (collector5.m_aEntry[nCountBase + 0]->Message () == "clicks: 1", "First count is 1");
   ASSERT (collector5.m_aEntry[nCountBase + 1]->Message () == "clicks: 2", "Second count is 2");
   ASSERT (collector5.m_aEntry[nCountBase + 2]->Message () == "clicks: 3", "Third count is 3");

   pConsole->CountReset (&cid, "clicks");
   pConsole->Count (&cid, "clicks");

   ENTRY_COLLECTOR collector5b;
   pConsole->Entry_Enum (&collector5b);
   size_t nResetBase = nCountBase + 3;
   ASSERT (collector5b.m_aEntry[nResetBase]->Message () == "clicks: 1", "Count reset to 1");

   // -----------------------------------------------------------------------
   // Test 7: Timing
   // -----------------------------------------------------------------------

   std::printf ("\n[Test 7] Timing\n");

   pConsole->Time (&cid, "op");
   std::this_thread::sleep_for (std::chrono::milliseconds (50));
   pConsole->TimeEnd (&cid, "op");

   ENTRY_COLLECTOR collector6;
   pConsole->Entry_Enum (&collector6);
   auto pTimerEntry = collector6.m_aEntry.back ();
   ASSERT (pTimerEntry->Message ().find ("op:") != std::string::npos, "Timer entry contains label");
   ASSERT (pTimerEntry->Message ().find ("ms") != std::string::npos, "Timer entry contains ms");

   // -----------------------------------------------------------------------
   // Test 8: Clear
   // -----------------------------------------------------------------------

   std::printf ("\n[Test 8] Clear\n");

   int nDeletesBefore = contextHost.m_nClearCount;
   ENTRY_COLLECTOR collectorPre;
   pConsole->Entry_Enum (&collectorPre);
   int nEntriesInRing = static_cast<int> (collectorPre.m_aEntry.size ());
   pConsole->Clear ();
   ASSERT (contextHost.m_nClearCount - nDeletesBefore == nEntriesInRing, "OnConsoleEntryDeleted fired per entry");

   ENTRY_COLLECTOR collector7;
   pConsole->Entry_Enum (&collector7);
   ASSERT (collector7.m_aEntry.empty (), "Ring buffer cleared");

   // -----------------------------------------------------------------------
   // Test 9: ENTRY serialization round-trip
   // -----------------------------------------------------------------------

   std::printf ("\n[Test 9] ENTRY serialization round-trip\n");

   pConsole->Log (&cid, "Serialize me");

   ENTRY_COLLECTOR collector8;
   pConsole->Entry_Enum (&collector8);
   ASSERT (!collector8.m_aEntry.empty (), "Entry exists for serialization");

   if (!collector8.m_aEntry.empty ())
   {
      auto pOriginal = collector8.m_aEntry.front ();
      nlohmann::json jEntry = pOriginal->ToJson ();
      auto pDeserialized = CONSOLE::ENTRY::FromJson (jEntry, &cid);

      ASSERT (pDeserialized != nullptr, "Deserialized entry is non-null");
      ASSERT (pDeserialized->Level ()   == pOriginal->Level (),   "Level round-trips");
      ASSERT (pDeserialized->Message () == pOriginal->Message (), "Message round-trips");
      ASSERT (pDeserialized->Index ()   == pOriginal->Index (),   "Index round-trips");
   }

   // -----------------------------------------------------------------------
   // Test 10: Channel load/unload (disk persistence)
   // -----------------------------------------------------------------------

   std::printf ("\n[Test 10] Channel load/unload\n");

   pConsole->Clear ();

   pConsole->Log (&cid, "Disk entry 1");
   pConsole->Log (&cid, "Disk entry 2");
   pConsole->Log (&cid, "Disk entry 3");

   pConsole->Channel_Load (&cid);

   ENTRY_COLLECTOR collector9;
   pConsole->Entry_Enum (&cid, &collector9);
   ASSERT (collector9.m_aEntry.size () == 3, "Channel loaded 3 entries from disk");

   pConsole->Channel_Unload (&cid);

   ENTRY_COLLECTOR collector10;
   pConsole->Entry_Enum (&cid, &collector10);
   ASSERT (collector10.m_aEntry.empty (), "Channel unloaded");

   // -----------------------------------------------------------------------
   // Test 11: Configuration
   // -----------------------------------------------------------------------

   std::printf ("\n[Test 11] Configuration\n");

   ASSERT (pConsole->EntriesPerBlock () == 4096,  "Default EntriesPerBlock is 4096");
   ASSERT (pConsole->MaxBlocks ()       == 5,     "Default MaxBlocks is 5");
   ASSERT (pConsole->MaxRingEntries ()  == 16384, "Default MaxRingEntries is 16384");

   pConsole->EntriesPerBlock (100);
   ASSERT (pConsole->EntriesPerBlock () == 100, "EntriesPerBlock changed to 100");

   pConsole->MaxBlocks (3);
   ASSERT (pConsole->MaxBlocks () == 3, "MaxBlocks changed to 3");

   pConsole->MaxRingEntries (50);
   ASSERT (pConsole->MaxRingEntries () == 50, "MaxRingEntries changed to 50");

   // -----------------------------------------------------------------------
   // Test 12: Ring buffer cap
   // -----------------------------------------------------------------------

   std::printf ("\n[Test 12] Ring buffer cap\n");

   pConsole->Clear ();
   pConsole->MaxRingEntries (10);

   for (int n = 0; n < 20; ++n)
      pConsole->Log (&cid, "Entry " + std::to_string (n));

   ENTRY_COLLECTOR collector11;
   pConsole->Entry_Enum (&collector11);
   ASSERT (collector11.m_aEntry.size () == 10, "Ring buffer capped at 10");
   ASSERT (collector11.m_aEntry.front ()->Message () == "Entry 10", "Oldest entry is Entry 10 (first 10 evicted)");

   // -----------------------------------------------------------------------
   // Test 13: LevelString
   // -----------------------------------------------------------------------

   std::printf ("\n[Test 13] LevelString\n");

   ASSERT (std::string (CONSOLE::ENTRY::LevelString (CONSOLE::kLEVEL_LOG))   == "log",   "LOG -> log");
   ASSERT (std::string (CONSOLE::ENTRY::LevelString (CONSOLE::kLEVEL_DEBUG)) == "debug", "DEBUG -> debug");
   ASSERT (std::string (CONSOLE::ENTRY::LevelString (CONSOLE::kLEVEL_INFO))  == "info",  "INFO -> info");
   ASSERT (std::string (CONSOLE::ENTRY::LevelString (CONSOLE::kLEVEL_WARN))  == "warn",  "WARN -> warn");
   ASSERT (std::string (CONSOLE::ENTRY::LevelString (CONSOLE::kLEVEL_ERROR)) == "error", "ERROR -> error");

   // -----------------------------------------------------------------------
   // Cleanup
   // -----------------------------------------------------------------------

   engine.Context_Close (pContext);

   std::printf ("\n  --- Results: %d passed, %d failed ---\n", g_nPassed, g_nFailed);

   return (g_nFailed > 0) ? 1 : 0;
}
