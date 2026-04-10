// Copyright 2026 Open Metaverse Browser Initiative (OMBI)
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
#include <cstdint>
#include <vector>

namespace rubidium { namespace renderer {

struct SPHERE_DATA
{
   float x, y, z;
   float dRadius;
   float r, g, b;
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

struct CAMERA_DATA
{
   float dPosX, dPosY, dPosZ;
   float dDirX, dDirY, dDirZ;
   float dUpX,  dUpY,  dUpZ;
   float dFovY;
   float dAspect;
};

class RENDERER
{
public:
   virtual ~RENDERER () = default;

   virtual bool Initialize (int nWidth, int nHeight) = 0;
   virtual void Shutdown () = 0;

   virtual void SetCamera (const CAMERA_DATA& pCamera) = 0;
   virtual void BeginFrame () = 0;
   virtual void SubmitSpheres (const std::vector<SPHERE_DATA>& aSpheres) = 0;
   virtual void SubmitCurves (const std::vector<CURVE_DATA>& aCurves) = 0;
   virtual void EndFrame () = 0;

   virtual const uint32_t* GetFrameBuffer () const = 0;
   virtual int GetWidth () const = 0;
   virtual int GetHeight () const = 0;
};

}} // namespace rubidium::renderer
