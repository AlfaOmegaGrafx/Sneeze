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

#include "Console.h"
#include <deque>

using namespace SNEEZE;

/***********************************************************************************************************************************
**  Impl Class
***********************************************************************************************************************************/

ICONSOLE_IMPL::ICONSOLE_IMPL () {}
ICONSOLE_IMPL::~ICONSOLE_IMPL () {}

class CONSOLE::Impl : public ICONSOLE_IMPL
{
public:
   Impl (CONTEXT* pContext) :
      m_pContext          (pContext),
      m_sPath_Temporary   ((std::filesystem::path (pContext->Path_Temporary ()) / "Console").string ()),
      m_nIndex_Entry      (0),
      m_nEntries_Cache    (16384),
      m_nEntries_Block    (4096),
      m_nBlocks           (4)
   {
   }

   bool Initialize ()
   {
      m_pContext->Engine ()->Log (IENGINE::kLOGLEVEL_Info, "CONSOLE", "Initialized");

      return true;
   }

   ~Impl ()
   {
      std::lock_guard<std::recursive_mutex> guard (m_mxConsole);

      while (!m_umpStream.empty ())
         Stream_Close (m_umpStream.begin ()->second);

      m_apEntry.clear ();
   }

   // ---------------------------------------------------------------------------
   // Stream management
   // ---------------------------------------------------------------------------

   CONSOLE::STREAM* Stream_Open (const CONTAINER::CID* pCID)
   {
      STREAM* pStream = nullptr;

      if (pCID)
      {
         std::lock_guard<std::recursive_mutex> guard (m_mxConsole);

         if (m_umpStream.find (pCID) == m_umpStream.end ())
         {
            pStream = new STREAM (this, pCID);

            m_umpStream[pCID] = pStream;

            pStream->Initialize (m_nBlocks, m_nEntries_Block);

            m_pContext->Host ()->OnConsoleStreamCreated (pStream);
         }
      }

      return pStream;
   }

   void Stream_Close (STREAM* pStream)
   {
      if (pStream)
      {
         std::lock_guard<std::recursive_mutex> guard (m_mxConsole);

         m_pContext->Host ()->OnConsoleStreamDeleted (pStream);

         for (auto it = m_umpStream.begin (); it != m_umpStream.end (); ++it)
         {
            if (it->second == pStream)
            {
               m_umpStream.erase (it);
               break;
            }
         }

         delete pStream;
      }
   }

   void Stream_Enum (CONSOLE::IENUM_STREAM* pEnum)
   {
      if (pEnum)
      {
         std::lock_guard<std::recursive_mutex> guard (m_mxConsole);

         for (auto& pair : m_umpStream)
            pEnum->OnStream (pair.second);
      }
   }

   // ---------------------------------------------------------------------------
   // Clear
   // ---------------------------------------------------------------------------

   void Clear ()
   {
      std::lock_guard<std::recursive_mutex> guard (m_mxConsole);

      for (const auto& pEntry : m_apEntry)
         m_pContext->Host ()->OnConsoleEntryDeleted (pEntry);

      m_apEntry.clear ();
   }

   // ---------------------------------------------------------------------------
   // Enumeration
   // ---------------------------------------------------------------------------

   void Entry_Enum (CONSOLE::IENUM_ENTRY* pEnum)
   {
      if (pEnum)
      {
         std::lock_guard<std::recursive_mutex> guard (m_mxConsole);

         for (const auto& pEntry : m_apEntry)
            pEnum->OnEntry (pEntry);
      }
   }

   // ---------------------------------------------------------------------------
   // ICONSOLE_IMPL
   // ---------------------------------------------------------------------------

   const std::string& Path_Temporary () const override
   {
      return m_sPath_Temporary;
   }

   // ---------------------------------------------------------------------------
   // Entry creation — called by STREAM to create, timestamp, sequence,
   // and ring-buffer an entry. Returns the immutable shared_ptr.
   // ---------------------------------------------------------------------------

   std::shared_ptr<const CONSOLE::ENTRY> Entry_Find (uint32_t nIndex) override
   {
      std::lock_guard<std::recursive_mutex> guard (m_mxConsole);

      std::shared_ptr<const CONSOLE::ENTRY> pEntry;

      if (!m_apEntry.empty ())
      {
         uint32_t nFirst = m_apEntry.front ()->Index ();
         uint32_t nLast  = m_apEntry.back  ()->Index ();

         if (nIndex >= nFirst  &&  nIndex <= nLast)
            pEntry = m_apEntry[nIndex - nFirst];
      }

      return pEntry;
   }

   std::shared_ptr<const CONSOLE::ENTRY> Entry_Create (const CONTAINER::CID* pCID, CONSOLE::eLEVEL eLevel, const std::string& sMessage, uint32_t nGroupDepth, bool bCollapsed) override
   {
      std::lock_guard<std::recursive_mutex> guard (m_mxConsole);

      auto pEntry = std::make_shared<const CONSOLE::ENTRY> (pCID, eLevel, sMessage, m_nIndex_Entry++, nGroupDepth, bCollapsed);
      
      m_apEntry.push_back (pEntry);

      while (m_apEntry.size () > m_nEntries_Cache)
      {
         m_pContext->Host ()->OnConsoleEntryDeleted (m_apEntry.front ());
         m_apEntry.pop_front ();
      }

      m_pContext->Host ()->OnConsoleEntryCreated (pEntry);

      return pEntry;
   }

   CONTEXT*                                                         m_pContext;
   std::string                                                      m_sPath_Temporary;

   uint32_t                                                         m_nEntries_Cache;
   uint32_t                                                         m_nEntries_Block;
   uint32_t                                                         m_nBlocks;

   std::recursive_mutex                                             m_mxConsole;
   std::deque<std::shared_ptr<const CONSOLE::ENTRY>>                m_apEntry;
   uint32_t                                                         m_nIndex_Entry;

   std::unordered_map<const CONTAINER::CID*, CONSOLE::STREAM*>      m_umpStream;
};

/***********************************************************************************************************************************
**  CONSOLE
***********************************************************************************************************************************/

CONSOLE::CONSOLE (CONTEXT* pContext) :
   m_pImpl (new Impl (pContext))
{
}

bool CONSOLE::Initialize ()
{
   return m_pImpl->Initialize ();
}

CONSOLE::~CONSOLE ()
{
   delete m_pImpl;
}

// ---------------------------------------------------------------------------
// Accessors
// ---------------------------------------------------------------------------

SNEEZE::CONTEXT*   CONSOLE::Context         () const     { return m_pImpl->m_pContext; }

uint32_t           CONSOLE::Entries_Cache   () const     { return m_pImpl->m_nEntries_Cache; }
void               CONSOLE::Entries_Cache   (uint32_t n) {        m_pImpl->m_nEntries_Cache = n; }
uint32_t           CONSOLE::Entries_Block   () const     { return m_pImpl->m_nEntries_Block; }
void               CONSOLE::Entries_Block   (uint32_t n) {        m_pImpl->m_nEntries_Block = n; }
uint32_t           CONSOLE::Blocks          () const     { return m_pImpl->m_nBlocks; }
void               CONSOLE::Blocks          (uint32_t n) {        m_pImpl->m_nBlocks = n; }

// ---------------------------------------------------------------------------
// Methods
// ---------------------------------------------------------------------------

CONSOLE::STREAM*   CONSOLE::Stream_Open       (const CONTAINER::CID* pCID)           { return m_pImpl->Stream_Open       (pCID); }
void               CONSOLE::Stream_Close      (STREAM* pStream)                               {        m_pImpl->Stream_Close      (pStream); }
void               CONSOLE::Stream_Enum       (IENUM_STREAM* pEnum)                           {        m_pImpl->Stream_Enum       (pEnum); }

void               CONSOLE::Clear             ()                                              {        m_pImpl->Clear             (); }

void               CONSOLE::Entry_Enum        (IENUM_ENTRY* pEnum)                            {        m_pImpl->Entry_Enum        (pEnum); }
