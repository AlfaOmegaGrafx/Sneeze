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
//
// Exercises the Vox GPU compute library as consumed through the Sneeze
// static library. Mirrors the upstream VoxTest (MetaversalCorp/Vox) but
// lives here so Sneeze's test suite catches ABI or link-order breakage
// when Vox is rolled forward. The test is tolerant of missing GPU
// backends: on a headless CI runner with no DX12/Vulkan/Metal driver,
// CreateDevice returns null and the buffer / dispatch tests skip cleanly.

#include <vox/Vox.h>

#include <cstdio>

static int nTotal  = 0;
static int nPassed = 0;

#define TEST_ASSERT(expr)                                                      \
   do {                                                                        \
      nTotal++;                                                                \
      if (expr) { nPassed++; }                                                 \
      else { std::printf ("  FAIL: %s (line %d)\n", #expr, __LINE__); }        \
   } while (0)

static const char* BackendName (vox::Backend eBackend)
{
   switch (eBackend)
   {
      case vox::Backend::Vulkan: return "Vulkan";
      case vox::Backend::DX12:   return "DX12";
      case vox::Backend::Metal:  return "Metal";
      default:                   return "Unknown";
   }
}

static void TestCreateDevice ()
{
   std::printf ("--- CreateDevice ---\n");

   vox::DEVICE* pDevice = vox::DEVICE::Create (vox::Backend::Auto);
   if (!pDevice)
   {
      std::printf ("  SKIP: no GPU backend available on this host\n");
      return;
   }

   vox::Backend eBackend = pDevice->GetBackend ();
   TEST_ASSERT (eBackend == vox::Backend::Vulkan ||
                eBackend == vox::Backend::DX12 ||
                eBackend == vox::Backend::Metal);
   std::printf ("  Backend: %s\n", BackendName (eBackend));

   delete pDevice;
}

static void TestCreateBuffer ()
{
   std::printf ("--- CreateBuffer ---\n");

   vox::DEVICE* pDevice = vox::DEVICE::Create ();
   if (!pDevice)
   {
      std::printf ("  SKIP: no GPU backend available on this host\n");
      return;
   }

   vox::BUFFER_DESC desc;
   desc.nSize        = 1024;
   desc.bHostVisible = true;

   vox::BUFFER* pBuffer = pDevice->CreateBuffer (desc);
   TEST_ASSERT (pBuffer != nullptr);

   if (pBuffer)
   {
      float aData[4] = { 1.0f, 2.0f, 3.0f, 4.0f };
      pBuffer->SetData (aData, sizeof (aData));

      float aReadback[4] = {};
      pBuffer->GetData (aReadback, sizeof (aReadback));

      TEST_ASSERT (aReadback[0] == 1.0f);
      TEST_ASSERT (aReadback[1] == 2.0f);
      TEST_ASSERT (aReadback[2] == 3.0f);
      TEST_ASSERT (aReadback[3] == 4.0f);

      pDevice->DestroyBuffer (pBuffer);
   }

   delete pDevice;
}

static void TestNullDevice ()
{
   std::printf ("--- NullDevice ---\n");

   // Ask for a backend that doesn't exist on this platform.
#if defined(__APPLE__)
   vox::DEVICE* pDevice = vox::DEVICE::Create (vox::Backend::DX12);
#else
   vox::DEVICE* pDevice = vox::DEVICE::Create (vox::Backend::Metal);
#endif

   TEST_ASSERT (pDevice == nullptr);
}

int RunVoxTests (int /*nArgc*/, char** /*aArgv*/)
{
   std::printf ("=== VoxTest ===\n\n");

   TestCreateDevice ();
   TestCreateBuffer ();
   TestNullDevice ();

   std::printf ("\n%d / %d passed\n", nPassed, nTotal);

   return (nPassed == nTotal) ? 0 : 1;
}
