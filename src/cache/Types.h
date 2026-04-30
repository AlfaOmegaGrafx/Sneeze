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

#ifndef SNEEZE_CACHE_TYPES_H
#define SNEEZE_CACHE_TYPES_H

#include <cstdint>

namespace sneeze { namespace cache {

// ---------------------------------------------------------------------------
// Cache tiers
// ---------------------------------------------------------------------------

enum CACHE_TIER
{
   CACHE_TIER_MSF      = 0,   // Session-only, URL-keyed (.msf files)
   CACHE_TIER_ASSET    = 1,   // Session-only, URL-keyed (.glb, .gltf, etc.)
   CACHE_TIER_MODULE   = 2,   // Persistent, URL+SHA256-keyed (.wasm, .spv)
};

// ---------------------------------------------------------------------------
// Cache entry lifecycle states
// ---------------------------------------------------------------------------

enum ENTRY_STATE
{
   ENTRY_STATE_IDLE       = 0,
   ENTRY_STATE_FETCHING   = 1,
   ENTRY_STATE_VALIDATING = 2,
   ENTRY_STATE_READY      = 3,
   ENTRY_STATE_FAILED     = 4,
};

}} // namespace sneeze::cache

#endif // SNEEZE_CACHE_TYPES_H
