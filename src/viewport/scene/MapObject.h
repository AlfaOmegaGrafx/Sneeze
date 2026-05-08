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

#include <atomic>
#include <cstdint>
#include <mutex>

namespace SNEEZE
{
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

      // Texture
      std::string              m_sTextureUrl;
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

      double m_dRadius = 0.0;
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
