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

#ifndef SNEEZE_SOM_SCENE_H
#define SNEEZE_SOM_SCENE_H

namespace SNEEZE { namespace CORE { class SNEEZE; }}

namespace SNEEZE { namespace som {

// ---------------------------------------------------------------------------
// SOM::SCENE — root container for the scene object model.
//
// Owned by CORE::SNEEZE. Every FABRIC in the scene holds a back-pointer to
// the SCENE, giving any NODE a path to engine services:
//     NODE -> FABRIC -> SCENE -> SNEEZE -> Cache(), etc.
// ---------------------------------------------------------------------------

class SCENE
{
public:
   explicit SCENE (CORE::SNEEZE* pSneeze);
   ~SCENE ();

   CORE::SNEEZE* Sneeze () const { return m_pSneeze; }

private:
   CORE::SNEEZE* m_pSneeze;
};

}} // namespace SNEEZE::som

#endif // SNEEZE_SOM_SCENE_H
