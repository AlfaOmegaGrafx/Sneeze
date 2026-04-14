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

#include <RmlUi/Core.h>
#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Core/FontEngineInterface.h>
#include <RmlUi/Core/RenderInterface.h>
#include <RmlUi/Core/StreamMemory.h>
#include <RmlUi/Core/SystemInterface.h>

#include <chrono>
#include <cstdio>
#include <cstring>
#include <string>

static int nPassed = 0;
static int nFailed = 0;

static void Check (bool bCondition, const char* szName)
{
   if (bCondition)
   {
      std::printf ("  PASS: %s\n", szName);
      nPassed++;
   }
   else
   {
      std::printf ("  FAIL: %s\n", szName);
      nFailed++;
   }
}

// ---------------------------------------------------------------------------
// Stub interfaces (same as UiContext.cpp - self-contained for the test)
// ---------------------------------------------------------------------------

namespace
{

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

   bool LogMessage (Rml::Log::Type, const Rml::String& sMessage) override
   {
      std::printf ("    [RmlUi] %s\n", sMessage.c_str ());
      return true;
   }

private:
   std::chrono::steady_clock::time_point tpStart;
};

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
      pMetrics.ascent       = 12.0f;
      pMetrics.descent      = 3.0f;
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

// ---------------------------------------------------------------------------
// Test 1: Version and initialization
// ---------------------------------------------------------------------------
static void TestInitialization ()
{
   std::printf ("\n[Test 1] Version and initialization\n");

   Rml::String sVersion = Rml::GetVersion ();
   Check (!sVersion.empty (), "Version string is non-empty");
   std::printf ("    RmlUi version: %s\n", sVersion.c_str ());
}

// ---------------------------------------------------------------------------
// Test 2: Create and destroy a context
// ---------------------------------------------------------------------------
static void TestContext ()
{
   std::printf ("\n[Test 2] Create and destroy a context\n");

   Rml::Context* pContext = Rml::CreateContext ("test", Rml::Vector2i (1280, 720));
   Check (pContext != nullptr, "CreateContext returned a context");

   if (pContext)
   {
      Check (pContext->GetName () == "test", "Context name is 'test'");

      Rml::Vector2i pDimensions = pContext->GetDimensions ();
      Check (pDimensions.x == 1280  &&  pDimensions.y == 720, "Context dimensions are 1280x720");

      Rml::RemoveContext ("test");
      Check (true, "RemoveContext completed (no crash)");
   }
}

// ---------------------------------------------------------------------------
// Test 3: Load an RML document from memory
// ---------------------------------------------------------------------------
static void TestLoadDocument ()
{
   std::printf ("\n[Test 3] Load an RML document from memory\n");

   Rml::Context* pContext = Rml::CreateContext ("doc_test", Rml::Vector2i (800, 600));
   Check (pContext != nullptr, "CreateContext for document test");

   if (pContext)
   {
      const char* szRml =
         "<rml>"
         "<head><title>Test</title>"
         "<style>body { width: 100%; height: 100%; } "
         "#hello { color: white; font-size: 16px; }</style>"
         "</head>"
         "<body>"
         "<div id='hello'>Hello from RmlUi</div>"
         "</body>"
         "</rml>";

      Rml::ElementDocument* pDocument = pContext->LoadDocumentFromMemory (szRml);
      Check (pDocument != nullptr, "LoadDocumentFromMemory returned a document");

      if (pDocument)
      {
         pDocument->Show ();

         Rml::String sTitle = pDocument->GetTitle ();
         Check (sTitle == "Test", "Document title is 'Test'");

         Rml::Element* pElement = pDocument->GetElementById ("hello");
         Check (pElement != nullptr, "GetElementById found #hello");

         if (pElement)
         {
            Rml::String sTag = pElement->GetTagName ();
            Check (sTag == "div", "Element tag is 'div'");

            Rml::String sId = pElement->GetId ();
            Check (sId == "hello", "Element id is 'hello'");
         }

         pDocument->Close ();
         Check (true, "Document closed (no crash)");
      }

      Rml::RemoveContext ("doc_test");
   }
}

// ---------------------------------------------------------------------------
// Test 4: Update cycle (simulate a frame)
// ---------------------------------------------------------------------------
static void TestUpdateCycle ()
{
   std::printf ("\n[Test 4] Update cycle (simulate a frame)\n");

   Rml::Context* pContext = Rml::CreateContext ("update_test", Rml::Vector2i (640, 480));
   Check (pContext != nullptr, "CreateContext for update test");

   if (pContext)
   {
      const char* szRml =
         "<rml><head><title>Frame</title></head>"
         "<body><div>Frame test</div></body></rml>";

      Rml::ElementDocument* pDocument = pContext->LoadDocumentFromMemory (szRml);
      if (pDocument)
         pDocument->Show ();

      bool bUpdateOk = pContext->Update ();
      Check (bUpdateOk, "Context.Update() succeeded");

      pContext->Render ();
      Check (true, "Context.Render() completed (no crash)");

      if (pDocument)
         pDocument->Close ();

      Rml::RemoveContext ("update_test");
   }
}

// ---------------------------------------------------------------------------
// Test 5: Multiple contexts
// ---------------------------------------------------------------------------
static void TestMultipleContexts ()
{
   std::printf ("\n[Test 5] Multiple contexts\n");

   Rml::Context* pCtxA = Rml::CreateContext ("ctx_a", Rml::Vector2i (100, 100));
   Rml::Context* pCtxB = Rml::CreateContext ("ctx_b", Rml::Vector2i (200, 200));

   Check (pCtxA != nullptr, "Context A created");
   Check (pCtxB != nullptr, "Context B created");
   Check (Rml::GetNumContexts () >= 2, "At least 2 contexts active");

   if (pCtxA  &&  pCtxB)
   {
      Check (pCtxA->GetName () != pCtxB->GetName (), "Context names are distinct");
   }

   Rml::RemoveContext ("ctx_a");
   Rml::RemoveContext ("ctx_b");
   Check (true, "Both contexts removed (no crash)");
}

// ---------------------------------------------------------------------------

int main (int /*argc*/, char* /*argv*/[])
{
   std::printf ("=== RmlUi Integration Test Suite ===\n");

   Rml::SetSystemInterface (&pStubSystem);
   Rml::SetRenderInterface (&pStubRender);
   Rml::SetFontEngineInterface (&pStubFontEngine);

   bool bInitOk = Rml::Initialise ();

   if (bInitOk)
   {
      TestInitialization ();
      TestContext ();
      TestLoadDocument ();
      TestUpdateCycle ();
      TestMultipleContexts ();

      Rml::Shutdown ();
   }
   else
   {
      std::fprintf (stderr, "Rml::Initialise failed\n");
      nFailed++;
   }

   std::printf ("\n=== Results: %d passed, %d failed ===\n", nPassed, nFailed);

   return (nFailed > 0) ? 1 : 0;
}
