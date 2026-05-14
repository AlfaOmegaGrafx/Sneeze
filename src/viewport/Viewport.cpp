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

#include <Sneeze.h>
#include "scene/Scene.h"
#include "renderer/AnariRenderer.h"

using namespace SNEEZE;

static constexpr float MOUSE_SENSITIVITY = 0.005f;
static constexpr float SCROLL_FACTOR = 1.1f;
static constexpr float MIN_DISTANCE = 0.1f;
static constexpr float MAX_DISTANCE = 1e14f;
static constexpr float PI_F = 3.14159265358979f;

// ===========================================================================
// Impl
// ===========================================================================

class VIEWPORT::Impl
{
public:
   Impl (VIEWPORT* pViewport, ENGINE* pEngine, IVIEWPORT* pHost) :
      m_pViewport (pViewport),
      m_pEngine (pEngine),
      m_pHost (pHost),
      m_eInitState (kINIT_NONE),
      m_bReady (false),
      m_pScene (nullptr),
      m_pRenderer (nullptr),
      m_bRendererPending (false),
      m_bRendererShutdownRequested (false),
      m_bRendererShutdownComplete (false),
      m_nFbWidth (0),
      m_nFbHeight (0),
      m_nWidth (0),
      m_nHeight (0),
      m_bResizePending (false),
      m_nResizeWidth (0),
      m_nResizeHeight (0),
      m_eSession (kSESSION_PERSISTENT)
   {
   }

   ~Impl ()
   {
   }

   bool Initialize (const std::string& sUrl, eSESSION eSession, const std::string& sPath_Permanent, const std::string& sPath_Temporary)
   {
      bool bResult = false;

      m_eSession = eSession;
      m_sPath_Permanent = sPath_Permanent;
      m_sPath_Temporary = sPath_Temporary;

      m_pHost->FrameSize (m_nWidth, m_nHeight);

      std::string sLibrary = m_pEngine->Host ()->sRenderer ();
      if (!sLibrary.empty () && m_pHost->FrameWindow ())
         m_bRendererPending = true;

      m_pScene = new SCENE (m_pViewport);

      if (m_pScene->Initialize (sUrl))
      {
         m_eInitState = kINIT_SCENE;
         m_bReady = true;
         bResult = true;
         m_pEngine->Log (IENGINE::kLOGLEVEL_Info, "VIEWPORT", "Initialized");
      }
      else
      {
         m_pEngine->Log (IENGINE::kLOGLEVEL_Error, "VIEWPORT", "Failed to initialize scene");
      }

      return bResult;
   }

   bool InitializeRenderer ()
   {
      bool bResult = false;

      if (m_bRendererPending)
      {
         m_bRendererPending = false;

         std::string sLibrary = m_pEngine->Host ()->sRenderer ();
         auto* pRenderer = new RENDERER::ANARI (m_pEngine, sLibrary);

         void* pNativeWindow = m_pHost->FrameWindow ();
         if (pNativeWindow)
            pRenderer->SetNativeWindow (pNativeWindow);

         if (pRenderer->Initialize (m_nWidth, m_nHeight))
         {
            m_pRenderer = pRenderer;
            m_eInitState = kINIT_RENDERER;
            bResult = true;
            m_pEngine->Log (IENGINE::kLOGLEVEL_Info, "VIEWPORT", "Renderer initialized on compositor thread");
         }
         else
         {
            delete pRenderer;
            m_pEngine->Log (IENGINE::kLOGLEVEL_Warning, "VIEWPORT", "Renderer unavailable -- headless mode");
         }
      }
      else
      {
         bResult = (m_pRenderer != nullptr);
      }

      return bResult;
   }

   void Shutdown ()
   {
      if (m_eInitState >= kINIT_SCENE)
      {
         if (m_eInitState >= kINIT_RENDERER)
         {
            RequestRendererShutdown ();
         }

         m_pScene->Shutdown ();
      }

      delete m_pScene;
      m_pScene = nullptr;

      m_eInitState = kINIT_NONE;
   }

   void ShutdownRenderer ()
   {
      if (m_pRenderer)
      {
         m_pRenderer->Shutdown ();
         delete m_pRenderer;
         m_pRenderer = nullptr;
      }
   }

   void RequestRendererShutdown ()
   {
      if (m_pRenderer)
      {
         m_bRendererShutdownRequested.store (true);
         std::unique_lock<std::mutex> lock (m_rendererMutex);
         m_rendererCondVar.wait (lock, [this] { return m_bRendererShutdownComplete; });
      }
   }

   bool ServiceRendererShutdown ()
   {
      bool bResult = m_bRendererShutdownRequested.load ();
      if (bResult)
      {
         ShutdownRenderer ();
         {
            std::lock_guard<std::mutex> guard (m_rendererMutex);
            m_bRendererShutdownComplete = true;
         }
         m_rendererCondVar.notify_one ();
      }
      return bResult;
   }

   std::string sViewportId ()
   {
      std::string sResult;

      if (!m_sPath_Temporary.empty ())
         sResult = std::filesystem::path (m_sPath_Temporary).filename ().generic_string ();

      return sResult;
   }

   // ---------------------------------------------------------------------------
   // Input
   // ---------------------------------------------------------------------------

   void SetMouseInput (int nDX, int nDY, float dScrollY, bool bMouseLeft, bool bMouseRight)
   {
      std::lock_guard<std::mutex> guard (m_inputMutex);
      m_Input.nMouseDX += nDX;
      m_Input.nMouseDY += nDY;
      m_Input.dScrollY += dScrollY;
      m_Input.bMouseLeft = bMouseLeft;
      m_Input.bMouseRight = bMouseRight;
   }

   void SetKeyInput (bool bKeySpace, bool bKeyPlus, bool bKeyMinus)
   {
      std::lock_guard<std::mutex> guard (m_inputMutex);
      m_Input.bKeySpace = bKeySpace;
      m_Input.bKeyPlus = bKeyPlus;
      m_Input.bKeyMinus = bKeyMinus;
   }

   INPUT ConsumeInput ()
   {
      std::lock_guard<std::mutex> guard (m_inputMutex);
      INPUT Input = m_Input;
      m_Input.nMouseDX = 0;
      m_Input.nMouseDY = 0;
      m_Input.dScrollY = 0.0f;
      return Input;
   }

   // ---------------------------------------------------------------------------
   // Framebuffer
   // ---------------------------------------------------------------------------

   void WriteFrameBuffer (const uint32_t* pPixels, int nWidth, int nHeight)
   {
      std::lock_guard<std::mutex> guard (m_fbMutex);
      int nSize = nWidth * nHeight;
      m_aFrameBuffer.resize (nSize);
      std::memcpy (m_aFrameBuffer.data (), pPixels, nSize * sizeof (uint32_t));
      m_nFbWidth = nWidth;
      m_nFbHeight = nHeight;
   }

   const uint32_t* LockFrameBuffer (int& nWidth, int& nHeight)
   {
      m_fbMutex.lock ();
      nWidth = m_nFbWidth;
      nHeight = m_nFbHeight;
      return m_aFrameBuffer.empty () ? nullptr : m_aFrameBuffer.data ();
   }

   void UnlockFrameBuffer ()
   {
      m_fbMutex.unlock ();
   }

   // ---------------------------------------------------------------------------
   // Resize
   // ---------------------------------------------------------------------------

   void Resize (int nWidth, int nHeight)
   {
      std::lock_guard<std::mutex> guard (m_resizeMutex);
      m_bResizePending = true;
      m_nResizeWidth = nWidth;
      m_nResizeHeight = nHeight;
   }

   bool ConsumePendingResize (int& nWidth, int& nHeight)
   {
      std::lock_guard<std::mutex> guard (m_resizeMutex);
      bool bResult = m_bResizePending;
      if (m_bResizePending)
      {
         nWidth = m_nResizeWidth;
         nHeight = m_nResizeHeight;
         m_nWidth = nWidth;
         m_nHeight = nHeight;
         m_bResizePending = false;
      }
      return bResult;
   }

   friend class VIEWPORT;

protected:
   VIEWPORT*               m_pViewport;
   ENGINE*                 m_pEngine;
   IVIEWPORT*              m_pHost;
   eINIT_STATE             m_eInitState;
   bool                    m_bReady;
   SCENE*                  m_pScene;
   RENDERER*               m_pRenderer;
   bool                    m_bRendererPending;
   std::atomic<bool>       m_bRendererShutdownRequested;
   bool                    m_bRendererShutdownComplete;
   std::mutex              m_rendererMutex;
   std::condition_variable m_rendererCondVar;

   // Input
   std::mutex              m_inputMutex;
   INPUT                   m_Input;

   // Framebuffer
   std::mutex              m_fbMutex;
   std::vector<uint32_t>   m_aFrameBuffer;
   int                     m_nFbWidth;
   int                     m_nFbHeight;

   // Dimensions
   int                     m_nWidth;
   int                     m_nHeight;

   // Resize request
   std::mutex              m_resizeMutex;
   bool                    m_bResizePending;
   int                     m_nResizeWidth;
   int                     m_nResizeHeight;

   // Paths
   eSESSION                m_eSession;
   std::string             m_sPath_Permanent;
   std::string             m_sPath_Temporary;

   // Camera
   VIEW                    m_View;
};


// ===========================================================================
// VIEWPORT
// ===========================================================================

VIEWPORT::VIEWPORT (ENGINE* pEngine, IVIEWPORT* pHost) :
   m_pImpl (new Impl (this, pEngine, pHost))
{
}

VIEWPORT::~VIEWPORT ()
{
   Shutdown ();

   delete m_pImpl;
}

bool VIEWPORT::Initialize (const std::string& sUrl, eSESSION eSession, const std::string& sPath_Permanent, const std::string& sPath_Temporary)
{
   return m_pImpl->Initialize (sUrl, eSession, sPath_Permanent, sPath_Temporary);
}

bool VIEWPORT::InitializeRenderer ()
{
   return m_pImpl->InitializeRenderer ();
}

void VIEWPORT::Shutdown ()
{
   m_pImpl->Shutdown ();
}

void VIEWPORT::ShutdownRenderer ()
{
   m_pImpl->ShutdownRenderer ();
}

void VIEWPORT::RequestRendererShutdown ()
{
   m_pImpl->RequestRendererShutdown ();
}

bool VIEWPORT::ServiceRendererShutdown ()
{
   return m_pImpl->ServiceRendererShutdown ();
}

// ---------------------------------------------------------------------------
// Accessors
// ---------------------------------------------------------------------------

SNEEZE::ENGINE*      VIEWPORT::Sneeze          () const { return m_pImpl->m_pEngine;         }
IVIEWPORT*           VIEWPORT::Host            () const { return m_pImpl->m_pHost;           }
VIEWPORT::SCENE*     VIEWPORT::Scene           () const { return m_pImpl->m_pScene;          }
VIEWPORT::eSESSION   VIEWPORT::Session         () const { return m_pImpl->m_eSession;        }
const std::string&   VIEWPORT::sPath_Permanent () const { return m_pImpl->m_sPath_Permanent; }
const std::string&   VIEWPORT::sPath_Temporary () const { return m_pImpl->m_sPath_Temporary; }
std::string          VIEWPORT::sViewportId     () const { return m_pImpl->sViewportId ();    }
bool                 VIEWPORT::IsReady         () const { return m_pImpl->m_bReady;          }
int                  VIEWPORT::Width           () const { return m_pImpl->m_nWidth;          }
int                  VIEWPORT::Height          () const { return m_pImpl->m_nHeight;         }
VIEWPORT::VIEW&      VIEWPORT::View            ()       { return m_pImpl->m_View;            }
VIEWPORT::RENDERER*  VIEWPORT::Renderer        () const { return m_pImpl->m_pRenderer;       }

// ---------------------------------------------------------------------------
// Input
// ---------------------------------------------------------------------------

void VIEWPORT::SetMouseInput (int nDX, int nDY, float dScrollY, bool bMouseLeft, bool bMouseRight)
{
   m_pImpl->SetMouseInput (nDX, nDY, dScrollY, bMouseLeft, bMouseRight);
}

void VIEWPORT::SetKeyInput (bool bKeySpace, bool bKeyPlus, bool bKeyMinus)
{
   m_pImpl->SetKeyInput (bKeySpace, bKeyPlus, bKeyMinus);
}

VIEWPORT::INPUT VIEWPORT::ConsumeInput ()
{
   return m_pImpl->ConsumeInput ();
}

// ---------------------------------------------------------------------------
// Framebuffer
// ---------------------------------------------------------------------------

void VIEWPORT::WriteFrameBuffer (const uint32_t* pPixels, int nWidth, int nHeight)
{
   m_pImpl->WriteFrameBuffer (pPixels, nWidth, nHeight);
}

const uint32_t* VIEWPORT::LockFrameBuffer (int& nWidth, int& nHeight)
{
   return m_pImpl->LockFrameBuffer (nWidth, nHeight);
}

void VIEWPORT::UnlockFrameBuffer ()
{
   m_pImpl->UnlockFrameBuffer ();
}

// ---------------------------------------------------------------------------
// Resize
// ---------------------------------------------------------------------------

void VIEWPORT::Resize (int nWidth, int nHeight)
{
   m_pImpl->Resize (nWidth, nHeight);
}

bool VIEWPORT::ConsumePendingResize (int& nWidth, int& nHeight)
{
   return m_pImpl->ConsumePendingResize (nWidth, nHeight);
}

// ---------------------------------------------------------------------------
// VIEW (camera orbit)
// ---------------------------------------------------------------------------

void VIEWPORT::VIEW::Update (int nDX, int nDY, float dScrollY, bool bMouseLeft, bool bMouseRight)
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
