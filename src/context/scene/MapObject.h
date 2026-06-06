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

#ifndef SNEEZE_SOM_MAPOBJECT_H
#define SNEEZE_SOM_MAPOBJECT_H

#include "Orbit.h"

namespace SNEEZE
{
   // ---------------------------------------------------------------------------
   // RMAP wire-format structures (RMCOBJECT, 432 bytes)
   //
   // These structs mirror the RMAP binary layout byte-for-byte. They are the
   // contract between WASM modules (which write them into linear memory) and
   // the Scene host functions (which read them out). Both sides must agree on
   // layout — C++ uses #pragma pack(push, 1), Rust uses #[repr(C, packed)].
   // ---------------------------------------------------------------------------

#pragma pack(push, 1)

   struct OBJECTIX
   {
      uint64_t qwComposed;

      uint64_t ObjectIx () const   { return qwComposed & 0x0000FFFFFFFFFFFFull; }
      uint16_t Class    () const   { return static_cast<uint16_t> (qwComposed >> 48); }
   };

   struct OBJECT_HEAD
   {
      OBJECTIX     Parent;
      OBJECTIX     Self;
      uint64_t     qwEvent;
   };

   struct RMCOBJECT_NAME
   {
      uint16_t wsName[48];
   };

   struct RMCOBJECT_TYPE
   {
      uint8_t  bType;
      uint8_t  bSubtype;
      uint8_t  bFiction;
      uint8_t  abReserved[5];
   };

   struct RMCOBJECT_OWNER
   {
      uint64_t twOwner;
   };

   struct RMCOBJECT_RESOURCE
   {
      uint64_t qwResource;
      char     sName[32];
      char     sReference[64];
   };

   struct RMCOBJECT_TRANSFORM
   {
      double d3Position[3];
      double d4Rotation[4];
      double d3Scale[3];
   };

   struct RMCOBJECT_ORBIT
   {
      int64_t  tmPeriod;
      int64_t  tmOrigin;
      double   dA;
      double   dB;
   };

   struct RMCOBJECT_BOUND
   {
      uint8_t  abReserved[24];
      double   d3Max[3];
   };

   struct RMCOBJECT_PROPERTIES
   {
      float    fMass;
      float    fGravity;
      float    fColor;
      float    fBrightness;
      float    fReflectivity;
      uint8_t  abReserved[12];
   };

   struct RMCOBJECT
   {
      OBJECT_HEAD              Head;
      RMCOBJECT_NAME           Name;
      RMCOBJECT_TYPE           Type;
      RMCOBJECT_OWNER          Owner;
      RMCOBJECT_RESOURCE       Resource;
      RMCOBJECT_TRANSFORM      Transform;
      RMCOBJECT_ORBIT          Orbit;
      RMCOBJECT_BOUND          Bound;
      RMCOBJECT_PROPERTIES     Properties;
   };

#pragma pack(pop)

   static_assert (sizeof (RMCOBJECT) == 432, "RMCOBJECT must be exactly 432 bytes");

   // ---------------------------------------------------------------------------
   // Map object type identifiers
   // ---------------------------------------------------------------------------

   enum MAP_OBJECT_TYPE
   {
      MAP_OBJECT_TYPE_ROOT        = 0,
      MAP_OBJECT_TYPE_CELESTIAL   = 1,
      MAP_OBJECT_TYPE_TERRESTRIAL = 2,
      MAP_OBJECT_TYPE_PHYSICAL    = 3,
   };

   // ---------------------------------------------------------------------------
   // Celestial body type identifiers
   // ---------------------------------------------------------------------------

   enum CELESTIAL_TYPE
   {
      CELESTIAL_TYPE_NONE           = 0,
      CELESTIAL_TYPE_UNIVERSE       = 1,
      CELESTIAL_TYPE_STARSYSTEM     = 9,
      CELESTIAL_TYPE_STAR           = 10,
      CELESTIAL_TYPE_PLANETSYSTEM   = 11,
      CELESTIAL_TYPE_PLANET         = 12,
      CELESTIAL_TYPE_MOONSYSTEM     = 125,
      CELESTIAL_TYPE_MOON           = 13,
      CELESTIAL_TYPE_DEBRISSYSTEM   = 135,
      CELESTIAL_TYPE_DEBRIS         = 14,
      CELESTIAL_TYPE_SATELLITE      = 15,
      CELESTIAL_TYPE_SURFACE        = 17,
   };

   // ---------------------------------------------------------------------------
   // MAP_OBJECT — base class for all 3D objects referenced by SOM::NODEs.
   // All spatial properties (position, orientation, scale, bounding volume,
   // visual appearance) belong here, not on the NODE itself.
   // ---------------------------------------------------------------------------

   class MAP_OBJECT
   {
   public:
      explicit MAP_OBJECT (MAP_OBJECT_TYPE bType);
      virtual ~MAP_OBJECT () = default;

      MAP_OBJECT_TYPE GetType () const { return m_bType; }

      // Position in parent-relative coordinates (meters)
      double m_dPosX   = 0.0;
      double m_dPosY   = 0.0;
      double m_dPosZ   = 0.0;

      // Scale (uniform for now)
      double m_dScale  = 1.0;

      // Bounding sphere radius (meters)
      double m_dBound  = 0.0;

      // Visual color (0xRRGGBB)
      uint32_t m_nColor = 0xcccccc;

      // Fabric
      std::string              m_sUrl_Fabric;

      // Texture
      std::string              m_sUrl_Texture;
      std::vector<uint8_t>     m_aTexturePixels;
      int                      m_nTextureWidth  = 0;
      int                      m_nTextureHeight = 0;
      int                      m_nTextureChannels = 0;
      std::atomic<bool>        m_bTextureReady {false};
      mutable std::mutex       m_textureMutex;

      void LockTexture () const   { m_textureMutex.lock (); }
      void UnlockTexture () const { m_textureMutex.unlock (); }

   private:
      MAP_OBJECT_TYPE m_bType;
   };

   // ---------------------------------------------------------------------------
   // Derived map object types
   // ---------------------------------------------------------------------------

   class MAP_OBJECT_ROOT : public MAP_OBJECT
   {
   public:
      MAP_OBJECT_ROOT () : MAP_OBJECT (MAP_OBJECT_TYPE_ROOT) {}
   };

   class MAP_OBJECT_CELESTIAL : public MAP_OBJECT
   {
   public:
      MAP_OBJECT_CELESTIAL () : MAP_OBJECT (MAP_OBJECT_TYPE_CELESTIAL) {}

      bool HasOrbit () const { return m_orbit.dA != 0.0  &&  m_orbit.tmPeriod != 0  &&  m_orbit.bHasQuat; }

      // Identity
      std::string    m_sName;
      CELESTIAL_TYPE m_bCelestialType = CELESTIAL_TYPE_NONE;

      // Physical
      double                m_dRadius        = 0.0;
      std::optional<double> m_dMass;
      std::optional<double> m_dGM;
      std::optional<double> m_dSystemRadiusKm;
      std::optional<double> m_dPoleRA;
      std::optional<double> m_dPoleDec;
      std::optional<double> m_dObliquity;

      // Color variants (base m_nColor on MAP_OBJECT serves as normal)
      uint32_t m_nColorDim    = 0x666666;
      uint32_t m_nColorBright = 0xffffff;

      // Orbital mechanics
      ORBIT m_orbit;
   };

   class MAP_OBJECT_TERRESTRIAL : public MAP_OBJECT
   {
   public:
      MAP_OBJECT_TERRESTRIAL () : MAP_OBJECT (MAP_OBJECT_TYPE_TERRESTRIAL) {}
   };

   class MAP_OBJECT_PHYSICAL : public MAP_OBJECT
   {
   public:
      MAP_OBJECT_PHYSICAL () : MAP_OBJECT (MAP_OBJECT_TYPE_PHYSICAL) {}
   };
}
#endif // SNEEZE_SOM_MAPOBJECT_H
