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

#ifndef SNEEZE_VIEWPORT_PRIVATE_H
#define SNEEZE_VIEWPORT_PRIVATE_H

#include "Types.h"

struct UV_SPHERE
{
   std::vector<float>    aPositions;
   std::vector<float>    aNormals;
   std::vector<float>    aTexCoords;
   std::vector<uint32_t> aIndices;
};

void GenerateUVSphere (UV_SPHERE& sphere, float dRadius,
                       int nStacks, int nSlices,
                       float dCenterX, float dCenterY, float dCenterZ);

// Centered unit cube spanning [-0.5, 0.5] on each axis, with per-face normals.
// Callers supply a world transform that maps this canonical cube to the target
// oriented box (dimensions and pivot are baked into that transform).
void GenerateUnitBox (UV_SPHERE& box);

namespace SNEEZE
{
   struct SPHERE_DATA
   {
      float x, y, z;
      float dRadius;
      float r, g, b;

      const uint8_t* pTexturePixels  = nullptr;
      int            nTextureWidth   = 0;
      int            nTextureHeight  = 0;
      bool           bEmissive       = false;
   };

   struct CURVE_POINT
   {
      float x, y, z;
      float dRadius;
   };

   struct CURVE_DATA
   {
      std::vector<CURVE_POINT> aPoints;
      float r, g, b;
   };

   struct BOX_DATA
   {
      float m16[16];               // column-major world transform (render space)
      float r, g, b;
   };

   struct CAMERA_DATA
   {
      float dPosX, dPosY, dPosZ;
      float dDirX, dDirY, dDirZ;
      float dUpX,  dUpY,  dUpZ;
      float dFovY;
      float dAspect;
      float dNear;
      float dFar;
   };

   struct LIGHT_DATA
   {
      float x = 0.0f;                 // world position of a star-driven point light
      float y = 0.0f;
      float z = 0.0f;
   };

   class VIEWPORT::RENDERER
   {
   public:
      class ANARI;

      virtual ~RENDERER () = default;

      virtual void SetNativeWindow (void* pHandle) { (void) pHandle; }
      virtual bool IsRenderingToNativeSurface () const { return false; }

      virtual bool Initialize (int nWidth, int nHeight) = 0;
      virtual void Resize (int nWidth, int nHeight) = 0;

      virtual void SetCamera (const CAMERA_DATA& pCamera) = 0;
      virtual void SetLights (const std::vector<LIGHT_DATA>& aLight) { (void) aLight; }
      virtual void BeginFrame () = 0;
      virtual void SubmitSpheres (const std::vector<SPHERE_DATA>& aSpheres) = 0;
      virtual void SubmitCurves (const std::vector<CURVE_DATA>& aCurves) = 0;
      virtual void SubmitBoxes (const std::vector<BOX_DATA>& aBoxes) { (void) aBoxes; }
      virtual void EndFrame () = 0;

      // Forces a full scene rebuild on the next frame (e.g. after a scene swap).
      virtual void InvalidateScene () {}

      virtual const uint32_t* GetFrameBuffer () const = 0;
      virtual int GetWidth () const = 0;
      virtual int GetHeight () const = 0;

      virtual double GetLastSubmitSeconds () const { return 0.0; }
      virtual double GetLastRenderSeconds () const { return 0.0; }
   };
}

#endif // SNEEZE_VIEWPORT_PRIVATE_H
