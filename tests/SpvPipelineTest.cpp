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

#include <spirv-tools/libspirv.hpp>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

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

// Minimal compute shader in SPIR-V assembly text.
// Equivalent to: void main() { } with local_size 1,1,1
static const char* SZ_COMPUTE_SPIRV_ASM =
   "; SPIR-V\n"
   "; Version: 1.0\n"
   "; Generator: Khronos; 0\n"
   "               OpCapability Shader\n"
   "               OpMemoryModel Logical GLSL450\n"
   "               OpEntryPoint GLCompute %main \"main\"\n"
   "               OpExecutionMode %main LocalSize 1 1 1\n"
   "       %void = OpTypeVoid\n"
   "   %fn_void  = OpTypeFunction %void\n"
   "       %main = OpFunction %void None %fn_void\n"
   "      %entry = OpLabel\n"
   "               OpReturn\n"
   "               OpFunctionEnd\n";

// ---------------------------------------------------------------------------
// Test 1: Assemble SPIR-V from text
// ---------------------------------------------------------------------------
static void TestAssemble ()
{
   std::printf ("\n[Test 1] Assemble SPIR-V from text\n");

   spvtools::SpirvTools pTools (SPV_ENV_VULKAN_1_3);

   std::string sMessages;
   pTools.SetMessageConsumer (
      [&sMessages] (spv_message_level_t, const char*, const spv_position_t&, const char* msg)
      {
         sMessages += msg;
         sMessages += "\n";
      });

   std::vector<uint32_t> aBinary;
   bool bOk = pTools.Assemble (SZ_COMPUTE_SPIRV_ASM, std::strlen (SZ_COMPUTE_SPIRV_ASM), &aBinary);
   Check (bOk, "Compute shader assembled");
   Check (aBinary.size () > 4, "Binary has content (>4 words)");
   Check (aBinary[0] == 0x07230203, "SPIR-V magic number correct");

   if (!bOk)
      std::fprintf (stderr, "    %s\n", sMessages.c_str ());
}

// ---------------------------------------------------------------------------
// Test 2: Validate valid SPIR-V
// ---------------------------------------------------------------------------
static void TestValidateGood ()
{
   std::printf ("\n[Test 2] Validate valid SPIR-V\n");

   spvtools::SpirvTools pTools (SPV_ENV_VULKAN_1_3);

   std::vector<uint32_t> aBinary;
   pTools.Assemble (SZ_COMPUTE_SPIRV_ASM, std::strlen (SZ_COMPUTE_SPIRV_ASM), &aBinary);

   bool bValid = pTools.Validate (aBinary);
   Check (bValid, "Valid compute shader passes validation");
}

// ---------------------------------------------------------------------------
// Test 3: Validate invalid SPIR-V (reject bad bytecode)
// ---------------------------------------------------------------------------
static void TestValidateBad ()
{
   std::printf ("\n[Test 3] Validate invalid SPIR-V (error handling)\n");

   spvtools::SpirvTools pTools (SPV_ENV_VULKAN_1_3);

   std::string sError;
   pTools.SetMessageConsumer (
      [&sError] (spv_message_level_t, const char*, const spv_position_t&, const char* msg)
      {
         sError += msg;
      });

   // Garbage data with valid magic number
   std::vector<uint32_t> aBadBinary = { 0x07230203, 0x00010000, 0, 0, 0, 0xDEADBEEF };
   bool bValid = pTools.Validate (aBadBinary);
   Check (!bValid, "Invalid SPIR-V correctly rejected");
}

// ---------------------------------------------------------------------------
// Test 4: Disassemble SPIR-V binary back to text
// ---------------------------------------------------------------------------
static void TestDisassemble ()
{
   std::printf ("\n[Test 4] Disassemble SPIR-V binary to text\n");

   spvtools::SpirvTools pTools (SPV_ENV_VULKAN_1_3);
   std::vector<uint32_t> aBinary;
   pTools.Assemble (SZ_COMPUTE_SPIRV_ASM, std::strlen (SZ_COMPUTE_SPIRV_ASM), &aBinary);

   std::string sText;
   bool bOk = pTools.Disassemble (aBinary, &sText);
   Check (bOk, "Disassembly succeeded");
   Check (sText.find ("OpEntryPoint GLCompute") != std::string::npos,
      "Disassembly contains OpEntryPoint GLCompute");
   Check (sText.find ("OpReturn") != std::string::npos,
      "Disassembly contains OpReturn");
}

// ---------------------------------------------------------------------------

int RunSpvTests (int /*nArgc*/, char** /*aArgv*/)
{
   std::printf ("=== SPIR-V Integration Test Suite ===\n");

   TestAssemble ();
   TestValidateGood ();
   TestValidateBad ();
   TestDisassemble ();

   std::printf ("\n=== Results: %d passed, %d failed ===\n", nPassed, nFailed);

   return (nFailed > 0) ? 1 : 0;
}
