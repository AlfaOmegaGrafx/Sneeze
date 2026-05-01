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

#ifndef SNEEZE_ASTRO_ASTROSERVICE_H
#define SNEEZE_ASTRO_ASTROSERVICE_H

#include "som/Fabric.h"
#include "som/Node.h"
#include "som/MapObject.h"
#include <vector>
#include <thread>

namespace SNEEZE { namespace CORE { class SNEEZE; }}

namespace SNEEZE { namespace astro {

class RMCOBJECT;
class ORBIT;

// ---------------------------------------------------------------------------
// CELESTIAL_MAP_OBJECT — a MAP_OBJECT_CELESTIAL that also references the
// source RMCOBJECT for orbit computation. This is the bridge between the
// disposable astro proof-of-concept and the SOM.
// ---------------------------------------------------------------------------

class CELESTIAL_MAP_OBJECT : public SNEEZE::som::MAP_OBJECT_CELESTIAL
{
public:
   CELESTIAL_MAP_OBJECT () : m_pBody (nullptr), m_pOrbit (nullptr) {}

   RMCOBJECT* m_pBody;
   ORBIT*     m_pOrbit;
};

// ---------------------------------------------------------------------------
// ASTRO_SERVICE — populates the primary fabric with celestial body nodes.
//
// Creates one SOM::NODE + CELESTIAL_MAP_OBJECT per renderable orbit body.
// Ownership of all nodes and map objects is retained here and destroyed
// on shutdown.
// ---------------------------------------------------------------------------

class ASTRO_SERVICE
{
public:
   explicit ASTRO_SERVICE (CORE::SNEEZE* pSneeze);
   ~ASTRO_SERVICE ();

   bool Initialize (SNEEZE::som::FABRIC* pPrimaryFabric);
   void Shutdown ();

private:
   void FetchTexture (CELESTIAL_MAP_OBJECT* pMapObj);

   CORE::SNEEZE*                           m_pSneeze;
   SNEEZE::som::FABRIC*                    m_pFabric;
   std::vector<SNEEZE::som::NODE*>         m_apNodes;
   std::vector<CELESTIAL_MAP_OBJECT*>      m_apMapObjects;
   std::vector<std::thread>                m_aFetchThreads;
};

}} // namespace SNEEZE::astro

#endif // SNEEZE_ASTRO_ASTROSERVICE_H
