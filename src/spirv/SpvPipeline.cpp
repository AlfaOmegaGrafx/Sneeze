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

#include "spirv/SpvPipeline.h"

#include <spirv-tools/libspirv.hpp>
#include <spirv_cross/spirv_hlsl.hpp>
#include <spirv_cross/spirv_glsl.hpp>

#include <cstdio>

namespace rubidium
{
namespace spirv
{

SPV_PIPELINE::SPV_PIPELINE ()
   : bInitialized (false)
{
}

SPV_PIPELINE::~SPV_PIPELINE ()
{
   Shutdown ();
}

bool SPV_PIPELINE::Initialize ()
{
   bInitialized = true;
   std::printf ("SPV_PIPELINE: SPIR-V pipeline initialized (SPIRV-Tools + SPIRV-Cross)\n");
   return true;
}

void SPV_PIPELINE::Shutdown ()
{
   bInitialized = false;
}

bool SPV_PIPELINE::Validate (const std::vector<uint32_t>& aBinary, std::string& sError)
{
   spvtools::SpirvTools pTools (SPV_ENV_VULKAN_1_3);

   std::string sMessages;
   pTools.SetMessageConsumer (
      [&sMessages] (spv_message_level_t, const char*, const spv_position_t&, const char* msg)
      {
         sMessages += msg;
         sMessages += "\n";
      });

   bool bValid = pTools.Validate (aBinary);
   if (!bValid)
      sError = sMessages;

   return bValid;
}

bool SPV_PIPELINE::CrossCompileToHLSL (const std::vector<uint32_t>& aBinary, std::string& sHLSL, std::string& sError)
{
   bool bOk = false;
   try
   {
      spirv_cross::CompilerHLSL pCompiler (aBinary);
      spirv_cross::CompilerHLSL::Options pOptions;
      pOptions.shader_model = 50;
      pCompiler.set_hlsl_options (pOptions);
      sHLSL = pCompiler.compile ();
      bOk = true;
   }
   catch (const spirv_cross::CompilerError& e)
   {
      sError = e.what ();
   }
   return bOk;
}

bool SPV_PIPELINE::CrossCompileToGLSL (const std::vector<uint32_t>& aBinary, std::string& sGLSL, std::string& sError)
{
   bool bOk = false;
   try
   {
      spirv_cross::CompilerGLSL pCompiler (aBinary);
      spirv_cross::CompilerGLSL::Options pOptions;
      pOptions.version = 450;
      pOptions.es = false;
      pCompiler.set_common_options (pOptions);
      sGLSL = pCompiler.compile ();
      bOk = true;
   }
   catch (const spirv_cross::CompilerError& e)
   {
      sError = e.what ();
   }
   return bOk;
}

} // namespace spirv
} // namespace rubidium
