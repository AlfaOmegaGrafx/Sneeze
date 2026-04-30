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

#include "Orbit.h"
#include <cmath>

namespace SNEEZE { namespace astro {

using CORE::AU_M;
using CORE::GM_SUN_M3S2;
using CORE::JULIAN_YEAR;
using CORE::TICKS_PER_S;
using CORE::TICKS_PER_CY;
using CORE::PI;
using CORE::TWO_PI;
using CORE::DEG_TO_RAD;
using CORE::EPOCH_J2000;

// ---------------------------------------------------------------------------

ORBIT::ORBIT ()
   : sFrame ("ecliptic")
   , pEpoch (&EPOCH_J2000)
   , dA (0.0), dB (0.0)
   , tmPeriod (0), tmStart (0)
{
}

ORBIT::ORBIT (const ORBIT_PROPS& props)
   : dSemiMajorAU      (props.dSemiMajorAU)
   , dEccentricity     (props.dEccentricity)
   , dInclination      (props.dInclination)
   , dLonAscNode       (props.dLonAscNode)
   , dLonPerihelion    (props.dLonPerihelion)
   , dMeanLongitude    (props.dMeanLongitude)
   , dArgPerihelion    (props.dArgPerihelion)
   , dMeanAnomaly      (props.dMeanAnomaly)
   , dPerihelionAU     (props.dPerihelionAU)
   , dSemiMajorAUDot   (props.dSemiMajorAUDot)
   , dEccentricityDot  (props.dEccentricityDot)
   , dInclinationDot   (props.dInclinationDot)
   , dLonAscNodeDot    (props.dLonAscNodeDot)
   , dLonPerihelionDot (props.dLonPerihelionDot)
   , dMeanLongitudeDot (props.dMeanLongitudeDot)
   , dArgPerihelionDot (props.dArgPerihelionDot)
   , dLaplacePoleRA    (props.dLaplacePoleRA)
   , dLaplacePoleDec   (props.dLaplacePoleDec)
   , sFrame            (props.sFrame)
   , dRefPlaneRA       (props.dRefPlaneRA)
   , dRefPlaneDec      (props.dRefPlaneDec)
   , dPeriodDays       (props.dPeriodDays)
   , pEpoch            (props.pEpoch ? props.pEpoch : &EPOCH_J2000)
   , dA (0.0), dB (0.0)
   , tmPeriod (0), tmStart (0)
{
}

// ---------------------------------------------------------------------------
//  SolveKepler - Newton iteration for Kepler's equation
// ---------------------------------------------------------------------------

double ORBIT::SolveKepler (double dM_rad, double dEcc)
{
   double dE = dEcc > 0.8 ? PI : dM_rad;

   for (int i = 0; i < 50; i++)
   {
      double dDelta = dE - dEcc * std::sin (dE) - dM_rad;
      if (std::abs (dDelta) < 1e-15) break;
      dE -= dDelta / (1.0 - dEcc * std::cos (dE));
   }

   return dE;
}

// ---------------------------------------------------------------------------
//  ComputeOrbital - full pipeline
// ---------------------------------------------------------------------------

void ORBIT::ComputeOrbital (double dParentGM_m3s2)
{
   DeriveElements ();
   ComputeDerivedRaw (dParentGM_m3s2);
   ComputeOrbitalQuaternion ();
   ComputePrecession ();
}

// ---------------------------------------------------------------------------
//  DeriveElements - compute omega and a from source data when not provided
// ---------------------------------------------------------------------------

void ORBIT::DeriveElements ()
{
   if (!dSemiMajorAU.has_value ()  &&  dPerihelionAU.has_value ())
   {
      dSemiMajorAU = *dPerihelionAU / (1.0 - *dEccentricity);
   }

   if (!dArgPerihelion.has_value ()  &&  dLonPerihelion.has_value ())
   {
      dArgPerihelion = *dLonPerihelion - *dLonAscNode;
   }

   if (!dLonPerihelionDot.has_value ()  &&  dArgPerihelionDot.has_value ()  &&  dLonAscNodeDot.has_value ())
   {
      dLonPerihelionDot = *dArgPerihelionDot + *dLonAscNodeDot;
   }

   if (!dMeanAnomaly.has_value ()  &&  dMeanLongitude.has_value ()  &&  dLonPerihelion.has_value ())
   {
      dMeanAnomaly = *dMeanLongitude - *dLonPerihelion;
   }

   if (dArgPerihelion.has_value ())
   {
      double v = std::fmod (*dArgPerihelion, 360.0);
      if (v < 0.0) v += 360.0;
      dArgPerihelion = v;
   }
   if (dMeanAnomaly.has_value ())
   {
      double v = std::fmod (*dMeanAnomaly, 360.0);
      if (v < 0.0) v += 360.0;
      dMeanAnomaly = v;
   }
}

// ---------------------------------------------------------------------------
//  ComputeOrbitalQuaternion - Rz(Omega) x Rx(i) x Rz(omega) in Z-up
// ---------------------------------------------------------------------------

void ORBIT::ComputeOrbitalQuaternion ()
{
   if (dInclination.has_value ()  &&  dLonAscNode.has_value ()  &&  dArgPerihelion.has_value ())
   {
      QUAT q = CELESTIAL::BuildOrbitalQuat (*dLonAscNode, *dInclination, *dArgPerihelion);

      if (sFrame != "ecliptic"  &&  dRefPlaneRA.has_value ()  &&  dRefPlaneDec.has_value ())
      {
         QUAT qFrame = CELESTIAL::FrameToEclipticQuat (*dRefPlaneRA, *dRefPlaneDec);
         q = CELESTIAL::QuatMultiply (qFrame, q);
      }

      dQx = q.dX;
      dQy = q.dY;
      dQz = q.dZ;
      dQw = q.dW;
      bHasQuat = true;
   }
}

// ---------------------------------------------------------------------------
//  ComputePrecession - angular velocity vector (Z-up ecliptic, rad/cy)
// ---------------------------------------------------------------------------

void ORBIT::ComputePrecession ()
{
   if (!dLonAscNodeDot.has_value ()  ||  !bHasQuat) return;

   if (dLaplacePoleRA.has_value ())
   {
      VEC3 vLP = CELESTIAL::EquatorialToEclipticVec (*dLaplacePoleRA, *dLaplacePoleDec);

      double dOmR  = *dLonAscNode * DEG_TO_RAD;
      double dInR  = *dInclination * DEG_TO_RAD;
      double dSinI = std::sin (dInR);
      VEC3 vON = { dSinI * std::sin (dOmR), -dSinI * std::cos (dOmR), std::cos (dInR) };

      double dNodeRate  = *dLonAscNodeDot * DEG_TO_RAD;
      double dApsisRate = dArgPerihelionDot.value_or (0.0) * DEG_TO_RAD;

      dPrecX = dNodeRate * vLP.x  +  dApsisRate * vON.x;
      dPrecY = dNodeRate * vLP.y  +  dApsisRate * vON.y;
      dPrecZ = dNodeRate * vLP.z  +  dApsisRate * vON.z;
   }
   else
   {
      double dArgPeriDot;
      if (dArgPerihelionDot.has_value ())
         dArgPeriDot = *dArgPerihelionDot;
      else
         dArgPeriDot = dLonPerihelionDot.value_or (0.0) - *dLonAscNodeDot;

      double dOmR  = *dLonAscNode * DEG_TO_RAD;
      double dInR  = *dInclination * DEG_TO_RAD;
      double dSinI = std::sin (dInR);
      VEC3 vON = { dSinI * std::sin (dOmR), -dSinI * std::cos (dOmR), std::cos (dInR) };

      double dNodeRate  = *dLonAscNodeDot * DEG_TO_RAD;
      double dApsisRate = dArgPeriDot * DEG_TO_RAD;

      dPrecX = dApsisRate * vON.x;
      dPrecY = dApsisRate * vON.y;
      dPrecZ = dNodeRate  +  dApsisRate * vON.z;
   }
}

// ---------------------------------------------------------------------------
//  ComputeDerivedRaw - semi-minor axis, period, epoch propagation
// ---------------------------------------------------------------------------

void ORBIT::ComputeDerivedRaw (double dParentGM_m3s2)
{
   if (!dSemiMajorAU.has_value ()  ||  !dEccentricity.has_value ()) return;

   dSemiMinorAU = *dSemiMajorAU * std::sqrt (1.0 - *dEccentricity * *dEccentricity);

   if (dPeriodDays.has_value ())
   {
      dPeriodSeconds = *dPeriodDays * 86400.0;
   }
   else if (dMeanLongitudeDot.has_value ()  &&  dLonPerihelionDot.has_value ())
   {
      double dMdot_degPerCy  = *dMeanLongitudeDot - *dLonPerihelionDot;
      double dMdot_radPerSec = (dMdot_degPerCy * PI / 180.0) / (36525.0 * 86400.0);
      dPeriodSeconds = TWO_PI / dMdot_radPerSec;
   }
   else if (dParentGM_m3s2 > 0.0)
   {
      double dA_m = *dSemiMajorAU * AU_M;
      dPeriodSeconds = TWO_PI * std::sqrt (dA_m * dA_m * dA_m / dParentGM_m3s2);
   }
   else
   {
      double dA_m = *dSemiMajorAU * AU_M;
      dPeriodSeconds = TWO_PI * std::sqrt (dA_m * dA_m * dA_m / GM_SUN_M3S2);
   }

   dPeriodYears = *dPeriodSeconds / JULIAN_YEAR;

   if (dMeanAnomaly.has_value ()  &&  pEpoch != &EPOCH_J2000)
   {
      dMeanAnomaly = pEpoch->PropagateMeanAnomaly (*dMeanAnomaly, *dPeriodYears, EPOCH_J2000);
   }
}

// ---------------------------------------------------------------------------
//  ConvertToOutput - raw values to MSF output units
// ---------------------------------------------------------------------------

void ORBIT::ConvertToOutput ()
{
   if (dSemiMajorAU.has_value ())
   {
      dA = *dSemiMajorAU * AU_M;
   }
   if (dSemiMinorAU.has_value ())
   {
      dB = *dSemiMinorAU * AU_M;
   }
   if (dPeriodSeconds.has_value ())
   {
      tmPeriod = static_cast<int64_t> (std::round (*dPeriodSeconds * TICKS_PER_S));
   }
   if (dMeanAnomaly.has_value ()  &&  tmPeriod != 0)
   {
      tmStart = static_cast<int64_t> (std::round ((*dMeanAnomaly / 360.0) * tmPeriod));
   }

   CELESTIAL::ConvertFrameYUp ();
}

// ---------------------------------------------------------------------------
//  PositionAtTick - where is this body at a given time?
// ---------------------------------------------------------------------------

ORBIT_POSITION* ORBIT::PositionAtTick (int64_t tmNow, ORBIT_POSITION& out) const
{
   ORBIT_POSITION* pResult = nullptr;

   if (dA != 0.0  &&  tmPeriod != 0  &&  bHasQuat)
   {
      double dLocA   = dA;
      double dEcc    = dEccentricity.value_or (0.0);
      double dLocB   = dLocA * std::sqrt (1.0 - dEcc * dEcc);

      int64_t tmInOrbit = ((tmStart + tmNow) % tmPeriod + tmPeriod) % tmPeriod;
      double  dM        = (static_cast<double> (tmInOrbit) / static_cast<double> (tmPeriod)) * TWO_PI;
      double  dE        = ORBIT::SolveKepler (dM, dEcc);

      double dRx = dQx, dRy = dQy, dRz = dQz, dRw = dQw;

      if (tmNow != 0)
      {
         QUAT pPrec     = PrecessionQuat (tmNow);
         QUAT pComposed = CELESTIAL::QuatMultiply (pPrec, { dRx, dRy, dRz, dRw });
         dRx = pComposed.dX;
         dRy = pComposed.dY;
         dRz = pComposed.dZ;
         dRw = pComposed.dW;
      }

      double dLX = dLocA * (std::cos (dE) - dEcc);
      double dLY = dLocB * std::sin (dE);

      VEC3 pPos = CELESTIAL::RotateByQuat (dRx, dRy, dRz, dRw, dLX, 0.0, -dLY);

      out.x  = pPos.x;
      out.y  = pPos.y;
      out.z  = pPos.z;
      out.dE = dE;
      pResult = &out;
   }

   return pResult;
}

// ---------------------------------------------------------------------------
//  PointOnOrbit - compute a 3D point on the orbital ellipse
// ---------------------------------------------------------------------------

CORE::VEC3 ORBIT::PointOnOrbit (double dE, int64_t tmElapsed) const
{
   double dRx = dQx, dRy = dQy, dRz = dQz, dRw = dQw;

   if (tmElapsed != 0)
   {
      QUAT pPrec     = PrecessionQuat (tmElapsed);
      QUAT pComposed = CELESTIAL::QuatMultiply (pPrec, { dRx, dRy, dRz, dRw });
      dRx = pComposed.dX;
      dRy = pComposed.dY;
      dRz = pComposed.dZ;
      dRw = pComposed.dW;
   }

   double dEcc = std::sqrt (1.0 - (dB * dB) / (dA * dA));
   double dLX  = dA * (std::cos (dE) - dEcc);
   double dLY  = dB * std::sin (dE);

   return CELESTIAL::RotateByQuat (dRx, dRy, dRz, dRw, dLX, 0.0, -dLY);
}

}} // namespace SNEEZE::astro
