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

#ifndef SNEEZE_CACHE_STORE_H
#define SNEEZE_CACHE_STORE_H

#include <string>

namespace SNEEZE { namespace CACHE {

// ---------------------------------------------------------------------------
// STORE — identity record for a WASM store (container) that requested cached
// files. Owned by MANAGER, outlives the container itself so FILE pointers
// remain valid after the container is unloaded.
//
// Currently holds only a display name. Additional properties (fingerprint,
// container, persona, company) will be added as they become available.
// ---------------------------------------------------------------------------

class STORE
{
public:
   std::string sName;
};

}} // namespace SNEEZE::CACHE

#endif // SNEEZE_CACHE_STORE_H
