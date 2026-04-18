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

#ifndef SNEEZE_JWS_JWSSERVICE_H
#define SNEEZE_JWS_JWSSERVICE_H

#include "JwsBase.h"

#include <map>
#include <string>
#include <vector>

namespace sneeze
{
namespace jws
{

struct MSS_MODULE
{
   std::string sUrl;
   std::string sSha256;
};

struct MSS_SERVICE
{
   std::string              sName;
   std::string              sType;
   std::string              sEndpoint;
   std::vector<std::string> aModules;
};

class JWS_SERVICE : public JWS_BASE
{
public:
   JWS_SERVICE ();
   ~JWS_SERVICE () override;

   std::string                          GetNamespace () const;
   std::string                          GetOrganization () const;
   std::vector<MSS_SERVICE>             GetServices () const;
   std::map<std::string, MSS_MODULE>    GetModules () const;
   std::string                          GetSuccessor () const;
};

} // namespace jws
} // namespace sneeze

#endif // SNEEZE_JWS_JWSSERVICE_H
