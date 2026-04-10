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

#ifndef SNEEZE_WASM_RUNTIME_H
#define SNEEZE_WASM_RUNTIME_H

#include <wasmtime.h>

namespace sneeze
{
namespace wasm
{

class WASM_RUNTIME
{
public:
   WASM_RUNTIME ();
   ~WASM_RUNTIME ();

   bool Initialize ();
   void Shutdown ();

private:
   wasm_engine_t*     pEngine;
   wasmtime_store_t*  pStore;
};

} // namespace wasm
} // namespace sneeze

#endif // SNEEZE_WASM_RUNTIME_H
