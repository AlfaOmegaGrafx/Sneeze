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

#include "../src/jws/JwsBase.h"
#include "../src/jws/JwsService.h"
#include "../src/jws/JwsFabric.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>

// ---------------------------------------------------------------------------
// Test framework
// ---------------------------------------------------------------------------

static int g_nTests  = 0;
static int g_nPassed = 0;

#define ASSERT(expr, msg) \
   do { \
      g_nTests++; \
      if (expr) { g_nPassed++; printf ("  PASS: %s\n", msg); } \
      else      { printf ("  FAIL: %s  [%s]\n", msg, #expr); } \
   } while (0)

static void BeginGroup (const char* sName)
{
   printf ("\n--- %s ---\n", sName);
}

// ---------------------------------------------------------------------------
// File I/O
// ---------------------------------------------------------------------------

static std::string ReadFile (const std::string& sPath)
{
   std::ifstream ifs (sPath, std::ios::binary);
   std::string sResult;
   if (ifs.is_open ())
   {
      std::ostringstream oss;
      oss << ifs.rdbuf ();
      sResult = oss.str ();
   }
   return sResult;
}

// ---------------------------------------------------------------------------
// Main
// ---------------------------------------------------------------------------

int main (int nArgc, char** aArgv)
{
   setvbuf (stdout, nullptr, _IONBF, 0);
   setvbuf (stderr, nullptr, _IONBF, 0);

   std::string sCertsDir = "tests/certs";
   if (nArgc > 1)
      sCertsDir = aArgv[1];

   printf ("JWS Integration Tests\n");
   fflush (stdout);
   printf ("Certs directory: %s\n", sCertsDir.c_str ());
   fflush (stdout);

   printf ("Loading certs...\n"); fflush (stdout);
   std::string sCaKey       = ReadFile (sCertsDir + "/ca-key.pem");
   std::string sCaCert      = ReadFile (sCertsDir + "/ca-cert.pem");
   std::string sProviderKey  = ReadFile (sCertsDir + "/provider-key.pem");
   std::string sProviderCert = ReadFile (sCertsDir + "/provider-cert.pem");
   std::string sExpiredKey   = ReadFile (sCertsDir + "/expired-key.pem");
   std::string sExpiredCert  = ReadFile (sCertsDir + "/expired-cert.pem");
   printf ("Certs loaded.\n"); fflush (stdout);

   ASSERT (!sCaKey.empty (),       "CA key loaded");
   ASSERT (!sCaCert.empty (),      "CA cert loaded");
   ASSERT (!sProviderKey.empty (), "Provider key loaded");
   ASSERT (!sProviderCert.empty (),"Provider cert loaded");

   // -----------------------------------------------------------------------
   // Test 1: Sign and verify round-trip
   // -----------------------------------------------------------------------

   BeginGroup ("Sign and Verify Round-Trip");

   std::string sPayload = R"({"namespace":"com.test.poker","organization":"Test Provider Inc."})";

   std::vector<std::string> aCertChain;
   aCertChain.push_back (sProviderCert);
   aCertChain.push_back (sCaCert);

   printf ("Signing...\n"); fflush (stdout);
   std::string sJws = sneeze::jws::JWS_BASE::Sign (sPayload, sProviderKey, aCertChain, "RS256");
   printf ("Sign returned (%zu bytes)\n", sJws.size ()); fflush (stdout);
   ASSERT (!sJws.empty (), "JWS signed successfully");
   ASSERT (sJws.find ('.') != std::string::npos, "JWS contains dot separators");

   sneeze::jws::JWS_BASE verifier;
   verifier.GetCertChain ().AddTrustedCert (sCaCert);
   sneeze::jws::JWS_RESULT result = verifier.Verify (sJws);
   ASSERT (result.sError.empty (), "Verification succeeded (no error)");
   ASSERT (result.sAlgorithm == "RS256", "Algorithm is RS256");
   ASSERT (!result.sFingerprint.empty (), "Signer fingerprint computed");

   printf ("  Fingerprint: %s\n", result.sFingerprint.c_str ());

   // -----------------------------------------------------------------------
   // Test 2: JWS_SERVICE parses MSS payload
   // -----------------------------------------------------------------------

   BeginGroup ("JWS_SERVICE Payload Parsing");

   std::string sMssPayload = R"({
      "namespace": "com.pokerstars.poker",
      "organization": "PokerStars",
      "services": [
         {
            "name": "game-server",
            "type": "websocket",
            "endpoint": "wss://rt.pokerstars.com/game",
            "modules": ["game-client.wasm"]
         }
      ],
      "modules": {
         "game-client.wasm": {
            "url": "https://cdn.pokerstars.com/modules/game-client.wasm",
            "sha256": "a1b2c3d4e5f6"
         }
      },
      "successor": "deadbeef0123456789abcdef"
   })";

   std::string sMssJws = sneeze::jws::JWS_BASE::Sign (sMssPayload, sProviderKey, aCertChain, "RS256");
   ASSERT (!sMssJws.empty (), "MSS JWS signed successfully");

   sneeze::jws::JWS_SERVICE svc;
   svc.GetCertChain ().AddTrustedCert (sCaCert);
   sneeze::jws::JWS_RESULT svcResult = svc.Verify (sMssJws);
   ASSERT (svcResult.sError.empty (), "MSS verification succeeded");

   ASSERT (svc.GetNamespace () == "com.pokerstars.poker", "Namespace parsed correctly");
   ASSERT (svc.GetOrganization () == "PokerStars", "Organization parsed correctly");
   ASSERT (svc.GetSuccessor () == "deadbeef0123456789abcdef", "Successor parsed correctly");
   ASSERT (svcResult.sSuccessorFingerprint == "deadbeef0123456789abcdef", "Successor fingerprint in result");

   auto aServices = svc.GetServices ();
   ASSERT (aServices.size () == 1, "One service declared");
   ASSERT (aServices[0].sName == "game-server", "Service name correct");
   ASSERT (aServices[0].sType == "websocket", "Service type correct");
   ASSERT (aServices[0].sEndpoint == "wss://rt.pokerstars.com/game", "Service endpoint correct");
   ASSERT (aServices[0].aModules.size () == 1, "Service has one module");

   auto aModules = svc.GetModules ();
   ASSERT (aModules.size () == 1, "One module declared");
   ASSERT (aModules.count ("game-client.wasm") == 1, "Module key found");
   ASSERT (aModules["game-client.wasm"].sSha256 == "a1b2c3d4e5f6", "Module sha256 correct");

   // -----------------------------------------------------------------------
   // Test 3: Tampered payload rejected
   // -----------------------------------------------------------------------

   BeginGroup ("Tampered Payload Rejected");

   std::string sTampered = sJws;
   size_t nFirstDot  = sTampered.find ('.');
   size_t nSecondDot = sTampered.find ('.', nFirstDot + 1);
   if (nFirstDot != std::string::npos  &&  nSecondDot != std::string::npos)
   {
      size_t nMid = (nFirstDot + nSecondDot) / 2;
      sTampered[nMid] = (sTampered[nMid] == 'A') ? 'B' : 'A';
   }

   sneeze::jws::JWS_BASE tamperedVerifier;
   tamperedVerifier.GetCertChain ().AddTrustedCert (sCaCert);
   sneeze::jws::JWS_RESULT tamperedResult = tamperedVerifier.Verify (sTampered);
   ASSERT (!tamperedResult.sError.empty (), "Tampered payload rejected");
   printf ("  Error: %s\n", tamperedResult.sError.c_str ());

   // -----------------------------------------------------------------------
   // Test 4: Expired certificate rejected
   // -----------------------------------------------------------------------

   BeginGroup ("Expired Certificate Rejected");

   std::vector<std::string> aExpiredChain;
   aExpiredChain.push_back (sExpiredCert);
   aExpiredChain.push_back (sCaCert);

   std::string sExpiredJws = sneeze::jws::JWS_BASE::Sign (sPayload, sExpiredKey, aExpiredChain, "RS256");
   ASSERT (!sExpiredJws.empty (), "Expired JWS signed successfully");

   sneeze::jws::JWS_BASE expiredVerifier;
   expiredVerifier.GetCertChain ().AddTrustedCert (sCaCert);
   sneeze::jws::JWS_RESULT expiredResult = expiredVerifier.Verify (sExpiredJws);
   ASSERT (!expiredResult.sError.empty (), "Expired certificate rejected");
   printf ("  Error: %s\n", expiredResult.sError.c_str ());

   // -----------------------------------------------------------------------
   // Test 5: Untrusted chain rejected
   // -----------------------------------------------------------------------

   BeginGroup ("Untrusted Chain Rejected");

   sneeze::jws::JWS_BASE untrustedVerifier;
   sneeze::jws::JWS_RESULT untrustedResult = untrustedVerifier.Verify (sJws);
   ASSERT (!untrustedResult.sError.empty (), "Untrusted chain rejected");
   printf ("  Error: %s\n", untrustedResult.sError.c_str ());

   // -----------------------------------------------------------------------
   // Test 6: Fingerprint stability
   // -----------------------------------------------------------------------

   BeginGroup ("Fingerprint Stability");

   sneeze::jws::JWS_BASE verifier2;
   verifier2.GetCertChain ().AddTrustedCert (sCaCert);
   sneeze::jws::JWS_RESULT result2 = verifier2.Verify (sJws);
   ASSERT (result2.sError.empty (), "Second verification succeeded");
   ASSERT (result.sFingerprint == result2.sFingerprint, "Fingerprint is stable across verifications");

   // -----------------------------------------------------------------------
   // Test 7: Malformed JWS rejected
   // -----------------------------------------------------------------------

   BeginGroup ("Malformed JWS Rejected");

   sneeze::jws::JWS_BASE malformedVerifier;

   sneeze::jws::JWS_RESULT emptyResult = malformedVerifier.Verify ("");
   ASSERT (!emptyResult.sError.empty (), "Empty string rejected");

   sneeze::jws::JWS_RESULT garbageResult = malformedVerifier.Verify ("not.a.jws");
   ASSERT (!garbageResult.sError.empty (), "Garbage string rejected");

   sneeze::jws::JWS_RESULT partialResult = malformedVerifier.Verify ("eyJhbGciOiJSUzI1NiJ9");
   ASSERT (!partialResult.sError.empty (), "Partial JWS (one segment) rejected");

   // -----------------------------------------------------------------------
   // Summary
   // -----------------------------------------------------------------------

   printf ("\n========================================\n");
   printf ("JWS Tests: %d/%d passed\n", g_nPassed, g_nTests);
   printf ("========================================\n");

   return (g_nPassed == g_nTests) ? 0 : 1;
}
