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

#include "JwsService.h"

namespace sneeze
{
namespace jws
{

JWS_SERVICE::JWS_SERVICE ()
{
}

JWS_SERVICE::~JWS_SERVICE ()
{
}

std::string JWS_SERVICE::GetNamespace () const
{
   std::string sResult;
   if (m_result.payload.is_object ()  &&  m_result.payload.contains ("namespace"))
      sResult = m_result.payload["namespace"].get<std::string> ();
   return sResult;
}

std::string JWS_SERVICE::GetOrganization () const
{
   std::string sResult;
   if (m_result.payload.is_object ()  &&  m_result.payload.contains ("organization"))
      sResult = m_result.payload["organization"].get<std::string> ();
   return sResult;
}

std::vector<MSS_SERVICE> JWS_SERVICE::GetServices () const
{
   std::vector<MSS_SERVICE> aResult;
   if (!m_result.payload.is_object ()  ||  !m_result.payload.contains ("services"))
      return aResult;

   for (const auto& svc : m_result.payload["services"])
   {
      MSS_SERVICE entry;
      if (svc.contains ("name"))     entry.sName     = svc["name"].get<std::string> ();
      if (svc.contains ("type"))     entry.sType     = svc["type"].get<std::string> ();
      if (svc.contains ("endpoint")) entry.sEndpoint = svc["endpoint"].get<std::string> ();
      if (svc.contains ("modules"))
      {
         for (const auto& mod : svc["modules"])
            entry.aModules.push_back (mod.get<std::string> ());
      }
      aResult.push_back (entry);
   }
   return aResult;
}

std::map<std::string, MSS_MODULE> JWS_SERVICE::GetModules () const
{
   std::map<std::string, MSS_MODULE> aResult;
   if (!m_result.payload.is_object ()  ||  !m_result.payload.contains ("modules"))
      return aResult;

   for (auto it = m_result.payload["modules"].begin (); it != m_result.payload["modules"].end (); ++it)
   {
      MSS_MODULE mod;
      if (it.value ().contains ("url"))    mod.sUrl    = it.value ()["url"].get<std::string> ();
      if (it.value ().contains ("sha256")) mod.sSha256 = it.value ()["sha256"].get<std::string> ();
      aResult[it.key ()] = mod;
   }
   return aResult;
}

std::string JWS_SERVICE::GetSuccessor () const
{
   std::string sResult;
   if (m_result.payload.is_object ()  &&  m_result.payload.contains ("successor"))
      sResult = m_result.payload["successor"].get<std::string> ();
   return sResult;
}

} // namespace jws
} // namespace sneeze
