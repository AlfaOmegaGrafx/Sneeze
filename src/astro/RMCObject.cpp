// Copyright 2026 Open Metaverse Browser Initiative (OMBI)
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

#include "RMCObject.h"

namespace rubidium { namespace astro {

using core::G_M3_KG_S2;

// Static members
std::map<std::string, RMCOBJECT*>  RMCOBJECT::s_pRegistry;
std::vector<RMCOBJECT*>            RMCOBJECT::s_aAll;
RMCOBJECT*                         RMCOBJECT::s_pRoot = nullptr;

// ---------------------------------------------------------------------------

RMCOBJECT::RMCOBJECT (const RMCOBJECT_PROPS& props)
   : sName        (props.sName)
   , sId          (props.sId)
   , bType        (props.bType)
   , pParent      (nullptr)
   , dMass        (props.dMass)
   , dGM          (props.dGM)
   , dGM_m3s2     (props.dMass.has_value () ? *props.dMass * G_M3_KG_S2 : 0.0)
   , dRadius      (props.dRadius)
   , dSystemRadiusKm (props.dSystemRadiusKm)
   , dBound       (0.0)
   , pColor       (props.pColor)
{
   // Parent wiring
   if (!props.sId_Parent.empty ())
   {
      pParent = RMCOBJECT::Find (props.sId_Parent);
   }

   if (pParent)
   {
      pParent->aChildren.push_back (this);
   }

   // Register
   if (!sId.empty ())
   {
      s_pRegistry[sId] = this;
   }
   s_aAll.push_back (this);

   // Track root
   if (!pParent  &&  s_pRoot == nullptr)
   {
      s_pRoot = this;
   }

   // Compose orbit
   if (props.bHasOrbit)
   {
      pOrbit = std::make_unique<ORBIT> (props.orbit);
   }
}

// ---------------------------------------------------------------------------
//  Registry
// ---------------------------------------------------------------------------

RMCOBJECT* RMCOBJECT::Find (const std::string& sId)
{
   auto it = s_pRegistry.find (sId);
   RMCOBJECT* pResult = (it != s_pRegistry.end ()) ? it->second : nullptr;
   return pResult;
}

std::vector<RMCOBJECT*>& RMCOBJECT::All ()
{
   return s_aAll;
}

RMCOBJECT* RMCOBJECT::Root ()
{
   return s_pRoot;
}

// ---------------------------------------------------------------------------
//  ComputeRaw — type-aware dispatcher
// ---------------------------------------------------------------------------

void RMCOBJECT::ComputeRaw ()
{
   switch (bType)
   {
      case RMCOBJECT_TYPE_STARSYSTEM:
      case RMCOBJECT_TYPE_PLANETSYSTEM:
      case RMCOBJECT_TYPE_MOONSYSTEM:
      case RMCOBJECT_TYPE_DEBRISSYSTEM:
      case RMCOBJECT_TYPE_SATELLITE:
         if (pOrbit)
         {
            double dParentGM = pParent ? pParent->dGM_m3s2 : 0.0;
            pOrbit->ComputeOrbital (dParentGM);
         }
         break;

      default:
         break;
   }
}

// ---------------------------------------------------------------------------
//  ConvertToOutput
// ---------------------------------------------------------------------------

void RMCOBJECT::ConvertToOutput ()
{
   if (pOrbit)
   {
      pOrbit->ConvertToOutput ();
   }

   double dBoundKm = dSystemRadiusKm.value_or (dRadius.value_or (0.0));
   if (dBoundKm > 0.0)
   {
      dBound = dBoundKm * 1000.0;
   }
}

// ---------------------------------------------------------------------------

uint32_t RMCOBJECT::GetColor () const
{
   return pColor.nNormal;
}

}} // namespace rubidium::astro
