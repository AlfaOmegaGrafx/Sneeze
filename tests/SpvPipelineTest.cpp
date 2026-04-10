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

#include <spirv-tools/libspirv.hpp>
#include <spirv_cross/spirv_hlsl.hpp>
#include <spirv_cross/spirv_glsl.hpp>
#include <spirv_cross/spirv_msl.hpp>
#include <spirv_cross/spirv_cross.hpp>

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

// A vertex shader with input/output for cross-compilation testing
static const char* SZ_VERTEX_SPIRV_ASM =
   "; SPIR-V\n"
   "; Version: 1.0\n"
   "; Generator: Khronos; 0\n"
   "               OpCapability Shader\n"
   "               OpMemoryModel Logical GLSL450\n"
   "               OpEntryPoint Vertex %main \"main\" %in_pos %out_pos\n"
   "               OpDecorate %in_pos Location 0\n"
   "               OpDecorate %out_pos BuiltIn Position\n"
   "      %float = OpTypeFloat 32\n"
   "    %v4float = OpTypeVector %float 4\n"
   " %ptr_in_v4  = OpTypePointer Input %v4float\n"
   "%ptr_out_v4  = OpTypePointer Output %v4float\n"
   "     %in_pos = OpVariable %ptr_in_v4 Input\n"
   "    %out_pos = OpVariable %ptr_out_v4 Output\n"
   "       %void = OpTypeVoid\n"
   "   %fn_void  = OpTypeFunction %void\n"
   "       %main = OpFunction %void None %fn_void\n"
   "      %entry = OpLabel\n"
   "        %pos = OpLoad %v4float %in_pos\n"
   "               OpStore %out_pos %pos\n"
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
// Test 4: Cross-compile to HLSL
// ---------------------------------------------------------------------------
static void TestCrossCompileHLSL ()
{
   std::printf ("\n[Test 4] Cross-compile SPIR-V to HLSL\n");

   spvtools::SpirvTools pTools (SPV_ENV_VULKAN_1_3);
   std::vector<uint32_t> aBinary;
   pTools.Assemble (SZ_VERTEX_SPIRV_ASM, std::strlen (SZ_VERTEX_SPIRV_ASM), &aBinary);
   Check (!aBinary.empty (), "Vertex shader assembled");

   try
   {
      spirv_cross::CompilerHLSL pCompiler (aBinary);
      spirv_cross::CompilerHLSL::Options pOptions;
      pOptions.shader_model = 50;
      pCompiler.set_hlsl_options (pOptions);
      std::string sHLSL = pCompiler.compile ();

      Check (!sHLSL.empty (), "HLSL output generated");
      Check (sHLSL.find ("main") != std::string::npos, "HLSL contains 'main' entry point");
      Check (sHLSL.find ("SV_Position") != std::string::npos, "HLSL contains SV_Position semantic");

      std::printf ("    --- HLSL output (%zu bytes) ---\n", sHLSL.size ());
      std::printf ("%s", sHLSL.c_str ());
      std::printf ("    --- end ---\n");
   }
   catch (const spirv_cross::CompilerError& e)
   {
      std::fprintf (stderr, "    SPIRV-Cross error: %s\n", e.what ());
      Check (false, "HLSL cross-compilation succeeded");
   }
}

// ---------------------------------------------------------------------------
// Test 5: Cross-compile to GLSL
// ---------------------------------------------------------------------------
static void TestCrossCompileGLSL ()
{
   std::printf ("\n[Test 5] Cross-compile SPIR-V to GLSL\n");

   spvtools::SpirvTools pTools (SPV_ENV_VULKAN_1_3);
   std::vector<uint32_t> aBinary;
   pTools.Assemble (SZ_VERTEX_SPIRV_ASM, std::strlen (SZ_VERTEX_SPIRV_ASM), &aBinary);

   try
   {
      spirv_cross::CompilerGLSL pCompiler (aBinary);
      spirv_cross::CompilerGLSL::Options pOptions;
      pOptions.version = 450;
      pOptions.es = false;
      pCompiler.set_common_options (pOptions);
      std::string sGLSL = pCompiler.compile ();

      Check (!sGLSL.empty (), "GLSL output generated");
      Check (sGLSL.find ("gl_Position") != std::string::npos, "GLSL contains gl_Position");
      Check (sGLSL.find ("#version 450") != std::string::npos, "GLSL targets version 450");

      std::printf ("    --- GLSL output (%zu bytes) ---\n", sGLSL.size ());
      std::printf ("%s", sGLSL.c_str ());
      std::printf ("    --- end ---\n");
   }
   catch (const spirv_cross::CompilerError& e)
   {
      std::fprintf (stderr, "    SPIRV-Cross error: %s\n", e.what ());
      Check (false, "GLSL cross-compilation succeeded");
   }
}

// ---------------------------------------------------------------------------
// Test 6: Cross-compile to MSL (Metal Shading Language)
// ---------------------------------------------------------------------------
static void TestCrossCompileMSL ()
{
   std::printf ("\n[Test 6] Cross-compile SPIR-V to MSL\n");

   spvtools::SpirvTools pTools (SPV_ENV_VULKAN_1_3);
   std::vector<uint32_t> aBinary;
   pTools.Assemble (SZ_VERTEX_SPIRV_ASM, std::strlen (SZ_VERTEX_SPIRV_ASM), &aBinary);

   try
   {
      spirv_cross::CompilerMSL pCompiler (aBinary);
      std::string sMSL = pCompiler.compile ();

      Check (!sMSL.empty (), "MSL output generated");
      Check (sMSL.find ("vertex") != std::string::npos  ||  sMSL.find ("main0") != std::string::npos,
         "MSL contains entry point");

      std::printf ("    --- MSL output (%zu bytes) ---\n", sMSL.size ());
      std::printf ("%s", sMSL.c_str ());
      std::printf ("    --- end ---\n");
   }
   catch (const spirv_cross::CompilerError& e)
   {
      std::fprintf (stderr, "    SPIRV-Cross error: %s\n", e.what ());
      Check (false, "MSL cross-compilation succeeded");
   }
}

// ---------------------------------------------------------------------------
// Test 7: Shader reflection (extract resource bindings)
// ---------------------------------------------------------------------------
static void TestReflection ()
{
   std::printf ("\n[Test 7] Shader reflection (inspect resources)\n");

   spvtools::SpirvTools pTools (SPV_ENV_VULKAN_1_3);
   std::vector<uint32_t> aBinary;
   pTools.Assemble (SZ_VERTEX_SPIRV_ASM, std::strlen (SZ_VERTEX_SPIRV_ASM), &aBinary);

   try
   {
      spirv_cross::Compiler pCompiler (aBinary);
      spirv_cross::ShaderResources pResources = pCompiler.get_shader_resources ();

      Check (pResources.stage_inputs.size () == 1, "One stage input found (in_pos)");
      Check (pResources.stage_outputs.size () == 0, "No stage outputs (gl_Position is builtin)");

      if (!pResources.stage_inputs.empty ())
      {
         uint32_t nLocation = pCompiler.get_decoration (
            pResources.stage_inputs[0].id, spv::DecorationLocation);
         Check (nLocation == 0, "Input at location 0");
         std::printf ("    Input: '%s' at location %u\n",
            pResources.stage_inputs[0].name.c_str (), nLocation);
      }
   }
   catch (const spirv_cross::CompilerError& e)
   {
      std::fprintf (stderr, "    SPIRV-Cross error: %s\n", e.what ());
      Check (false, "Reflection succeeded");
   }
}

// ---------------------------------------------------------------------------
// Test 8: Disassemble SPIR-V binary back to text
// ---------------------------------------------------------------------------
static void TestDisassemble ()
{
   std::printf ("\n[Test 8] Disassemble SPIR-V binary to text\n");

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

int main (int /*argc*/, char* /*argv*/[])
{
   std::printf ("=== SPIR-V Integration Test Suite ===\n");

   TestAssemble ();
   TestValidateGood ();
   TestValidateBad ();
   TestCrossCompileHLSL ();
   TestCrossCompileGLSL ();
   TestCrossCompileMSL ();
   TestReflection ();
   TestDisassemble ();

   std::printf ("\n=== Results: %d passed, %d failed ===\n", nPassed, nFailed);

   return (nFailed > 0) ? 1 : 0;
}
