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

#include "sneeze/Types.h"

namespace SNEEZE
{
   struct ORBIT_POSITION
   {
      double x;
      double y;
      double z;
      double dE;
   };

   // ---------------------------------------------------------------------------
   // Map object type identifiers
   // ---------------------------------------------------------------------------

   enum MAP_OBJECT_TYPE_TYPE : uint8_t
   {
      MAP_OBJECT_TYPE_TYPE_ROOT        = 0,
      MAP_OBJECT_TYPE_TYPE_CELESTIAL   = 1,
      MAP_OBJECT_TYPE_TYPE_TERRESTRIAL = 2,
      MAP_OBJECT_TYPE_TYPE_PHYSICAL    = 3,
   };

   // ---------------------------------------------------------------------------
   // Celestial body type identifiers
   // ---------------------------------------------------------------------------

   enum MAP_OBJECT_TYPE_SUBTYPE_CELESTIAL
   {
      MAP_OBJECT_TYPE_SUBTYPE_CELESTIAL_NONE           = 0,
      MAP_OBJECT_TYPE_SUBTYPE_CELESTIAL_UNIVERSE       = 1,
      MAP_OBJECT_TYPE_SUBTYPE_CELESTIAL_SUPERCLUSTER   = 2,
      MAP_OBJECT_TYPE_SUBTYPE_CELESTIAL_GALAXYCLUSTER  = 3,
      MAP_OBJECT_TYPE_SUBTYPE_CELESTIAL_GALAXY         = 4,
      MAP_OBJECT_TYPE_SUBTYPE_CELESTIAL_SECTOR         = 5,
      MAP_OBJECT_TYPE_SUBTYPE_CELESTIAL_NEBULA         = 6,
      MAP_OBJECT_TYPE_SUBTYPE_CELESTIAL_STARCLUSTER    = 7,
      MAP_OBJECT_TYPE_SUBTYPE_CELESTIAL_BLACKHOLE      = 8,
      MAP_OBJECT_TYPE_SUBTYPE_CELESTIAL_STARSYSTEM     = 9,
      MAP_OBJECT_TYPE_SUBTYPE_CELESTIAL_STAR           = 10,
      MAP_OBJECT_TYPE_SUBTYPE_CELESTIAL_PLANETSYSTEM   = 11,
      MAP_OBJECT_TYPE_SUBTYPE_CELESTIAL_PLANET         = 12,
      MAP_OBJECT_TYPE_SUBTYPE_CELESTIAL_MOONSYSTEM     = 125,
      MAP_OBJECT_TYPE_SUBTYPE_CELESTIAL_MOON           = 13,
      MAP_OBJECT_TYPE_SUBTYPE_CELESTIAL_DEBRISSYSTEM   = 135,
      MAP_OBJECT_TYPE_SUBTYPE_CELESTIAL_DEBRIS         = 14,
      MAP_OBJECT_TYPE_SUBTYPE_CELESTIAL_SATELLITE      = 15,
      MAP_OBJECT_TYPE_SUBTYPE_CELESTIAL_TRANSPORT      = 16,
      MAP_OBJECT_TYPE_SUBTYPE_CELESTIAL_SURFACE        = 17,
   };

   // ---------------------------------------------------------------------------
   // RMAP wire-format structures (RMCOBJECT, 432 bytes)
   //
   // These structs mirror the RMAP binary layout byte-for-byte. They are the
   // contract between WASM modules (which write them into linear memory) and
   // the Scene host functions (which read them out). Both sides must agree on
   // layout — C++ uses #pragma pack(push, 1), Rust uses #[repr(C, packed)].
   // ---------------------------------------------------------------------------

   struct OBJECTIX
   {
      uint64_t              qwComposed;

      uint64_t              ObjectIx () const   { return qwComposed & 0x0000FFFFFFFFFFFFull; }
      uint16_t              Class    () const   { return static_cast<uint16_t> (qwComposed >> 48); }
   };

   struct OBJECT_HEAD
   {
      OBJECTIX              Parent;
      OBJECTIX              Self;
      uint64_t              qwEvent;
   };

   struct MAP_OBJECT_NAME
   {
      uint16_t              wsName[48];
   };

   struct MAP_OBJECT_TYPE
   {
      MAP_OBJECT_TYPE_TYPE  bType;
      uint8_t               bSubtype;
      uint8_t               bFiction;
      uint8_t               abReserved[5];
   };

   struct MAP_OBJECT_OWNER
   {
      uint64_t              twOwner;
   };

   struct MAP_OBJECT_RESOURCE
   {
      uint64_t              qwResource;
      char                  sName[32];
      char                  sReference[64];
   };

   struct MAP_OBJECT_TRANSFORM
   {
      double                d3Position[3];
      double                d4Rotation[4];
      double                d3Scale[3];
   };

   struct MAP_OBJECT_ORBIT
   {
      int64_t               tmPeriod;
      int64_t               tmOrigin;
      double                dA;
      double                dB;
   };

   struct MAP_OBJECT_BOUND
   {
      uint8_t               abReserved[24];
      double                d3Max[3];
   };

   struct MAP_OBJECT_PROPERTIES
   {
      float                 fMass;
      float                 fGravity;
      float                 fColor;
      float                 fBrightness;
      float                 fReflectivity;
      uint8_t               abReserved[12];
   };

   struct RMCOBJECT
   {
      OBJECT_HEAD           Head;
      MAP_OBJECT_NAME       Name;
      MAP_OBJECT_TYPE       Type;
      MAP_OBJECT_OWNER      Owner;
      MAP_OBJECT_RESOURCE   Resource;
      MAP_OBJECT_TRANSFORM  Transform;
      MAP_OBJECT_ORBIT      Orbit;
      MAP_OBJECT_BOUND      Bound;
      MAP_OBJECT_PROPERTIES Properties;
   };

   static_assert (sizeof (RMCOBJECT) == 432, "RMCOBJECT must be exactly 432 bytes");

   // ---------------------------------------------------------------------------
   // MAP_OBJECT — base class for all 3D objects referenced by SOM::NODEs.
   // All spatial properties (position, orientation, scale, bounding volume,
   // visual appearance) belong here, not on the NODE itself.
   // ---------------------------------------------------------------------------

   class MAP_OBJECT
   {
   public:
      explicit MAP_OBJECT (MAP_OBJECT_TYPE_TYPE bType);
      virtual ~MAP_OBJECT () = default;

      MAP_OBJECT_NAME               m_Name             = {};
      MAP_OBJECT_TYPE               m_Type             = {};
      MAP_OBJECT_OWNER              m_Owner            = {};
      MAP_OBJECT_RESOURCE           m_Resource         = {};
      MAP_OBJECT_TRANSFORM          m_Transform        = {};
      MAP_OBJECT_ORBIT              m_Orbit            = {};
      MAP_OBJECT_BOUND              m_Bound            = {};
      MAP_OBJECT_PROPERTIES         m_Properties       = {};

      // Texture
      std::vector<uint8_t>          m_aTexturePixels;
      int                           m_nTextureWidth    = 0;
      int                           m_nTextureHeight   = 0;
      int                           m_nTextureChannels = 0;
      std::atomic<bool>             m_bTextureReady      {false};
      mutable std::mutex            m_textureMutex;

      MAP_OBJECT_TYPE_TYPE GetType       () const { return m_Type.bType; }

      virtual void Position (int64_t tmNow, double& dX, double& dY, double& dZ) const;
      virtual void Rotation (int64_t tmNow, double& dQx, double& dQy, double& dQz, double& dQw) const;
      void         Scale    (double& dX, double& dY, double& dZ) const;
      double       Radius   () const;
      uint32_t     ColorToU32      () const;
      uint32_t     ColorDimToU32   () const;
      uint32_t     ColorBrightToU32 () const;

      void                 LockTexture   () const {        m_textureMutex.lock   (); }
      void                 UnlockTexture () const {        m_textureMutex.unlock (); }

   };

   // ---------------------------------------------------------------------------
   // Derived map object types
   // ---------------------------------------------------------------------------

   class MAP_OBJECT_ROOT : public MAP_OBJECT
   {
   public:
      MAP_OBJECT_ROOT ();
   };

   class MAP_OBJECT_CELESTIAL : public MAP_OBJECT
   {
   public:
      MAP_OBJECT_CELESTIAL ();

      bool HasOrbit () const;

      void Position (int64_t tmNow, double& dX, double& dY, double& dZ) const override;
      void Rotation (int64_t tmNow, double& dQx, double& dQy, double& dQz, double& dQw) const override;

      ORBIT_POSITION* PositionAtTick (int64_t tmNow, ORBIT_POSITION& out) const;
      VEC3            OrbitTrailPoint (double dE, int64_t tmElapsed) const;
   };

   class MAP_OBJECT_TERRESTRIAL : public MAP_OBJECT
   {
   public:
      MAP_OBJECT_TERRESTRIAL ();
   };

   class MAP_OBJECT_PHYSICAL : public MAP_OBJECT
   {
   public:
      MAP_OBJECT_PHYSICAL ();
   };
}
#endif // SNEEZE_SOM_MAPOBJECT_H
