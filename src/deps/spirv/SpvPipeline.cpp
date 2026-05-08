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
#include "spirv/SpvPipeline.h"

#include <spirv-tools/libspirv.hpp>

using namespace SNEEZE::DEP;

SPV_PIPELINE::SPV_PIPELINE ()
   : m_pEngine (nullptr)
   , bInitialized (false)
{
}

SPV_PIPELINE::~SPV_PIPELINE ()
{
   Shutdown ();
}

bool SPV_PIPELINE::Initialize (ENGINE* pEngine)
{
   m_pEngine = pEngine;
   bInitialized = true;
   m_pEngine->Log (ENGINE::IENGINE::kLOGLEVEL_Info, "SPV_PIPELINE",
      "SPIR-V validation pipeline initialized (SPIRV-Tools)");
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
