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

#ifndef SNEEZE_RENDERER_ANARIRENDERER_H
#define SNEEZE_RENDERER_ANARIRENDERER_H

#include "Viewport.h"
#include <unordered_map>

// Forward declarations for ANARI types used in RENDERER::ANARI class body
// to avoid transitively including in every unit that uses AnariRenderer.h
namespace anari 
{ 
   namespace api 
   {
      struct Library;
      struct Object;
      struct Device;
      struct Camera;
      struct Array;
      struct Frame;
      struct Renderer;
      struct World;
   }
} // namespace api::anari

namespace SNEEZE
{
   class VIEWPORT::RENDERER::ANARI : public VIEWPORT::RENDERER
   {
   public:
      explicit ANARI (ENGINE* pEngine, const std::string& sLibrary);
      ~ANARI () override;

      void SetNativeWindow (void* pHandle) override;
      bool IsRenderingToNativeSurface () const override;

      bool Initialize (int nWidth, int nHeight) override;
      void Resize (int nWidth, int nHeight) override;

      void SetCamera (const CAMERA_DATA& pCamera) override;
      void SetLights (const std::vector<LIGHT_DATA>& aLight) override;
      void BeginFrame () override;
      void SubmitSpheres (const std::vector<SPHERE_DATA>& aSpheres) override;
      void SubmitCurves (const std::vector<CURVE_DATA>& aCurves) override;
      void SubmitBoxes (const std::vector<BOX_DATA>& aBoxes) override;
      void SubmitPanels (const std::vector<PANEL_DATA>& aPanels) override;
      void EndFrame () override;

      void InvalidateScene () override;

      const uint32_t* GetFrameBuffer () const override;
      int GetWidth () const override;
      int GetHeight () const override;

      double GetLastSubmitSeconds () const override { return m_dLastSubmitSeconds; }
      double GetLastRenderSeconds () const override { return m_dLastRenderSeconds; }

   private:
      ENGINE*                 m_pEngine;
      std::string             m_sLibrary;
      anari::api::Library*    m_pLibrary;
      anari::api::Device*     m_pDevice;
      anari::api::World*      m_pWorld;
      anari::api::Camera*     m_pCamera;
      anari::api::Renderer*   m_pRenderer;
      anari::api::Frame*      m_pFrame;
      anari::api::Object*     m_pNativeSurface;

      void* m_pNativeWindow;
      bool  m_bNativeSurface;

      int m_nWidth;
      int m_nHeight;

      std::vector<uint32_t> m_aPixels;

      std::vector<SPHERE_DATA> m_aSpheres;
      std::vector<CURVE_DATA>  m_aCurves;
      std::vector<BOX_DATA>    m_aBoxes;
      std::vector<PANEL_DATA>  m_aPanels;

      UV_SPHERE m_pUnitSphere;
      bool      m_bUnitSphereReady;

      UV_SPHERE m_pUnitBox;
      bool      m_bUnitBoxReady;
      std::unordered_map<const uint8_t*, std::vector<float>> m_pColorCache;

      struct SCENE_STATE;
      SCENE_STATE* m_pSceneState;

      bool m_bSceneDirty;

      std::vector<LIGHT_DATA> m_aLight;

      void ReleaseScene ();
      void BuildScene (const std::vector<SPHERE_DATA>& aSpheres, const std::vector<CURVE_DATA>& aCurves, const std::vector<BOX_DATA>& aBoxes, const std::vector<PANEL_DATA>& aPanels);
      void UpdateScene (const std::vector<SPHERE_DATA>& aSpheres, const std::vector<CURVE_DATA>& aCurves, const std::vector<BOX_DATA>& aBoxes, const std::vector<PANEL_DATA>& aPanels);
      bool SceneNeedsRebuild (const std::vector<SPHERE_DATA>& aSpheres, const std::vector<CURVE_DATA>& aCurves, const std::vector<BOX_DATA>& aBoxes, const std::vector<PANEL_DATA>& aPanels) const;

      double m_dLastSubmitSeconds;
      double m_dLastRenderSeconds;
   };
}
#endif // SNEEZE_RENDERER_ANARIRENDERER_H
