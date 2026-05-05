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

#include "CameraOrbit.h"
#include <cmath>
#include <algorithm>

static constexpr float MOUSE_SENSITIVITY = 0.005f;
static constexpr float SCROLL_FACTOR     = 1.1f;
static constexpr float MIN_DISTANCE      = 0.1f;
static constexpr float MAX_DISTANCE      = 1e14f;
static constexpr float PI_F              = 3.14159265358979f;

void SNEEZE::VIEWPORT::VIEW::Update (int nDX, int nDY, float dScrollY,
                                     bool bMouseLeft, bool bMouseRight)
{
   if (bMouseLeft)
   {
      dTheta += nDX * MOUSE_SENSITIVITY;
      dPhi   += nDY * MOUSE_SENSITIVITY;
      dPhi = std::max (-PI_F * 0.49f, std::min (PI_F * 0.49f, dPhi));
   }

   if (dScrollY != 0.0f)
   {
      float dFactor = (dScrollY > 0.0f) ? (1.0f / SCROLL_FACTOR) : SCROLL_FACTOR;
      dDistance *= dFactor;
      dDistance = std::max (MIN_DISTANCE, std::min (MAX_DISTANCE, dDistance));
   }
}
