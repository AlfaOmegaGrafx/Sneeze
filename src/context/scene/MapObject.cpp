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
#include <cstring>

using namespace SNEEZE;

// ---------------------------------------------------------------------------
// MAP_OBJECT
// ---------------------------------------------------------------------------

MAP_OBJECT::MAP_OBJECT (MAP_OBJECT_TYPE_TYPE bType)
{
   m_Type.bType = bType;
}

void MAP_OBJECT::Position (int64_t tmNow, double& dX, double& dY, double& dZ) const
{
   (void) tmNow;
   dX = m_Transform.d3Position[0];
   dY = m_Transform.d3Position[1];
   dZ = m_Transform.d3Position[2];
}

void MAP_OBJECT::Rotation (int64_t tmNow, double& dQx, double& dQy, double& dQz, double& dQw) const
{
   (void) tmNow;
   dQx = m_Transform.d4Rotation[0];
   dQy = m_Transform.d4Rotation[1];
   dQz = m_Transform.d4Rotation[2];
   dQw = m_Transform.d4Rotation[3];
}

void MAP_OBJECT::Scale (double& dX, double& dY, double& dZ) const
{
   dX = m_Transform.d3Scale[0];
   dY = m_Transform.d3Scale[1];
   dZ = m_Transform.d3Scale[2];
}

double MAP_OBJECT::Radius () const
{
   return m_Bound.d3Max[0];
}

uint32_t MAP_OBJECT::ColorToU32 () const
{
   uint32_t nColor;
   memcpy (&nColor, &m_Properties.fColor, 4);
   return nColor & 0x00FFFFFF;
}

uint32_t MAP_OBJECT::ColorDimToU32 () const
{
   uint32_t nC = ColorToU32 ();
   int r = (nC >> 16) & 0xFF;
   int g = (nC >>  8) & 0xFF;
   int b =  nC        & 0xFF;
   return static_cast<uint32_t> (((r / 2) << 16) | ((g / 2) << 8) | (b / 2));
}

uint32_t MAP_OBJECT::ColorBrightToU32 () const
{
   uint32_t nC = ColorToU32 ();
   int r = (nC >> 16) & 0xFF;
   int g = (nC >>  8) & 0xFF;
   int b =  nC        & 0xFF;
   auto clamp = [] (int v) { return v > 255 ? 255 : v; };
   return static_cast<uint32_t> ((clamp (r + 64) << 16) | (clamp (g + 64) << 8) | clamp (b + 64));
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
   return m_Orbit.dA != 0.0  &&  m_Orbit.tmPeriod != 0  &&  m_Transform.d4Rotation[3] != 0.0;
}

void MAP_OBJECT_CELESTIAL::Position (int64_t tmNow, double& dX, double& dY, double& dZ) const
{
   ORBIT_POSITION pos;

   if (PositionAtTick (tmNow, pos))
   {
      dX = pos.x;
      dY = pos.y;
      dZ = pos.z;
   }
   else
   {
      dX = m_Transform.d3Position[0];
      dY = m_Transform.d3Position[1];
      dZ = m_Transform.d3Position[2];
   }
}

void MAP_OBJECT_CELESTIAL::Rotation (int64_t tmNow, double& dQx, double& dQy, double& dQz, double& dQw) const
{
   uint8_t bSub = m_Type.bSubtype;

   if (bSub == MAP_OBJECT_TYPE_SUBTYPE_CELESTIAL_STAR        ||
       bSub == MAP_OBJECT_TYPE_SUBTYPE_CELESTIAL_PLANET      ||
       bSub == MAP_OBJECT_TYPE_SUBTYPE_CELESTIAL_MOON        ||
       bSub == MAP_OBJECT_TYPE_SUBTYPE_CELESTIAL_DEBRIS)
   {
      double eX = m_Transform.d4Rotation[0];
      double eY = m_Transform.d4Rotation[1];
      double eZ = m_Transform.d4Rotation[2];
      double eW = m_Transform.d4Rotation[3];

      if (eW == 0.0  &&  eX == 0.0  &&  eY == 0.0  &&  eZ == 0.0)
      {
         dQx = 0.0;  dQy = 0.0;  dQz = 0.0;  dQw = 1.0;
      }
      else
      {
         double dPrecX = m_Transform.d3Position[0];
         double dPrecY = m_Transform.d3Position[1];
         double dPrecZ = m_Transform.d3Position[2];
         double dRate  = std::sqrt (dPrecX * dPrecX + dPrecY * dPrecY + dPrecZ * dPrecZ);

         if (dRate > 1e-30  &&  tmNow != 0)
         {
            double dAngle   = dRate * static_cast<double> (tmNow);
            double dHalf    = dAngle * 0.5;
            double dSinHalf = std::sin (dHalf) / dRate;

            QUAT qPrec     = { dPrecX * dSinHalf, dPrecY * dSinHalf, dPrecZ * dSinHalf, std::cos (dHalf) };
            QUAT qComposed = CELESTIAL::QuatMultiply (qPrec, { eX, eY, eZ, eW });

            dQx = qComposed.dX;
            dQy = qComposed.dY;
            dQz = qComposed.dZ;
            dQw = qComposed.dW;
         }
         else
         {
            dQx = eX;  dQy = eY;  dQz = eZ;  dQw = eW;
         }
      }
   }
   else if (bSub == MAP_OBJECT_TYPE_SUBTYPE_CELESTIAL_SURFACE)
   {
      int64_t tmSpinPeriod = m_Orbit.tmPeriod;

      if (tmSpinPeriod != 0)
      {
         double dW0Rad = m_Orbit.dA;
         double dAngle = dW0Rad + (static_cast<double> (tmNow) / static_cast<double> (tmSpinPeriod)) * TWO_PI;
         double dHalf  = dAngle * 0.5;

         dQx = 0.0;
         dQy = std::sin (dHalf);
         dQz = 0.0;
         dQw = std::cos (dHalf);
      }
      else
      {
         dQx = 0.0;  dQy = 0.0;  dQz = 0.0;  dQw = 1.0;
      }
   }
   else
   {
      dQx = 0.0;  dQy = 0.0;  dQz = 0.0;  dQw = 1.0;
   }
}

ORBIT_POSITION* MAP_OBJECT_CELESTIAL::PositionAtTick (int64_t tmNow, ORBIT_POSITION& out) const
{
   ORBIT_POSITION* pResult = nullptr;

   if (m_Orbit.dA != 0.0  &&  m_Orbit.tmPeriod != 0  &&  m_Transform.d4Rotation[3] != 0.0)
   {
      double dA   = m_Orbit.dA;
      double dB   = m_Orbit.dB;
      double dEcc = std::sqrt (1.0 - (dB * dB) / (dA * dA));

      int64_t tmInOrbit = ((m_Orbit.tmOrigin + tmNow) % m_Orbit.tmPeriod + m_Orbit.tmPeriod) % m_Orbit.tmPeriod;
      double  dM        = (static_cast<double> (tmInOrbit) / static_cast<double> (m_Orbit.tmPeriod)) * TWO_PI;
      double  dE        = ORBIT::SolveKepler (dM, dEcc);

      double dRx = m_Transform.d4Rotation[0];
      double dRy = m_Transform.d4Rotation[1];
      double dRz = m_Transform.d4Rotation[2];
      double dRw = m_Transform.d4Rotation[3];

      if (tmNow != 0)
      {
         double dPrecX = m_Transform.d3Position[0];
         double dPrecY = m_Transform.d3Position[1];
         double dPrecZ = m_Transform.d3Position[2];
         double dRate = std::sqrt (dPrecX * dPrecX + dPrecY * dPrecY + dPrecZ * dPrecZ);

         if (dRate > 1e-30)
         {
            double dAngle   = dRate * static_cast<double> (tmNow);
            double dHalf    = dAngle * 0.5;
            double dSinHalf = std::sin (dHalf) / dRate;

            QUAT pPrec = { dPrecX * dSinHalf, dPrecY * dSinHalf, dPrecZ * dSinHalf, std::cos (dHalf) };
            QUAT pComposed = CELESTIAL::QuatMultiply (pPrec, { dRx, dRy, dRz, dRw });
            dRx = pComposed.dX;
            dRy = pComposed.dY;
            dRz = pComposed.dZ;
            dRw = pComposed.dW;
         }
      }

      double dLX = dA * (std::cos (dE) - dEcc);
      double dLY = dB * std::sin (dE);

      VEC3 pPos = CELESTIAL::RotateByQuat (dRx, dRy, dRz, dRw, dLX, 0.0, -dLY);

      out.x  = pPos.x;
      out.y  = pPos.y;
      out.z  = pPos.z;
      out.dE = dE;
      pResult = &out;
   }

   return pResult;
}

VEC3 MAP_OBJECT_CELESTIAL::OrbitTrailPoint (double dE, int64_t tmElapsed) const
{
   double dRx = m_Transform.d4Rotation[0];
   double dRy = m_Transform.d4Rotation[1];
   double dRz = m_Transform.d4Rotation[2];
   double dRw = m_Transform.d4Rotation[3];

   if (tmElapsed != 0)
   {
      double dPrecX = m_Transform.d3Position[0];
      double dPrecY = m_Transform.d3Position[1];
      double dPrecZ = m_Transform.d3Position[2];
      double dRate = std::sqrt (dPrecX * dPrecX + dPrecY * dPrecY + dPrecZ * dPrecZ);

      if (dRate > 1e-30)
      {
         double dAngle   = dRate * static_cast<double> (tmElapsed);
         double dHalf    = dAngle * 0.5;
         double dSinHalf = std::sin (dHalf) / dRate;

         QUAT pPrec = { dPrecX * dSinHalf, dPrecY * dSinHalf, dPrecZ * dSinHalf, std::cos (dHalf) };
         QUAT pComposed = CELESTIAL::QuatMultiply (pPrec, { dRx, dRy, dRz, dRw });
         dRx = pComposed.dX;
         dRy = pComposed.dY;
         dRz = pComposed.dZ;
         dRw = pComposed.dW;
      }
   }

   double dA   = m_Orbit.dA;
   double dB   = m_Orbit.dB;
   double dEcc = std::sqrt (1.0 - (dB * dB) / (dA * dA));
   double dLX  = dA * (std::cos (dE) - dEcc);
   double dLY  = dB * std::sin (dE);

   return CELESTIAL::RotateByQuat (dRx, dRy, dRz, dRw, dLX, 0.0, -dLY);
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
