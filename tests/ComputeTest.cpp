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

#include "compute/EmbeddedKernels.h"
#include "compute/ComputeDispatch.h"

#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <cstring>
#include <cmath>

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

static void TestEmbeddedKernelRetrieval ()
{
   std::printf ("\n--- Embedded kernel retrieval ---\n");

   auto pKernel = SNEEZE::compute::GetEmbeddedKernel ("TEST_PROXIMITY");
   Check (pKernel.pBytes != nullptr, "Kernel pointer is non-null");
   Check (pKernel.nSize > 0, "Kernel size is non-zero");

   if (pKernel.pBytes  &&  pKernel.nSize >= 4)
   {
      uint32_t nMagic = 0;
      std::memcpy (&nMagic, pKernel.pBytes, sizeof (uint32_t));
      Check (nMagic == 0x07230203, "SPIR-V magic number correct (0x07230203)");
      std::printf ("    Kernel size: %zu bytes\n", pKernel.nSize);
   }
   else
   {
      Check (false, "SPIR-V magic number correct (0x07230203)");
   }
}

static void TestUnknownKernel ()
{
   std::printf ("\n--- Unknown kernel lookup ---\n");

   auto pKernel = SNEEZE::compute::GetEmbeddedKernel ("DOES_NOT_EXIST");
   Check (pKernel.pBytes == nullptr, "Unknown kernel returns null pointer");
   Check (pKernel.nSize == 0, "Unknown kernel returns zero size");
}

static void TestMultipleRetrievals ()
{
   std::printf ("\n--- Multiple retrievals return same data ---\n");

   auto pKernel1 = SNEEZE::compute::GetEmbeddedKernel ("TEST_PROXIMITY");
   auto pKernel2 = SNEEZE::compute::GetEmbeddedKernel ("TEST_PROXIMITY");
   Check (pKernel1.pBytes == pKernel2.pBytes, "Same pointer on repeated retrieval");
   Check (pKernel1.nSize == pKernel2.nSize, "Same size on repeated retrieval");
}

static void TestSpvStructure ()
{
   std::printf ("\n--- SPIR-V structure validation ---\n");

   auto pKernel = SNEEZE::compute::GetEmbeddedKernel ("TEST_PROXIMITY");
   if (!pKernel.pBytes  ||  pKernel.nSize < 20)
   {
      Check (false, "Kernel too small for SPIR-V header");
   }
   else
   {
      const uint32_t* pWords = reinterpret_cast<const uint32_t*> (pKernel.pBytes);
      Check (pWords[0] == 0x07230203, "Magic number");

      uint32_t nVersionMajor = (pWords[1] >> 16) & 0xFF;
      uint32_t nVersionMinor = (pWords[1] >> 8) & 0xFF;
      Check (nVersionMajor >= 1, "SPIR-V version major >= 1");
      std::printf ("    SPIR-V version: %u.%u\n", nVersionMajor, nVersionMinor);

      uint32_t nBound = pWords[3];
      Check (nBound > 0, "ID bound is non-zero");
      std::printf ("    ID bound: %u\n", nBound);

      Check (pKernel.nSize % 4 == 0, "Size is word-aligned (multiple of 4)");
   }
}

static void TestDispatchConstruction ()
{
   std::printf ("\n--- Compute dispatch construction ---\n");

   SNEEZE::compute::COMPUTE_DISPATCH pDispatch;

   // With Vox wired in, native compute availability is a property of
   // the host machine (GPU drivers present). We can't assert either way
   // portably, but construction must not throw and the flag must be
   // queryable.
   bool bNative = pDispatch.SupportsNativeCompute ();
   Check (true, "Dispatch construction succeeded");
   std::printf ("    Native compute (Vox) available: %s\n", bNative ? "yes" : "no");
}

static void TestProximityDispatch ()
{
   std::printf ("\n--- Proximity kernel dispatch (CPU fallback) ---\n");

   SNEEZE::compute::COMPUTE_DISPATCH pDispatch (nullptr);

   float aPositions[] = {
      3.0f, 0.0f, 0.0f, 1.0f,
      0.0f, 4.0f, 0.0f, 1.0f,
      3.0f, 4.0f, 0.0f, 1.0f,
      1.0f, 1.0f, 1.0f, 1.0f
   };

   float aDistances[4] = { -1.0f, -1.0f, -1.0f, -1.0f };

   SNEEZE::compute::BUFFER_BINDING aBindings[2];
   aBindings[0] = { 0, aPositions, sizeof (aPositions), true };
   aBindings[1] = { 1, aDistances, sizeof (aDistances), false };

   struct { float dX, dY, dZ, dW; uint32_t nCount; } pushConstants =
      { 0.0f, 0.0f, 0.0f, 0.0f, 4 };

   bool bResult = pDispatch.Dispatch (
      "TEST_PROXIMITY", 1, 1, 1,
      aBindings, 2,
      &pushConstants, sizeof (pushConstants)
   );

   Check (bResult, "Dispatch returned true");

   float dTolerance = 0.001f;
   Check (std::fabs (aDistances[0] - 3.0f) < dTolerance, "Distance[0] = 3.0 (single axis)");
   Check (std::fabs (aDistances[1] - 4.0f) < dTolerance, "Distance[1] = 4.0 (single axis)");
   Check (std::fabs (aDistances[2] - 5.0f) < dTolerance, "Distance[2] = 5.0 (3-4-5 triangle)");
   Check (std::fabs (aDistances[3] - std::sqrt (3.0f)) < dTolerance, "Distance[3] = sqrt(3) (diagonal)");

   std::printf ("    Results: %.3f, %.3f, %.3f, %.3f\n",
      aDistances[0], aDistances[1], aDistances[2], aDistances[3]);
}

static void TestNonOriginQuery ()
{
   std::printf ("\n--- Proximity dispatch with non-origin query point ---\n");

   SNEEZE::compute::COMPUTE_DISPATCH pDispatch (nullptr);

   float aPositions[] = {
      10.0f, 0.0f, 0.0f, 1.0f,
       0.0f, 0.0f, 0.0f, 1.0f
   };

   float aDistances[2] = { -1.0f, -1.0f };

   SNEEZE::compute::BUFFER_BINDING aBindings[2];
   aBindings[0] = { 0, aPositions, sizeof (aPositions), true };
   aBindings[1] = { 1, aDistances, sizeof (aDistances), false };

   struct { float dX, dY, dZ, dW; uint32_t nCount; } pushConstants =
      { 5.0f, 0.0f, 0.0f, 0.0f, 2 };

   pDispatch.Dispatch (
      "TEST_PROXIMITY", 1, 1, 1,
      aBindings, 2,
      &pushConstants, sizeof (pushConstants)
   );

   float dTolerance = 0.001f;
   Check (std::fabs (aDistances[0] - 5.0f) < dTolerance, "Distance from (5,0,0) to (10,0,0) = 5.0");
   Check (std::fabs (aDistances[1] - 5.0f) < dTolerance, "Distance from (5,0,0) to (0,0,0) = 5.0");
}

static void TestUnknownKernelDispatch ()
{
   std::printf ("\n--- Unknown kernel dispatch ---\n");

   SNEEZE::compute::COMPUTE_DISPATCH pDispatch (nullptr);

   bool bResult = pDispatch.Dispatch ("NONEXISTENT_KERNEL", 1, 1, 1, nullptr, 0, nullptr, 0);
   Check (!bResult, "Dispatch of unknown kernel returns false");
}

int RunComputeTests (int /*nArgc*/, char** /*aArgv*/)
{
   std::printf ("ComputeTest - SPIR-V Compute Integration Tests\n");
   std::printf ("=======================================================\n");

   TestEmbeddedKernelRetrieval ();
   TestUnknownKernel ();
   TestMultipleRetrievals ();
   TestSpvStructure ();
   TestDispatchConstruction ();
   TestProximityDispatch ();
   TestNonOriginQuery ();
   TestUnknownKernelDispatch ();

   std::printf ("\n=======================================================\n");
   std::printf ("Results: %d passed, %d failed, %d total\n",
      nPassed, nFailed, nPassed + nFailed);

   return nFailed > 0 ? EXIT_FAILURE : EXIT_SUCCESS;
}
