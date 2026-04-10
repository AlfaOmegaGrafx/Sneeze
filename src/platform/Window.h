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

#include <SDL3/SDL.h>
#include <cstdint>

namespace sneeze { namespace platform {

class WINDOW
{
public:
   WINDOW ();
   ~WINDOW ();

   bool Initialize (int nWidth, int nHeight, const char* sTitle);
   void Shutdown ();

   bool IsOpen () const;
   void PollEvents ();
   void Present (const uint32_t* pPixels, int nWidth, int nHeight);

   int  GetWidth () const;
   int  GetHeight () const;

   // Input state (updated by PollEvents)
   bool  bQuit;
   int   nMouseX;
   int   nMouseY;
   int   nMouseDX;
   int   nMouseDY;
   bool  bMouseLeft;
   bool  bMouseRight;
   float dScrollY;
   bool  bKeySpace;
   bool  bKeyPlus;
   bool  bKeyMinus;

private:
   SDL_Window*   m_pWindow;
   SDL_Renderer* m_pSDLRenderer;
   SDL_Texture*  m_pTexture;
   int           m_nWidth;
   int           m_nHeight;
};

}} // namespace sneeze::platform
