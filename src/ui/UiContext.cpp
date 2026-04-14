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

#include "ui/UiContext.h"

#include <RmlUi/Core.h>
#include <RmlUi/Core/FontEngineInterface.h>
#include <RmlUi/Core/RenderInterface.h>
#include <RmlUi/Core/SystemInterface.h>

#include <chrono>
#include <cstdio>

namespace
{

// Minimal system interface - provides elapsed time and logging.
class STUB_SYSTEM : public Rml::SystemInterface
{
public:
   STUB_SYSTEM ()
   {
      tpStart = std::chrono::steady_clock::now ();
   }

   double GetElapsedTime () override
   {
      auto tpNow = std::chrono::steady_clock::now ();
      return std::chrono::duration<double> (tpNow - tpStart).count ();
   }

   bool LogMessage (Rml::Log::Type nType, const Rml::String& sMessage) override
   {
      const char* szLevel = "INFO";
      if (nType == Rml::Log::LT_ERROR)   szLevel = "ERROR";
      if (nType == Rml::Log::LT_WARNING) szLevel = "WARN";
      if (nType == Rml::Log::LT_DEBUG)   szLevel = "DEBUG";

      std::printf ("UI_CONTEXT [%s]: %s\n", szLevel, sMessage.c_str ());
      return true;
   }

private:
   std::chrono::steady_clock::time_point tpStart;
};

// Minimal render interface - stubs every pure virtual so the library
// links and initializes. No pixels are drawn; real rendering comes
// when RmlUi is wired to ANARI through the SOM.
class STUB_RENDER : public Rml::RenderInterface
{
public:
   Rml::CompiledGeometryHandle CompileGeometry (Rml::Span<const Rml::Vertex>, Rml::Span<const int>) override
   {
      return Rml::CompiledGeometryHandle (1);
   }

   void RenderGeometry (Rml::CompiledGeometryHandle, Rml::Vector2f, Rml::TextureHandle) override {}
   void ReleaseGeometry (Rml::CompiledGeometryHandle) override {}

   Rml::TextureHandle LoadTexture (Rml::Vector2i& pDimensions, const Rml::String&) override
   {
      pDimensions = Rml::Vector2i (1, 1);
      return Rml::TextureHandle (1);
   }

   Rml::TextureHandle GenerateTexture (Rml::Span<const Rml::byte>, Rml::Vector2i) override
   {
      return Rml::TextureHandle (1);
   }

   void ReleaseTexture (Rml::TextureHandle) override {}
   void EnableScissorRegion (bool) override {}
   void SetScissorRegion (Rml::Rectanglei) override {}
};

// Minimal font engine - all methods use the base class defaults (no-ops).
// Real font rendering comes when we integrate FreeType or a custom engine.
class STUB_FONT_ENGINE : public Rml::FontEngineInterface
{
public:
   Rml::FontFaceHandle GetFontFaceHandle (const Rml::String&, Rml::Style::FontStyle, Rml::Style::FontWeight, int) override
   {
      return Rml::FontFaceHandle (1);
   }

   const Rml::FontMetrics& GetFontMetrics (Rml::FontFaceHandle) override
   {
      static Rml::FontMetrics pMetrics = {};
      pMetrics.ascent      = 12.0f;
      pMetrics.descent     = 3.0f;
      pMetrics.line_spacing = 16.0f;
      return pMetrics;
   }

   int GetStringWidth (Rml::FontFaceHandle, Rml::StringView sString, const Rml::TextShapingContext&, Rml::Character) override
   {
      return static_cast<int> (sString.size ()) * 8;
   }
};

static STUB_SYSTEM      pStubSystem;
static STUB_RENDER      pStubRender;
static STUB_FONT_ENGINE pStubFontEngine;

} // anonymous namespace

namespace sneeze
{
namespace ui
{

UI_CONTEXT::UI_CONTEXT ()
   : bInitialized (false)
{
}

UI_CONTEXT::~UI_CONTEXT ()
{
   Shutdown ();
}

bool UI_CONTEXT::Initialize ()
{
   Rml::SetSystemInterface (&pStubSystem);
   Rml::SetRenderInterface (&pStubRender);
   Rml::SetFontEngineInterface (&pStubFontEngine);

   bool bOk = Rml::Initialise ();
   if (!bOk)
   {
      std::fprintf (stderr, "UI_CONTEXT: Rml::Initialise failed\n");
      bInitialized = false;
   }
   else
   {
      bInitialized = true;
      Rml::String sVersion = Rml::GetVersion ();
      std::printf ("UI_CONTEXT: RmlUi %s initialized (stub renderer)\n", sVersion.c_str ());
   }

   return bInitialized;
}

void UI_CONTEXT::Shutdown ()
{
   if (bInitialized)
   {
      Rml::Shutdown ();
      bInitialized = false;
   }
}

} // namespace ui
} // namespace sneeze
