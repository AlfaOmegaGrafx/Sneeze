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

#ifndef SNEEZE_VIEWPORT_H
#define SNEEZE_VIEWPORT_H

#include "Sneeze.h"

class SNEEZE::VIEWPORT
{
public:
   class SCENE;
   class CONTAINER;
   class MSF;
   class RENDERER;
   class VIEW;

   explicit VIEWPORT (SNEEZE* pSneeze);
   ~VIEWPORT ();

   SNEEZE* Sneeze () const { return m_pSneeze; }
   SCENE*  GetScene () const { return m_pScene; }
   void    SetScene (SCENE* p) { m_pScene = p; }

private:
   SNEEZE* m_pSneeze;
   SCENE*  m_pScene;
};

#endif // SNEEZE_VIEWPORT_H
