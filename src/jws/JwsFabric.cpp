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

#include "JwsFabric.h"

namespace sneeze
{
namespace jws
{

JWS_FABRIC::JWS_FABRIC ()
{
}

JWS_FABRIC::~JWS_FABRIC ()
{
}

std::vector<std::string> JWS_FABRIC::GetServiceRefs () const
{
   std::vector<std::string> aResult;
   if (!m_result.payload.is_object ()  ||  !m_result.payload.contains ("services"))
      return aResult;

   for (const auto& ref : m_result.payload["services"])
   {
      if (ref.is_string ())
         aResult.push_back (ref.get<std::string> ());
   }
   return aResult;
}

} // namespace jws
} // namespace sneeze
