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

#include "AnariRenderer.h"
#include <anari/anari.h>
#include <anari/ext/helide/anariNewHelideDevice.h>
#include <cstring>
#include <cstdio>

namespace sneeze { namespace renderer {

// ---------------------------------------------------------------------------

HELIDE_RENDERER::HELIDE_RENDERER ()
   : m_pDevice (nullptr)
   , m_pWorld (nullptr)
   , m_pCamera (nullptr)
   , m_pRenderer (nullptr)
   , m_pFrame (nullptr)
   , m_nWidth (0)
   , m_nHeight (0)
{
}

HELIDE_RENDERER::~HELIDE_RENDERER ()
{
   Shutdown ();
}

// ---------------------------------------------------------------------------

bool HELIDE_RENDERER::Initialize (int nWidth, int nHeight)
{
   m_nWidth  = nWidth;
   m_nHeight = nHeight;
   m_aPixels.resize (nWidth * nHeight, 0);

   bool bOk = false;

   m_pDevice = anariNewHelideDevice (nullptr, nullptr);
   if (!m_pDevice)
   {
      std::fprintf (stderr, "ANARI: failed to create helide device\n");
   }
   else
   {
      anariCommitParameters (m_pDevice, m_pDevice);

      m_pWorld = anariNewWorld (m_pDevice);
      m_pCamera = anariNewCamera (m_pDevice, "perspective");
      m_pRenderer = anariNewRenderer (m_pDevice, "default");

      float bgColor[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
      anariSetParameter (m_pDevice, m_pRenderer, "background", ANARI_FLOAT32_VEC4, bgColor);
      anariCommitParameters (m_pDevice, m_pRenderer);

      m_pFrame = anariNewFrame (m_pDevice);
      uint32_t aSize[2] = { static_cast<uint32_t> (nWidth), static_cast<uint32_t> (nHeight) };
      anariSetParameter (m_pDevice, m_pFrame, "size", ANARI_UINT32_VEC2, aSize);
      ANARIDataType nColorType = ANARI_UFIXED8_RGBA_SRGB;
      anariSetParameter (m_pDevice, m_pFrame, "channel.color", ANARI_DATA_TYPE, &nColorType);
      anariSetParameter (m_pDevice, m_pFrame, "renderer", ANARI_RENDERER, &m_pRenderer);
      anariSetParameter (m_pDevice, m_pFrame, "camera", ANARI_CAMERA, &m_pCamera);
      anariSetParameter (m_pDevice, m_pFrame, "world", ANARI_WORLD, &m_pWorld);
      anariCommitParameters (m_pDevice, m_pFrame);

      bOk = true;
   }

   return bOk;
}

void HELIDE_RENDERER::Shutdown ()
{
   if (m_pDevice)
   {
      if (m_pFrame)
      {
         anariRelease (m_pDevice, m_pFrame);
         m_pFrame = nullptr;
      }
      if (m_pRenderer)
      {
         anariRelease (m_pDevice, m_pRenderer);
         m_pRenderer = nullptr;
      }
      if (m_pCamera)
      {
         anariRelease (m_pDevice, m_pCamera);
         m_pCamera = nullptr;
      }
      if (m_pWorld)
      {
         anariRelease (m_pDevice, m_pWorld);
         m_pWorld = nullptr;
      }
      anariRelease (m_pDevice, m_pDevice);
      m_pDevice = nullptr;
   }
}

// ---------------------------------------------------------------------------

void HELIDE_RENDERER::SetCamera (const CAMERA_DATA& pCamera)
{
   float pos[3] = { pCamera.dPosX, pCamera.dPosY, pCamera.dPosZ };
   float dir[3] = { pCamera.dDirX, pCamera.dDirY, pCamera.dDirZ };
   float up[3]  = { pCamera.dUpX,  pCamera.dUpY,  pCamera.dUpZ };

   anariSetParameter (m_pDevice, m_pCamera, "position", ANARI_FLOAT32_VEC3, pos);
   anariSetParameter (m_pDevice, m_pCamera, "direction", ANARI_FLOAT32_VEC3, dir);
   anariSetParameter (m_pDevice, m_pCamera, "up", ANARI_FLOAT32_VEC3, up);
   anariSetParameter (m_pDevice, m_pCamera, "fovy", ANARI_FLOAT32, &pCamera.dFovY);
   anariSetParameter (m_pDevice, m_pCamera, "aspect", ANARI_FLOAT32, &pCamera.dAspect);
   anariCommitParameters (m_pDevice, m_pCamera);
}

void HELIDE_RENDERER::BeginFrame ()
{
   m_aSpheres.clear ();
   m_aCurves.clear ();
}

void HELIDE_RENDERER::SubmitSpheres (const std::vector<SPHERE_DATA>& aSpheres)
{
   m_aSpheres.insert (m_aSpheres.end (), aSpheres.begin (), aSpheres.end ());
}

void HELIDE_RENDERER::SubmitCurves (const std::vector<CURVE_DATA>& aCurves)
{
   m_aCurves.insert (m_aCurves.end (), aCurves.begin (), aCurves.end ());
}

void HELIDE_RENDERER::EndFrame ()
{
   RebuildWorld (m_aSpheres, m_aCurves);

   anariCommitParameters (m_pDevice, m_pWorld);
   anariCommitParameters (m_pDevice, m_pFrame);

   anariRenderFrame (m_pDevice, m_pFrame);
   anariFrameReady (m_pDevice, m_pFrame, ANARI_WAIT);

   uint32_t nW = 0, nH = 0;
   ANARIDataType nType = ANARI_UNKNOWN;
   const void* pData = anariMapFrame (m_pDevice, m_pFrame, "channel.color", &nW, &nH, &nType);

   if (pData)
   {
      std::memcpy (m_aPixels.data (), pData, nW * nH * sizeof (uint32_t));
      anariUnmapFrame (m_pDevice, m_pFrame, "channel.color");
   }
}

const uint32_t* HELIDE_RENDERER::GetFrameBuffer () const
{
   return m_aPixels.data ();
}

int HELIDE_RENDERER::GetWidth () const
{
   return m_nWidth;
}

int HELIDE_RENDERER::GetHeight () const
{
   return m_nHeight;
}

// ---------------------------------------------------------------------------
//  RebuildWorld — recreate ANARI scene objects from submitted data
// ---------------------------------------------------------------------------

void HELIDE_RENDERER::RebuildWorld (const std::vector<SPHERE_DATA>& aSpheres,
                                    const std::vector<CURVE_DATA>& aCurves)
{
   std::vector<ANARISurface> aSurfaces;

   // --- Spheres: one surface per sphere (each has its own color) ---

   for (const auto& s : aSpheres)
   {
      ANARIGeometry pGeom = anariNewGeometry (m_pDevice, "sphere");
      float pos[3] = { s.x, s.y, s.z };
      ANARIArray1D pPosArr = anariNewArray1D (m_pDevice, &pos, nullptr, nullptr, ANARI_FLOAT32_VEC3, 1);
      anariSetParameter (m_pDevice, pGeom, "vertex.position", ANARI_ARRAY1D, &pPosArr);
      anariSetParameter (m_pDevice, pGeom, "radius", ANARI_FLOAT32, &s.dRadius);
      anariCommitParameters (m_pDevice, pGeom);
      anariRelease (m_pDevice, pPosArr);

      ANARIMaterial pMat = anariNewMaterial (m_pDevice, "matte");
      float color[3] = { s.r, s.g, s.b };
      anariSetParameter (m_pDevice, pMat, "color", ANARI_FLOAT32_VEC3, color);
      anariCommitParameters (m_pDevice, pMat);

      ANARISurface pSurf = anariNewSurface (m_pDevice);
      anariSetParameter (m_pDevice, pSurf, "geometry", ANARI_GEOMETRY, &pGeom);
      anariSetParameter (m_pDevice, pSurf, "material", ANARI_MATERIAL, &pMat);
      anariCommitParameters (m_pDevice, pSurf);

      aSurfaces.push_back (pSurf);

      anariRelease (m_pDevice, pGeom);
      anariRelease (m_pDevice, pMat);
   }

   // --- Curves: one surface per orbit path ---

   for (const auto& c : aCurves)
   {
      if (c.aPoints.empty ()) continue;

      ANARIGeometry pGeom = anariNewGeometry (m_pDevice, "curve");

      std::vector<float> aPos;
      std::vector<float> aRadii;
      aPos.reserve (c.aPoints.size () * 3);
      aRadii.reserve (c.aPoints.size ());

      for (const auto& p : c.aPoints)
      {
         aPos.push_back (p.x);
         aPos.push_back (p.y);
         aPos.push_back (p.z);
         aRadii.push_back (p.dRadius);
      }

      ANARIArray1D pPosArr = anariNewArray1D (m_pDevice, aPos.data (), nullptr, nullptr,
                                               ANARI_FLOAT32_VEC3, c.aPoints.size ());
      ANARIArray1D pRadArr = anariNewArray1D (m_pDevice, aRadii.data (), nullptr, nullptr,
                                               ANARI_FLOAT32, c.aPoints.size ());

      anariSetParameter (m_pDevice, pGeom, "vertex.position", ANARI_ARRAY1D, &pPosArr);
      anariSetParameter (m_pDevice, pGeom, "vertex.radius", ANARI_ARRAY1D, &pRadArr);
      anariCommitParameters (m_pDevice, pGeom);

      anariRelease (m_pDevice, pPosArr);
      anariRelease (m_pDevice, pRadArr);

      ANARIMaterial pMat = anariNewMaterial (m_pDevice, "matte");
      float color[3] = { c.r, c.g, c.b };
      anariSetParameter (m_pDevice, pMat, "color", ANARI_FLOAT32_VEC3, color);
      anariCommitParameters (m_pDevice, pMat);

      ANARISurface pSurf = anariNewSurface (m_pDevice);
      anariSetParameter (m_pDevice, pSurf, "geometry", ANARI_GEOMETRY, &pGeom);
      anariSetParameter (m_pDevice, pSurf, "material", ANARI_MATERIAL, &pMat);
      anariCommitParameters (m_pDevice, pSurf);

      aSurfaces.push_back (pSurf);

      anariRelease (m_pDevice, pGeom);
      anariRelease (m_pDevice, pMat);
   }

   // --- Group + instance ---

   if (!aSurfaces.empty ())
   {
      ANARIArray1D pSurfArr = anariNewArray1D (m_pDevice, aSurfaces.data (), nullptr, nullptr,
                                                ANARI_SURFACE, aSurfaces.size ());
      ANARIGroup pGroup = anariNewGroup (m_pDevice);
      anariSetParameter (m_pDevice, pGroup, "surface", ANARI_ARRAY1D, &pSurfArr);
      anariCommitParameters (m_pDevice, pGroup);
      anariRelease (m_pDevice, pSurfArr);

      ANARIInstance pInst = anariNewInstance (m_pDevice, "transform");
      anariSetParameter (m_pDevice, pInst, "group", ANARI_GROUP, &pGroup);
      anariCommitParameters (m_pDevice, pInst);
      anariRelease (m_pDevice, pGroup);

      ANARIArray1D pInstArr = anariNewArray1D (m_pDevice, &pInst, nullptr, nullptr,
                                                ANARI_INSTANCE, 1);
      anariSetParameter (m_pDevice, m_pWorld, "instance", ANARI_ARRAY1D, &pInstArr);
      anariRelease (m_pDevice, pInstArr);
      anariRelease (m_pDevice, pInst);
   }

   // --- Point light at origin (the Sun) ---

   ANARILight pLight = anariNewLight (m_pDevice, "point");
   float lightPos[3] = { 0.0f, 0.0f, 0.0f };
   float lightColor[3] = { 1.0f, 1.0f, 0.95f };
   float lightIntensity = 1.0f;
   anariSetParameter (m_pDevice, pLight, "position", ANARI_FLOAT32_VEC3, lightPos);
   anariSetParameter (m_pDevice, pLight, "color", ANARI_FLOAT32_VEC3, lightColor);
   anariSetParameter (m_pDevice, pLight, "intensity", ANARI_FLOAT32, &lightIntensity);
   anariCommitParameters (m_pDevice, pLight);

   ANARIArray1D pLightArr = anariNewArray1D (m_pDevice, &pLight, nullptr, nullptr,
                                              ANARI_LIGHT, 1);
   anariSetParameter (m_pDevice, m_pWorld, "light", ANARI_ARRAY1D, &pLightArr);
   anariRelease (m_pDevice, pLightArr);
   anariRelease (m_pDevice, pLight);

   for (auto& s : aSurfaces)
   {
      anariRelease (m_pDevice, s);
   }
}

}} // namespace sneeze::renderer
