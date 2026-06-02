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

#ifndef SNEEZE_SCENE_ORBIT_H
#define SNEEZE_SCENE_ORBIT_H

#include "Celestial.h"
#include "Epoch.h"
#include <optional>

namespace SNEEZE
{
   struct ORBIT_PROPS
   {
      std::optional<double> dSemiMajorAU;
      std::optional<double> dEccentricity;
      std::optional<double> dInclination;
      std::optional<double> dLonAscNode;
      std::optional<double> dLonPerihelion;
      std::optional<double> dMeanLongitude;
      std::optional<double> dArgPerihelion;
      std::optional<double> dMeanAnomaly;
      std::optional<double> dPerihelionAU;

      std::optional<double> dSemiMajorAUDot;
      std::optional<double> dEccentricityDot;
      std::optional<double> dInclinationDot;
      std::optional<double> dLonAscNodeDot;
      std::optional<double> dLonPerihelionDot;
      std::optional<double> dMeanLongitudeDot;
      std::optional<double> dArgPerihelionDot;

      std::optional<double> dLaplacePoleRA;
      std::optional<double> dLaplacePoleDec;

      std::string           sFrame = "ecliptic";
      std::optional<double> dRefPlaneRA;
      std::optional<double> dRefPlaneDec;

      std::optional<double> dPeriodDays;

      const EPOCH*    pEpoch = nullptr;
   };

   struct ORBIT_POSITION
   {
      double x;
      double y;
      double z;
      double dE;
   };

   class ORBIT : public CELESTIAL
   {
   public:
      ORBIT ();
      explicit ORBIT (const ORBIT_PROPS& props);

      // Source orbital elements
      std::optional<double> dSemiMajorAU;
      std::optional<double> dEccentricity;
      std::optional<double> dInclination;
      std::optional<double> dLonAscNode;
      std::optional<double> dLonPerihelion;
      std::optional<double> dMeanLongitude;
      std::optional<double> dArgPerihelion;
      std::optional<double> dMeanAnomaly;
      std::optional<double> dPerihelionAU;

      // Rates of change per Julian century
      std::optional<double> dSemiMajorAUDot;
      std::optional<double> dEccentricityDot;
      std::optional<double> dInclinationDot;
      std::optional<double> dLonAscNodeDot;
      std::optional<double> dLonPerihelionDot;
      std::optional<double> dMeanLongitudeDot;
      std::optional<double> dArgPerihelionDot;

      // Laplace-plane precession
      std::optional<double> dLaplacePoleRA;
      std::optional<double> dLaplacePoleDec;

      // Reference frame
      std::string           sFrame;
      std::optional<double> dRefPlaneRA;
      std::optional<double> dRefPlaneDec;

      // Direct period specification
      std::optional<double> dPeriodDays;

      // Epoch
      const EPOCH*    pEpoch;

      // Derived raw values
      std::optional<double> dSemiMinorAU;
      std::optional<double> dPeriodSeconds;
      std::optional<double> dPeriodYears;

      // MSF output values
      double  dA;
      double  dB;
      int64_t tmPeriod;
      int64_t tmStart;

      // --- Static solvers ---

      static double SolveKepler (double dM_rad, double dEcc);

      // --- Pipeline ---

      void ComputeOrbital (double dParentGM_m3s2);
      void DeriveElements ();
      void ComputeOrbitalQuaternion ();
      void ComputePrecession ();
      void ComputeDerivedRaw (double dParentGM_m3s2);
      void ConvertToOutput ();

      ORBIT_POSITION* PositionAtTick (int64_t tmNow, ORBIT_POSITION& out) const;
      VEC3      PointOnOrbit (double dE, int64_t tmElapsed) const;
   };
}
#endif // SNEEZE_SCENE_ORBIT_H
