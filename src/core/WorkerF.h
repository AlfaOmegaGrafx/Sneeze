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

#ifndef SNEEZE_CORE_WORKER_F_H
#define SNEEZE_CORE_WORKER_F_H

#include "Worker.h"

namespace SNEEZE { namespace CORE {

class WORKER_F : public WORKER
{
public:
   explicit WORKER_F (SNEEZE* pSneeze);

protected:
   void Tick () override;
};

}} // namespace SNEEZE::CORE

#endif // SNEEZE_CORE_WORKER_F_H
