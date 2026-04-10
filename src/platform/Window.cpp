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

#include "Window.h"
#include <cstdio>
#include <cstring>

namespace sneeze { namespace platform {

WINDOW::WINDOW ()
   : bQuit (false)
   , nMouseX (0), nMouseY (0)
   , nMouseDX (0), nMouseDY (0)
   , bMouseLeft (false), bMouseRight (false)
   , dScrollY (0.0f)
   , bKeySpace (false), bKeyPlus (false), bKeyMinus (false)
   , m_pWindow (nullptr)
   , m_pSDLRenderer (nullptr)
   , m_pTexture (nullptr)
   , m_nWidth (0), m_nHeight (0)
{
}

WINDOW::~WINDOW ()
{
   Shutdown ();
}

bool WINDOW::Initialize (int nWidth, int nHeight, const char* sTitle)
{
   m_nWidth  = nWidth;
   m_nHeight = nHeight;

   bool bOk = false;

   if (!SDL_Init (SDL_INIT_VIDEO))
   {
      std::fprintf (stderr, "SDL_Init failed: %s\n", SDL_GetError ());
   }
   else
   {
      m_pWindow = SDL_CreateWindow (sTitle, nWidth, nHeight, 0);
      if (!m_pWindow)
      {
         std::fprintf (stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError ());
      }
      else
      {
         m_pSDLRenderer = SDL_CreateRenderer (m_pWindow, nullptr);
         if (!m_pSDLRenderer)
         {
            std::fprintf (stderr, "SDL_CreateRenderer failed: %s\n", SDL_GetError ());
         }
         else
         {
            m_pTexture = SDL_CreateTexture (m_pSDLRenderer, SDL_PIXELFORMAT_ABGR8888,
                                             SDL_TEXTUREACCESS_STREAMING, nWidth, nHeight);
            if (!m_pTexture)
            {
               std::fprintf (stderr, "SDL_CreateTexture failed: %s\n", SDL_GetError ());
            }
            else
            {
               bOk = true;
            }
         }
      }
   }

   return bOk;
}

void WINDOW::Shutdown ()
{
   if (m_pTexture)
   {
      SDL_DestroyTexture (m_pTexture);
      m_pTexture = nullptr;
   }
   if (m_pSDLRenderer)
   {
      SDL_DestroyRenderer (m_pSDLRenderer);
      m_pSDLRenderer = nullptr;
   }
   if (m_pWindow)
   {
      SDL_DestroyWindow (m_pWindow);
      m_pWindow = nullptr;
   }
   SDL_Quit ();
}

bool WINDOW::IsOpen () const
{
   return !bQuit;
}

void WINDOW::PollEvents ()
{
   nMouseDX = 0;
   nMouseDY = 0;
   dScrollY = 0.0f;

   SDL_Event event;
   while (SDL_PollEvent (&event))
   {
      switch (event.type)
      {
         case SDL_EVENT_QUIT:
            bQuit = true;
            break;

         case SDL_EVENT_MOUSE_MOTION:
            nMouseX  = static_cast<int> (event.motion.x);
            nMouseY  = static_cast<int> (event.motion.y);
            nMouseDX = static_cast<int> (event.motion.xrel);
            nMouseDY = static_cast<int> (event.motion.yrel);
            break;

         case SDL_EVENT_MOUSE_BUTTON_DOWN:
            if (event.button.button == SDL_BUTTON_LEFT)  bMouseLeft = true;
            if (event.button.button == SDL_BUTTON_RIGHT) bMouseRight = true;
            break;

         case SDL_EVENT_MOUSE_BUTTON_UP:
            if (event.button.button == SDL_BUTTON_LEFT)  bMouseLeft = false;
            if (event.button.button == SDL_BUTTON_RIGHT) bMouseRight = false;
            break;

         case SDL_EVENT_MOUSE_WHEEL:
            dScrollY = event.wheel.y;
            break;

         case SDL_EVENT_KEY_DOWN:
            if (event.key.key == SDLK_SPACE)  bKeySpace = true;
            if (event.key.key == SDLK_EQUALS) bKeyPlus  = true;
            if (event.key.key == SDLK_MINUS)  bKeyMinus = true;
            if (event.key.key == SDLK_ESCAPE) bQuit = true;
            break;

         case SDL_EVENT_KEY_UP:
            if (event.key.key == SDLK_SPACE)  bKeySpace = false;
            if (event.key.key == SDLK_EQUALS) bKeyPlus  = false;
            if (event.key.key == SDLK_MINUS)  bKeyMinus = false;
            break;
      }
   }
}

void WINDOW::Present (const uint32_t* pPixels, int nWidth, int nHeight)
{
   if (m_pTexture  &&  pPixels)
   {
      SDL_UpdateTexture (m_pTexture, nullptr, pPixels, nWidth * sizeof (uint32_t));
      SDL_RenderClear (m_pSDLRenderer);
      SDL_RenderTexture (m_pSDLRenderer, m_pTexture, nullptr, nullptr);
      SDL_RenderPresent (m_pSDLRenderer);
   }
}

int WINDOW::GetWidth () const
{
   return m_nWidth;
}

int WINDOW::GetHeight () const
{
   return m_nHeight;
}

}} // namespace sneeze::platform
