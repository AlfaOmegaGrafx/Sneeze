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

#ifndef SNEEZE_SPV_PIPELINE_H
#define SNEEZE_SPV_PIPELINE_H

#include <string>
#include <vector>
#include <cstdint>

class SNEEZE;

namespace spirv
{

class SPV_PIPELINE
{
public:
   SPV_PIPELINE ();
   ~SPV_PIPELINE ();

   bool Initialize (SNEEZE* pSneeze);
   void Shutdown ();

   bool Validate (const std::vector<uint32_t>& aBinary, std::string& sError);

private:
   SNEEZE* m_pSneeze;
   bool bInitialized;
};

} // namespace spirv

#endif // SNEEZE_SPV_PIPELINE_H
