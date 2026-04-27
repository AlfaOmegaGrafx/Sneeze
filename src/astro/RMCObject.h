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

#ifndef SNEEZE_ASTRO_RMCOBJECT_H
#define SNEEZE_ASTRO_RMCOBJECT_H

#include "Orbit.h"
#include "core/Types.h"
#include <string>
#include <vector>
#include <map>
#include <memory>
#include <optional>
#include <cstdint>

namespace sneeze { namespace astro {

enum RMCOBJECT_TYPE
{
   RMCOBJECT_TYPE_NONE           = 0,
   RMCOBJECT_TYPE_UNIVERSE       = 1,
   RMCOBJECT_TYPE_STARSYSTEM     = 9,
   RMCOBJECT_TYPE_STAR           = 10,
   RMCOBJECT_TYPE_PLANETSYSTEM   = 11,
   RMCOBJECT_TYPE_PLANET         = 12,
   RMCOBJECT_TYPE_MOONSYSTEM     = 125,
   RMCOBJECT_TYPE_MOON           = 13,
   RMCOBJECT_TYPE_DEBRISSYSTEM   = 135,
   RMCOBJECT_TYPE_DEBRIS         = 14,
   RMCOBJECT_TYPE_SATELLITE      = 15,
   RMCOBJECT_TYPE_SURFACE        = 17,
};

struct RMCOBJECT_COLOR
{
   uint32_t nNormal;
   uint32_t nDim;
   uint32_t nBright;
};

struct RMCOBJECT_PROPS
{
   std::string           sName;
   std::string           sId;
   std::string           sId_Parent;
   RMCOBJECT_TYPE        bType           = RMCOBJECT_TYPE_NONE;

   std::optional<double> dMass;
   std::optional<double> dGM;
   std::optional<double> dRadius;
   std::optional<double> dSystemRadiusKm;
   std::optional<double> dPoleRA;
   std::optional<double> dPoleDec;
   std::optional<double> dObliquity;

   RMCOBJECT_COLOR       pColor          = { 0xcccccc, 0x666666, 0xffffff };

   ORBIT_PROPS           orbit;
   bool                  bHasOrbit       = false;
};

class RMCOBJECT
{
public:
   explicit RMCOBJECT (const RMCOBJECT_PROPS& props);

   // Identity
   std::string    sName;
   std::string    sId;
   RMCOBJECT_TYPE bType;

   // Hierarchy
   RMCOBJECT*              pParent;
   std::vector<RMCOBJECT*> aChildren;

   // Physical
   std::optional<double>   dMass;
   std::optional<double>   dGM;
   double                  dGM_m3s2;
   std::optional<double>   dRadius;
   std::optional<double>   dSystemRadiusKm;
   double                  dBound;

   // Visual
   RMCOBJECT_COLOR         pColor;

   // Composition
   std::unique_ptr<ORBIT>  pOrbit;

   // --- Registry ---

   static RMCOBJECT*                     Find (const std::string& sId);
   static std::vector<RMCOBJECT*>&       All ();
   static RMCOBJECT*                     Root ();

   // --- Computation ---

   void ComputeRaw ();
   void ConvertToOutput ();
   uint32_t GetColor () const;

private:
   static std::map<std::string, RMCOBJECT*>  s_pRegistry;
   static std::vector<RMCOBJECT*>            s_aAll;
   static RMCOBJECT*                         s_pRoot;
};

}} // namespace sneeze::astro

#endif // SNEEZE_ASTRO_RMCOBJECT_H
