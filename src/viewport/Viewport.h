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

#include "Sneeze.h"
#include <mutex>
#include <vector>
#include <cstdint>
#include <cmath>
#include <algorithm>
#include <string>

// ---------------------------------------------------------------------------
// IVIEWPORT -- per-viewport interface between the host and a viewport.
// Each viewport gets its own IVIEWPORT instance from the application.
// ---------------------------------------------------------------------------

class SNEEZE::IVIEWPORT
{
public:
   virtual ~IVIEWPORT () = default;

   // --- Configuration (set by host before Viewport_Open) ---

   void*       pNativeWindow  = nullptr;
   int         nWidth         = 0;
   int         nHeight        = 0;

   // --- Callbacks (host must implement) ---

   virtual void OnFrameReady (const uint32_t* pFB, int nFbW, int nFbH) = 0;

   // --- Inspector callbacks (optional) ---

   virtual void OnNetworkFileCreated (NOTIFICATION*) {}
   virtual void OnNetworkFileChanged (NOTIFICATION*) {}
   virtual void OnNetworkFileDeleted (NOTIFICATION*) {}

   virtual void OnStorageUnitCreated (NOTIFICATION*) {}
   virtual void OnStorageUnitChanged (NOTIFICATION*) {}
   virtual void OnStorageUnitDeleted (NOTIFICATION*) {}
};

// ---------------------------------------------------------------------------

class SNEEZE::VIEWPORT
{
public:
   class SCENE;
   class CONTAINER;
   class MSF;
   class RENDERER;

   // --- Camera orbit state ---

   struct VIEW
   {
      float dTheta    = 0.3f;
      float dPhi      = 0.4f;
      float dDistance  = 10.0f;
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

   explicit VIEWPORT (SNEEZE* pSneeze, SNEEZE::IVIEWPORT* pHost);
   ~VIEWPORT ();

   bool Initialize (const std::string& sUrl);
   bool InitializeRenderer ();
   void Shutdown ();

   SNEEZE*            Sneeze () const;
   SNEEZE::IVIEWPORT* Host () const;
   SCENE*             Scene () const;

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
   void Resize (int nWidth, int nHeight);
   bool ConsumePendingResize (int& nWidth, int& nHeight);

   // --- Camera ---

   VIEW& View ();

   // --- Renderer ---

   RENDERER* Renderer () const;

private:
   SNEEZE*              m_pSneeze;
   SNEEZE::IVIEWPORT*   m_pHost;
   SCENE*               m_pScene;
   RENDERER*            m_pRenderer;
   bool                 m_bRendererPending;

   // Input
   std::mutex           m_inputMutex;
   INPUT                m_Input;

   // Framebuffer
   std::mutex           m_fbMutex;
   std::vector<uint32_t> m_aFrameBuffer;
   int                  m_nFbWidth;
   int                  m_nFbHeight;

   // Dimensions
   int                  m_nWidth;
   int                  m_nHeight;

   // Resize request
   std::mutex           m_resizeMutex;
   bool                 m_bResizePending;
   int                  m_nResizeWidth;
   int                  m_nResizeHeight;

   // Camera
   VIEW                 m_View;
};

#endif // SNEEZE_VIEWPORT_H
