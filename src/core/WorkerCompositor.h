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

#ifndef SNEEZE_CORE_WORKERCOMPOSITOR_H
#define SNEEZE_CORE_WORKERCOMPOSITOR_H

#include "Worker.h"
#include "renderer/AnariRenderer.h"
#include "view/CameraOrbit.h"
#include <chrono>

namespace SNEEZE { namespace CORE {

class WORKER_COMPOSITOR : public WORKER
{
public:
   explicit WORKER_COMPOSITOR (SNEEZE* pSneeze);

protected:
   void Tick () override;
   void ThreadLoop () override;

private:
   renderer::ANARI_RENDERER  m_pRenderer;
   view::CAMERA_ORBIT        m_pCameraOrbit;

   int64_t m_tmNow;
   double  m_dTimeScale;
   bool    m_bPaused;
   bool    m_bSpaceWasDown;

   std::chrono::steady_clock::time_point m_tpLastFrame;

   int    m_nFrameCount;
   double m_dFpsAccum;
   double m_dAccumInput;
   double m_dAccumScene;
   double m_dAccumSubmit;
   double m_dAccumRender;
   double m_dAccumPublish;
   double m_dAccumFlush;
};

}} // namespace SNEEZE::CORE

#endif // SNEEZE_CORE_WORKERCOMPOSITOR_H
