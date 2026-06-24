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

#ifndef SNEEZE_UI_CONTEXT_H
#define SNEEZE_UI_CONTEXT_H

#include <cstdint>

namespace SNEEZE
{
   class ENGINE;

   namespace DEP
   {
      class UI_RENDER;

      // Shared RmlUi manager: owns the one-time global RmlUi lifecycle (init
      // and system interface) for the engine. Fonts are not owned here -- the
      // host application loads its faces into RmlUi's process-global font
      // registry, which this engine instance shares. Individual panels
      // (UI_PANEL, one per MAP_OBJECT_PANEL) drive their own contexts/canvases.
      // There is exactly one UI_CONTEXT per ENGINE, reachable via
      // ENGINE::Ui_Context().
      class UI_CONTEXT
      {
      public:
         UI_CONTEXT ();
         ~UI_CONTEXT ();

         bool Initialize (ENGINE* pEngine);

         ENGINE* Engine () const { return m_pEngine; }

         // The one render interface for all engine-side panels. RmlUi keeps a
         // process-global render-manager registry keyed by render interface; the
         // host application shares that registry and may release it. Every panel
         // therefore binds its context to this single, engine-lifetime interface
         // so RmlUi never holds a render manager for a freed interface.
         UI_RENDER* Render () const { return m_pRender; }

      private:
         ENGINE*    m_pEngine;
         UI_RENDER* m_pRender;        // shared by every panel; outlives Rml::Shutdown
         bool       m_bInitialized;
      };
   } // namespace DEP
}
#endif // SNEEZE_UI_CONTEXT_H
