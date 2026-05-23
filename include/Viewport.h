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

#ifndef SNEEZE_VIEWPORT_H
#define SNEEZE_VIEWPORT_H

namespace SNEEZE
{
   class VIEWPORT
   {
   public:
      class SCENE;
      class MSF;
      class RENDERER;

   public:
      enum eINIT_STATE
      {
         kINIT_NONE,
         kINIT_SCENE,
         kINIT_RENDERER,
      };

      // --- Camera orbit state ---

      class VIEW
      {
      public:
         float dTheta    = 0.3f;
         float dPhi      = 0.4f;
         float dDistance = 10.0f;
         float dTargetX  = 0.0f;
         float dTargetY  = 0.0f;
         float dTargetZ  = 0.0f;

         void Update (int nDX, int nDY, float dScrollY, bool bMouseLeft, bool bMouseRight);
      };

      // --- Per-frame input state ---

      struct INPUT
      {
         int   nMouseDX    = 0;
         int   nMouseDY    = 0;
         float dScrollY    = 0.0f;
         bool  bMouseLeft  = false;
         bool  bMouseRight = false;

         bool  bKeySpace   = false;
         bool  bKeyPlus    = false;
         bool  bKeyMinus   = false;
      };

      // ------------------------------------------------------------------------

      explicit VIEWPORT (CONTEXT* pContext);
      ~VIEWPORT ();

      bool Initialize (const std::string& sUrl);
      bool InitializeRenderer ();
      void ShutdownRenderer ();
      void RequestRendererShutdown ();
      bool ServiceRendererShutdown ();

      void Attach (IVIEWPORT* pHost);
      void Detach ();

      ENGINE*              Engine () const;
      CONTEXT*             Context () const;
      IVIEWPORT*           Host () const;
      SCENE*               Scene () const;
      bool                 IsReady () const;

      // --- Input (called by application) ---

      void  SetMouseInput (int nDX, int nDY, float dScrollY, bool bMouseLeft, bool bMouseRight);
      void  SetKeyInput (bool bKeySpace, bool bKeyPlus, bool bKeyMinus);
      INPUT ConsumeInput ();

      // --- Framebuffer ---

      void            WriteFrameBuffer (const uint32_t* pPixels, int nWidth, int nHeight);
      const uint32_t* LockFrameBuffer (int& nWidth, int& nHeight);
      void            UnlockFrameBuffer ();

      // --- Dimensions ---

      int  Width () const;
      int  Height () const;
      void SetDimensions (int nWidth, int nHeight);

      // --- Camera ---

      VIEW& View ();

      // --- Renderer ---

      RENDERER* Renderer () const;

   private:
      class Impl;
      Impl* m_pImpl;
   };
}
#endif // SNEEZE_VIEWPORT_H
