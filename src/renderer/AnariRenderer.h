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

#pragma once

#include "Renderer.h"
#include <anari/anari.h>
#include <vector>

namespace sneeze { namespace renderer {

class HELIDE_RENDERER : public RENDERER
{
public:
   HELIDE_RENDERER ();
   ~HELIDE_RENDERER () override;

   bool Initialize (int nWidth, int nHeight) override;
   void Shutdown () override;

   void SetCamera (const CAMERA_DATA& pCamera) override;
   void BeginFrame () override;
   void SubmitSpheres (const std::vector<SPHERE_DATA>& aSpheres) override;
   void SubmitCurves (const std::vector<CURVE_DATA>& aCurves) override;
   void EndFrame () override;

   const uint32_t* GetFrameBuffer () const override;
   int GetWidth () const override;
   int GetHeight () const override;

private:
   ANARILibrary  m_pLibrary;
   ANARIDevice   m_pDevice;
   ANARIWorld    m_pWorld;
   ANARICamera   m_pCamera;
   ANARIRenderer m_pRenderer;
   ANARIFrame    m_pFrame;

   int m_nWidth;
   int m_nHeight;

   std::vector<uint32_t> m_aPixels;

   void RebuildWorld (const std::vector<SPHERE_DATA>& aSpheres,
                      const std::vector<CURVE_DATA>& aCurves);

   std::vector<SPHERE_DATA> m_aSpheres;
   std::vector<CURVE_DATA>  m_aCurves;
};

}} // namespace sneeze::renderer
