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

#ifndef SNEEZE_RENDERER_UVSPHERE_H
#define SNEEZE_RENDERER_UVSPHERE_H

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

#endif // SNEEZE_RENDERER_UVSPHERE_H
