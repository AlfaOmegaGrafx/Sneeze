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

#ifndef SNEEZE_JWS_JWSFABRIC_H
#define SNEEZE_JWS_JWSFABRIC_H

#include "JwsBase.h"

#include <string>
#include <vector>

namespace sneeze
{
namespace jws
{

// MSF payload schema is not yet fully defined (open question in the
// architecture doc).  This class is stubbed with basic accessors that
// will be fleshed out once the MSF format stabilises.

class JWS_FABRIC : public JWS_BASE
{
public:
   JWS_FABRIC ();
   ~JWS_FABRIC () override;

   std::vector<std::string> GetServiceRefs () const;
};

} // namespace jws
} // namespace sneeze

#endif // SNEEZE_JWS_JWSFABRIC_H
