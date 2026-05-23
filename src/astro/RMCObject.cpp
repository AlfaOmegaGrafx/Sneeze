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
#include "RMCObject.h"

using namespace SNEEZE::astro;

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
   , sTexture     (props.sTexture)
{
   if (props.bHasOrbit)
   {
      pOrbit = std::make_unique<ORBIT> (props.orbit);
   }
}

// ---------------------------------------------------------------------------
//  ComputeRaw - type-aware dispatcher
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
