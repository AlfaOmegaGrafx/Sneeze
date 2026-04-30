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

#ifndef SNEEZE_SOM_ACCESSCONTROL_H
#define SNEEZE_SOM_ACCESSCONTROL_H

namespace SNEEZE { namespace som {

class NODE;
class FABRIC;

// ---------------------------------------------------------------------------
// Access control enforcement for WASM host function calls.
//
// Browser internals (compositor, animator) bypass these checks entirely.
// Only WASM modules are subject to access control — they must prove they
// own the fabric or that the node/fabric isn't private to another owner.
//
// pRequestingOwner is the opaque owner pointer of the calling store
// (WASM_STORE*). A null owner means browser-internal — always granted.
// ---------------------------------------------------------------------------

bool CanRead (const NODE* pNode, const void* pRequestingOwner);
bool CanWrite (const NODE* pNode, const void* pRequestingOwner);

bool CanReadFabric (const FABRIC* pFabric, const void* pRequestingOwner);
bool CanWriteFabric (const FABRIC* pFabric, const void* pRequestingOwner);

}} // namespace SNEEZE::som

#endif // SNEEZE_SOM_ACCESSCONTROL_H
