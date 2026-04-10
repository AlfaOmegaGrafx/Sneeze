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

#include "compute/EmbeddedKernels.h"

#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <cstring>

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

   auto pKernel = rubidium::compute::GetEmbeddedKernel ("TEST_PROXIMITY");
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

   auto pKernel = rubidium::compute::GetEmbeddedKernel ("DOES_NOT_EXIST");
   Check (pKernel.pBytes == nullptr, "Unknown kernel returns null pointer");
   Check (pKernel.nSize == 0, "Unknown kernel returns zero size");
}

static void TestMultipleRetrievals ()
{
   std::printf ("\n--- Multiple retrievals return same data ---\n");

   auto pKernel1 = rubidium::compute::GetEmbeddedKernel ("TEST_PROXIMITY");
   auto pKernel2 = rubidium::compute::GetEmbeddedKernel ("TEST_PROXIMITY");
   Check (pKernel1.pBytes == pKernel2.pBytes, "Same pointer on repeated retrieval");
   Check (pKernel1.nSize == pKernel2.nSize, "Same size on repeated retrieval");
}

static void TestSpvStructure ()
{
   std::printf ("\n--- SPIR-V structure validation ---\n");

   auto pKernel = rubidium::compute::GetEmbeddedKernel ("TEST_PROXIMITY");
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

int main ()
{
   std::printf ("ComputeTest — SPIR-V Embedded Kernel Integration Tests\n");
   std::printf ("=======================================================\n");

   TestEmbeddedKernelRetrieval ();
   TestUnknownKernel ();
   TestMultipleRetrievals ();
   TestSpvStructure ();

   std::printf ("\n=======================================================\n");
   std::printf ("Results: %d passed, %d failed, %d total\n",
      nPassed, nFailed, nPassed + nFailed);

   return nFailed > 0 ? EXIT_FAILURE : EXIT_SUCCESS;
}
