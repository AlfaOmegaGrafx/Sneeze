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

namespace view {

static constexpr float MOUSE_SENSITIVITY = 0.005f;
static constexpr float SCROLL_FACTOR     = 1.1f;
static constexpr float MIN_DISTANCE      = 0.1f;
static constexpr float MAX_DISTANCE      = 1e14f;
static constexpr float PI_F              = 3.14159265358979f;

void UpdateCameraOrbit (CAMERA_ORBIT& pOrbit, int nDX, int nDY, float dScrollY,
                        bool bMouseLeft, bool bMouseRight)
{
   if (bMouseLeft)
   {
      pOrbit.dTheta += nDX * MOUSE_SENSITIVITY;
      pOrbit.dPhi   += nDY * MOUSE_SENSITIVITY;
      pOrbit.dPhi = std::max (-PI_F * 0.49f, std::min (PI_F * 0.49f, pOrbit.dPhi));
   }

   if (dScrollY != 0.0f)
   {
      float dFactor = (dScrollY > 0.0f) ? (1.0f / SCROLL_FACTOR) : SCROLL_FACTOR;
      pOrbit.dDistance *= dFactor;
      pOrbit.dDistance = std::max (MIN_DISTANCE, std::min (MAX_DISTANCE, pOrbit.dDistance));
   }
}

} // namespace view
