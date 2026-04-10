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

#pragma once

#include "core/Types.h"

namespace sneeze { namespace astro {

using core::QUAT;
using core::VEC3;

class CELESTIAL
{
public:
   CELESTIAL ();

   // Orientation quaternion (reference frame -> local frame at epoch)
   double dQx;
   double dQy;
   double dQz;
   double dQw;
   bool   bHasQuat;

   // Precession angular velocity vector (rad/tick after ConvertFrameYUp)
   double dPrecX;
   double dPrecY;
   double dPrecZ;

   // --- Static quaternion / math helpers ---

   static QUAT QuatMultiply (const QUAT& q1, const QUAT& q2);
   static QUAT QuatRotZ (double dDeg);
   static QUAT QuatRotX (double dDeg);
   static QUAT QuatConjugate (const QUAT& q);
   static VEC3 RotateByQuat (double qx, double qy, double qz, double qw,
                              double vx, double vy, double vz);

   static QUAT BuildOrbitalQuat (double dLonAscNode, double dInclination, double dArgPerihelion);
   static QUAT FrameToEclipticQuat (double dRA, double dDec);
   static VEC3 EquatorialToEclipticVec (double dRA, double dDec);
   static QUAT MatrixToQuat (double m00, double m01, double m02,
                              double m10, double m11, double m12,
                              double m20, double m21, double m22);

   QUAT PrecessionQuat (int64_t tmElapsed) const;
   void ConvertFrameYUp ();
};

}} // namespace sneeze::astro
