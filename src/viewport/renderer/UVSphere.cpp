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

#include "UVSphere.h"
#include <cmath>

namespace renderer {

static constexpr float PI = 3.14159265358979323846f;

void GenerateUVSphere (UV_SPHERE& sphere, float dRadius,
                       int nStacks, int nSlices,
                       float dCenterX, float dCenterY, float dCenterZ)
{
   sphere.aPositions.clear ();
   sphere.aNormals.clear ();
   sphere.aTexCoords.clear ();
   sphere.aIndices.clear ();

   for (int nStack = 0; nStack <= nStacks; nStack++)
   {
      float dPhi = PI * static_cast<float> (nStack) / static_cast<float> (nStacks);
      float dSinPhi = std::sin (dPhi);
      float dCosPhi = std::cos (dPhi);

      for (int nSlice = 0; nSlice <= nSlices; nSlice++)
      {
         float dTheta = 2.0f * PI * static_cast<float> (nSlice) / static_cast<float> (nSlices);
         float dSinTheta = std::sin (dTheta);
         float dCosTheta = std::cos (dTheta);

         float nx = dSinPhi * dCosTheta;
         float ny = dCosPhi;
         float nz = dSinPhi * dSinTheta;

         sphere.aPositions.push_back (dCenterX + dRadius * nx);
         sphere.aPositions.push_back (dCenterY + dRadius * ny);
         sphere.aPositions.push_back (dCenterZ + dRadius * nz);

         sphere.aNormals.push_back (nx);
         sphere.aNormals.push_back (ny);
         sphere.aNormals.push_back (nz);

         float u = static_cast<float> (nSlice) / static_cast<float> (nSlices);
         float v = static_cast<float> (nStack) / static_cast<float> (nStacks);
         sphere.aTexCoords.push_back (u);
         sphere.aTexCoords.push_back (v);
      }
   }

   for (int nStack = 0; nStack < nStacks; nStack++)
   {
      for (int nSlice = 0; nSlice < nSlices; nSlice++)
      {
         uint32_t nCur  = static_cast<uint32_t> (nStack * (nSlices + 1) + nSlice);
         uint32_t nNext = nCur + static_cast<uint32_t> (nSlices + 1);

         sphere.aIndices.push_back (nCur);
         sphere.aIndices.push_back (nCur + 1);
         sphere.aIndices.push_back (nNext);

         sphere.aIndices.push_back (nCur + 1);
         sphere.aIndices.push_back (nNext + 1);
         sphere.aIndices.push_back (nNext);
      }
   }
}

} // namespace renderer
