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

   STREAM* Stream_Open (CONTAINER* pContainer)
   {
      STREAM* pStream = nullptr;

      if (pContainer)
      {
         std::lock_guard<std::recursive_mutex> guard (m_mxConsole);

         if (m_umpStream.find (pContainer) == m_umpStream.end ())
         {
            pStream = new STREAM (this, pContainer);

            m_umpStream[pContainer] = pStream;

            pStream->Initialize (m_nBlocks, m_nEntries_Block);
         }
      }

      return pStream;
   }

   void Stream_Close (STREAM* pStream)
   {
      if (pStream)
      {
         std::lock_guard<std::recursive_mutex> guard (m_mxConsole);

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

   void Stream_Enum (IENUM_STREAM* pEnum)
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

   void Entry_Enum (IENUM_ENTRY* pEnum)
   {
      if (pEnum)
      {
         std::lock_guard<std::recursive_mutex> guard (m_mxConsole);

         for (const auto& pEntry : m_apEntry)
            pEnum->OnEntry (pEntry);
      }
   }

   // ---------------------------------------------------------------------------
   // Entry creation — called by STREAM to create, timestamp, sequence,
   // and ring-buffer an entry. Returns the immutable shared_ptr.
   // ---------------------------------------------------------------------------

   std::shared_ptr<const ENTRY> Entry_Find (uint32_t nIndex) override
   {
      std::lock_guard<std::recursive_mutex> guard (m_mxConsole);

      std::shared_ptr<const ENTRY> pEntry;

      if (!m_apEntry.empty ())
      {
         uint32_t nFirst = m_apEntry.front ()->Index ();
         uint32_t nLast  = m_apEntry.back  ()->Index ();

         if (nIndex >= nFirst  &&  nIndex <= nLast)
            pEntry = m_apEntry[nIndex - nFirst];
      }

      return pEntry;
   }

   std::shared_ptr<const ENTRY> Entry_Create (CONTAINER* pContainer, eENTRY_LEVEL eLevel, const std::string& sMessage, uint32_t nGroupDepth, bool bCollapsed, bool bSystem) override
   {
      std::lock_guard<std::recursive_mutex> guard (m_mxConsole);

      auto pEntry = std::make_shared<const ENTRY> (pContainer, eLevel, sMessage, m_nIndex_Entry++, nGroupDepth, bCollapsed, bSystem);
      
      m_apEntry.push_back (pEntry);

      while (m_apEntry.size () > m_nEntries_Cache)
      {
         m_pContext->Host ()->OnConsoleEntryDeleted (m_apEntry.front ());
         m_apEntry.pop_front ();
      }

      m_pContext->Host ()->OnConsoleEntryCreated (pEntry);

      return pEntry;
   }

   CONTEXT*                                 m_pContext;

   uint32_t                                 m_nEntries_Cache;
   uint32_t                                 m_nEntries_Block;
   uint32_t                                 m_nBlocks;

   std::recursive_mutex                     m_mxConsole;
   std::deque<std::shared_ptr<const ENTRY>> m_apEntry;
   uint32_t                                 m_nIndex_Entry;

   std::unordered_map<CONTAINER*, STREAM*>  m_umpStream;
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

SNEEZE::CONTEXT* CONSOLE::Context       () const                { return m_pImpl->m_pContext; }

uint32_t         CONSOLE::Entries_Cache () const                { return m_pImpl->m_nEntries_Cache; }
uint32_t         CONSOLE::Entries_Block () const                { return m_pImpl->m_nEntries_Block; }
uint32_t         CONSOLE::Blocks        () const                { return m_pImpl->m_nBlocks; }

// ---------------------------------------------------------------------------
//Mutators
// ---------------------------------------------------------------------------

void             CONSOLE::Entries_Cache (uint32_t n)            {        m_pImpl->m_nEntries_Cache = n; }
void             CONSOLE::Entries_Block (uint32_t n)            {        m_pImpl->m_nEntries_Block = n; }
void             CONSOLE::Blocks        (uint32_t n)            {        m_pImpl->m_nBlocks        = n; }

// ---------------------------------------------------------------------------
// Methods
// ---------------------------------------------------------------------------

STREAM*          CONSOLE::Stream_Open   (CONTAINER* pContainer) { return m_pImpl->Stream_Open       (pContainer); }
void             CONSOLE::Stream_Close  (STREAM* pStream)       {        m_pImpl->Stream_Close      (pStream); }
void             CONSOLE::Stream_Enum   (IENUM_STREAM* pEnum)   {        m_pImpl->Stream_Enum       (pEnum); }

void             CONSOLE::Clear         ()                      {        m_pImpl->Clear             (); }

void             CONSOLE::Entry_Enum    (IENUM_ENTRY* pEnum)    {        m_pImpl->Entry_Enum        (pEnum); }
