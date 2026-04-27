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

#include "Celestial.h"
#include <cmath>
#include <algorithm>

namespace sneeze { namespace astro {

using core::DEG_TO_RAD;
using core::OBLIQUITY_J2000;
using core::TICKS_PER_CY;

// ---------------------------------------------------------------------------

CELESTIAL::CELESTIAL ()
   : dQx (0.0), dQy (0.0), dQz (0.0), dQw (1.0)
   , bHasQuat (false)
   , dPrecX (0.0), dPrecY (0.0), dPrecZ (0.0)
{
}

// ---------------------------------------------------------------------------
//  Static - quaternion / math helpers
// ---------------------------------------------------------------------------

QUAT CELESTIAL::QuatMultiply (const QUAT& q1, const QUAT& q2)
{
   QUAT r;
   r.dX = q1.dW*q2.dX + q1.dX*q2.dW + q1.dY*q2.dZ - q1.dZ*q2.dY;
   r.dY = q1.dW*q2.dY - q1.dX*q2.dZ + q1.dY*q2.dW + q1.dZ*q2.dX;
   r.dZ = q1.dW*q2.dZ + q1.dX*q2.dY - q1.dY*q2.dX + q1.dZ*q2.dW;
   r.dW = q1.dW*q2.dW - q1.dX*q2.dX - q1.dY*q2.dY - q1.dZ*q2.dZ;
   return r;
}

QUAT CELESTIAL::QuatRotZ (double dDeg)
{
   double dRad = dDeg * DEG_TO_RAD * 0.5;
   return { 0.0, 0.0, std::sin (dRad), std::cos (dRad) };
}

QUAT CELESTIAL::QuatRotX (double dDeg)
{
   double dRad = dDeg * DEG_TO_RAD * 0.5;
   return { std::sin (dRad), 0.0, 0.0, std::cos (dRad) };
}

QUAT CELESTIAL::QuatConjugate (const QUAT& q)
{
   return { -q.dX, -q.dY, -q.dZ, q.dW };
}

VEC3 CELESTIAL::RotateByQuat (double qx, double qy, double qz, double qw,
                               double vx, double vy, double vz)
{
   double cx1 = qy * vz - qz * vy;
   double cy1 = qz * vx - qx * vz;
   double cz1 = qx * vy - qy * vx;
   double cx2 = qy * cz1 - qz * cy1;
   double cy2 = qz * cx1 - qx * cz1;
   double cz2 = qx * cy1 - qy * cx1;

   return {
      vx + 2.0 * (qw * cx1 + cx2),
      vy + 2.0 * (qw * cy1 + cy2),
      vz + 2.0 * (qw * cz1 + cz2),
   };
}

// ---------------------------------------------------------------------------
//  BuildOrbitalQuat - Rz(Omega) x Rx(i) x Rz(omega)
// ---------------------------------------------------------------------------

QUAT CELESTIAL::BuildOrbitalQuat (double dLonAscNode, double dInclination, double dArgPerihelion)
{
   QUAT qNode = QuatRotZ (dLonAscNode);
   QUAT qTilt = QuatRotX (dInclination);
   QUAT qPeri = QuatRotZ (dArgPerihelion);
   QUAT qTemp = QuatMultiply (qNode, qTilt);

   return QuatMultiply (qTemp, qPeri);
}

// ---------------------------------------------------------------------------
//  FrameToEclipticQuat - rotate non-ecliptic reference frame into
//  J2000 ecliptic coordinates.
// ---------------------------------------------------------------------------

QUAT CELESTIAL::FrameToEclipticQuat (double dRA, double dDec)
{
   double dAlpha = dRA  * DEG_TO_RAD;
   double dDelta = dDec * DEG_TO_RAD;
   double dEps   = OBLIQUITY_J2000 * DEG_TO_RAD;

   double dCosA = std::cos (dAlpha);
   double dSinA = std::sin (dAlpha);
   double dCosD = std::cos (dDelta);
   double dSinD = std::sin (dDelta);
   double dCosE = std::cos (dEps);
   double dSinE = std::sin (dEps);

   double m00 =  dSinA;
   double m10 = -dCosA * dCosE;
   double m20 =  dCosA * dSinE;

   double m01 =  dSinD * dCosA;
   double m11 =  dSinD * dSinA * dCosE - dCosD * dSinE;
   double m21 = -dSinD * dSinA * dSinE - dCosD * dCosE;

   double m02 =  dCosD * dCosA;
   double m12 =  dCosD * dSinA * dCosE + dSinD * dSinE;
   double m22 = -dCosD * dSinA * dSinE + dSinD * dCosE;

   return MatrixToQuat (m00, m01, m02, m10, m11, m12, m20, m21, m22);
}

// ---------------------------------------------------------------------------
//  EquatorialToEclipticVec - ICRF equatorial (RA, Dec) -> Z-up ecliptic
// ---------------------------------------------------------------------------

VEC3 CELESTIAL::EquatorialToEclipticVec (double dRA, double dDec)
{
   double dA = dRA  * DEG_TO_RAD;
   double dD = dDec * DEG_TO_RAD;
   double dE = OBLIQUITY_J2000 * DEG_TO_RAD;

   double x = std::cos (dD) * std::cos (dA);
   double y = std::cos (dD) * std::sin (dA);
   double z = std::sin (dD);

   return {
       x,
       y * std::cos (dE)  +  z * std::sin (dE),
      -y * std::sin (dE)  +  z * std::cos (dE),
   };
}

// ---------------------------------------------------------------------------
//  MatrixToQuat - Shepperd's method
// ---------------------------------------------------------------------------

QUAT CELESTIAL::MatrixToQuat (double m00, double m01, double m02,
                               double m10, double m11, double m12,
                               double m20, double m21, double m22)
{
   double dTrace = m00 + m11 + m22;
   double dX, dY, dZ, dW;

   if (dTrace > 0.0)
   {
      double dS = 2.0 * std::sqrt (dTrace + 1.0);
      dW = 0.25 * dS;
      dX = (m21 - m12) / dS;
      dY = (m02 - m20) / dS;
      dZ = (m10 - m01) / dS;
   }
   else if (m00 > m11  &&  m00 > m22)
   {
      double dS = 2.0 * std::sqrt (1.0 + m00 - m11 - m22);
      dW = (m21 - m12) / dS;
      dX = 0.25 * dS;
      dY = (m01 + m10) / dS;
      dZ = (m02 + m20) / dS;
   }
   else if (m11 > m22)
   {
      double dS = 2.0 * std::sqrt (1.0 + m11 - m00 - m22);
      dW = (m02 - m20) / dS;
      dX = (m01 + m10) / dS;
      dY = 0.25 * dS;
      dZ = (m12 + m21) / dS;
   }
   else
   {
      double dS = 2.0 * std::sqrt (1.0 + m22 - m00 - m11);
      dW = (m10 - m01) / dS;
      dX = (m02 + m20) / dS;
      dY = (m12 + m21) / dS;
      dZ = 0.25 * dS;
   }

   return { dX, dY, dZ, dW };
}

// ---------------------------------------------------------------------------
//  PrecessionQuat - build rotation quaternion for elapsed time
// ---------------------------------------------------------------------------

QUAT CELESTIAL::PrecessionQuat (int64_t tmElapsed) const
{
   QUAT pResult = { 0.0, 0.0, 0.0, 1.0 };

   double dRate = std::sqrt (dPrecX * dPrecX + dPrecY * dPrecY + dPrecZ * dPrecZ);

   if (dRate > 1e-30)
   {
      double dAngle   = dRate * static_cast<double> (tmElapsed);
      double dHalf    = dAngle * 0.5;
      double dSinHalf = std::sin (dHalf) / dRate;

      pResult = {
         dPrecX * dSinHalf,
         dPrecY * dSinHalf,
         dPrecZ * dSinHalf,
         std::cos (dHalf),
      };
   }

   return pResult;
}

// ---------------------------------------------------------------------------
//  ConvertFrameYUp - Z-up -> Y-up swap + rad/century -> rad/tick conversion
// ---------------------------------------------------------------------------

void CELESTIAL::ConvertFrameYUp ()
{
   if (bHasQuat)
   {
      double dTempQy = dQy;
      dQy            = dQz;
      dQz            = -dTempQy;
   }

   double dTempPy = dPrecY;
   dPrecY         = dPrecZ;
   dPrecZ         = -dTempPy;

   dPrecX /= static_cast<double> (TICKS_PER_CY);
   dPrecY /= static_cast<double> (TICKS_PER_CY);
   dPrecZ /= static_cast<double> (TICKS_PER_CY);
}

}} // namespace sneeze::astro
