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

#include <atomic>
#include <mutex>
#include <condition_variable>
#include <vector>
#include <cstdint>
#include <cmath>
#include <algorithm>
#include <string>

namespace SNEEZE
{
   // ---------------------------------------------------------------------------
   // IVIEWPORT -- per-viewport interface between the host and a viewport.
   // Each viewport gets its own IVIEWPORT instance from the application.
   // ---------------------------------------------------------------------------

   class IVIEWPORT
   {
   public:
      virtual ~IVIEWPORT () = default;

      // --- Callbacks (host must implement) ---

      virtual void* GetFrameWindow ()                                         = 0;
      virtual void  GetFrameSize   (int &nWidth, int& nHeight)                = 0;

      virtual void  OnFrameReady   (const uint32_t* pFB, int nFbW, int nFbH)  = 0;

      // --- Inspector callbacks (optional) ---

      virtual void OnNetworkFileCreated (NOTIFICATION*) {}
      virtual void OnNetworkFileChanged (NOTIFICATION*) {}
      virtual void OnNetworkFileDeleted (NOTIFICATION*) {}

      virtual void OnStorageUnitCreated (NOTIFICATION*) {}
      virtual void OnStorageUnitChanged (NOTIFICATION*) {}
      virtual void OnStorageUnitDeleted (NOTIFICATION*) {}
   };

   // ---------------------------------------------------------------------------

   class VIEWPORT
   {
   public:
      class SCENE;
      class MSF;
      class RENDERER;

   public:
      // ---------------------------------------------------------------------------
      // VIEWPORT::CONTAINER — the runtime manifestation of an MSF file.
      //
      // NAME is the identity record for a container. Uniqueness is determined by
      // the tuple (persona hash, fingerprint, container name).
      // ---------------------------------------------------------------------------

      class CONTAINER
      {
      public:
         class NAME
         {
         public:
            std::string sFingerprint;
            std::string sOrganization;
            std::string sCommonName;
            std::string sContainerName;
            std::string sPersonaHash;
            bool        bValidated;

            std::string DisplayName () const { return sCommonName + "/" + sContainerName; }
         };
      };

   public:
      enum eSESSION
      {
         kPERSISTENT,
         kTRANSITORY
      };

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

      explicit VIEWPORT (ENGINE* pEngine, IVIEWPORT* pHost);
      ~VIEWPORT ();

      bool Initialize (const std::string& sUrl);
      bool InitializeRenderer ();
      void Shutdown ();
      void ShutdownRenderer ();
      void RequestRendererShutdown ();
      bool ServiceRendererShutdown ();

      ENGINE*    Sneeze () const;
      IVIEWPORT* Host () const;
      SCENE*     Scene () const;

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
      ENGINE*              m_pEngine;
      IVIEWPORT*           m_pHost;
      SCENE*               m_pScene;
      RENDERER*            m_pRenderer;
      bool                 m_bRendererPending;
      std::atomic<bool>    m_bRendererShutdownRequested;
      bool                 m_bRendererShutdownComplete;
      std::mutex           m_rendererMutex;
      std::condition_variable m_rendererCondVar;

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
}
#endif // SNEEZE_VIEWPORT_H
