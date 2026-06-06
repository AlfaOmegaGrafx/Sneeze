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

#include <Sneeze.h>
#include "MapObject.h"

using namespace SNEEZE;

// ---------------------------------------------------------------------------
// MAP_OBJECT
// ---------------------------------------------------------------------------

MAP_OBJECT::MAP_OBJECT (MAP_OBJECT_TYPE_TYPE bType)
{
   m_Type.bType = bType;
}

// ---------------------------------------------------------------------------
// MAP_OBJECT_ROOT
// ---------------------------------------------------------------------------

MAP_OBJECT_ROOT::MAP_OBJECT_ROOT () : MAP_OBJECT (MAP_OBJECT_TYPE_TYPE_ROOT)
{
}

// ---------------------------------------------------------------------------
// MAP_OBJECT_CELESTIAL
// ---------------------------------------------------------------------------

MAP_OBJECT_CELESTIAL::MAP_OBJECT_CELESTIAL () : MAP_OBJECT (MAP_OBJECT_TYPE_TYPE_CELESTIAL)
{
}

bool MAP_OBJECT_CELESTIAL::HasOrbit () const
{
   return m_orbit.dA != 0.0  &&  m_orbit.tmPeriod != 0  &&  m_orbit.bHasQuat;
}

// ---------------------------------------------------------------------------
// MAP_OBJECT_TERRESTRIAL
// ---------------------------------------------------------------------------

MAP_OBJECT_TERRESTRIAL::MAP_OBJECT_TERRESTRIAL () : MAP_OBJECT (MAP_OBJECT_TYPE_TYPE_TERRESTRIAL)
{
}

// ---------------------------------------------------------------------------
// MAP_OBJECT_PHYSICAL
// ---------------------------------------------------------------------------

MAP_OBJECT_PHYSICAL::MAP_OBJECT_PHYSICAL () : MAP_OBJECT (MAP_OBJECT_TYPE_TYPE_PHYSICAL)
{
}
