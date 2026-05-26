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
#include "Stream.h"
#include <deque>

using namespace SNEEZE;

/***********************************************************************************************************************************
**  Impl Class
***********************************************************************************************************************************/

class CONSOLE::Impl
{
public:
   Impl (CONSOLE* pConsole, CONTEXT* pContext) :
      m_pConsole          (pConsole),
      m_pContext           (pContext),
      m_sPath_Temporary   ((std::filesystem::path (pContext->sPath_Temporary ()) / "Console").string ()),
      m_nNextIndex        (1),
      m_tpEpoch           (std::chrono::steady_clock::now ()),
      m_nEntriesPerBlock  (4096),
      m_nMaxBlocks        (5),
      m_nMaxRingEntries   (16384)
   {
   }

   bool Initialize ()
   {
      m_tpEpoch = std::chrono::steady_clock::now ();

      m_pContext->Engine ()->Log (IENGINE::kLOGLEVEL_Info, "CONSOLE", "Initialized");

      return true;
   }

   ~Impl ()
   {
      std::lock_guard<std::recursive_mutex> guard (m_mxConsole);

      for (auto& pair : m_umpStream)
         delete pair.second;

      m_umpStream.clear ();
      m_aRingBuffer.clear ();
      m_umpCID.clear ();
   }

   // ---------------------------------------------------------------------------
   // CID pool — copies each unique CID by value so ENTRY pointers remain
   // valid after the original container has been destroyed.
   // Symmetric with NETWORK::FILE and STORAGE::UNIT CID-by-value copies.
   // ---------------------------------------------------------------------------

   const CONTEXT::CONTAINER::CID* CID_Pool (const CONTEXT::CONTAINER::CID* pCID)
   {
      const CONTEXT::CONTAINER::CID* pResult = nullptr;

      if (pCID)
      {
         std::string sKey = StreamKey (pCID);
         auto it = m_umpCID.find (sKey);
         if (it == m_umpCID.end ())
         {
            m_umpCID[sKey] = *pCID;
            it = m_umpCID.find (sKey);
         }
         pResult = &it->second;
      }

      return pResult;
   }

   // ---------------------------------------------------------------------------
   // STREAM map — find-or-create, symmetric with NETWORK::Asset_Open and
   // STORAGE::Asset_Open.
   // ---------------------------------------------------------------------------

   CONSOLE::STREAM* Stream_Open (const CONTEXT::CONTAINER::CID* pPooledCID)
   {
      std::string sKey = StreamKey (pPooledCID);

      auto it = m_umpStream.find (sKey);
      if (it == m_umpStream.end ())
      {
         STREAM* pStream = new STREAM (m_pConsole, pPooledCID, m_sPath_Temporary);
         m_umpStream[sKey] = pStream;
         return pStream;
      }

      return it->second;
   }

   CONSOLE::STREAM* Stream_Find (const CONTEXT::CONTAINER::CID* pCID) const
   {
      STREAM* pResult = nullptr;

      std::string sKey = StreamKey (pCID);
      auto it = m_umpStream.find (sKey);
      if (it != m_umpStream.end ())
         pResult = it->second;

      return pResult;
   }

   // ---------------------------------------------------------------------------
   // Emit — core write path shared by all logging methods.
   //
   // Creates an ENTRY, delegates disk I/O to STREAM::Write, appends to the
   // global ring buffer, and fires the ICONTEXT notification.
   // ---------------------------------------------------------------------------

   void Emit (CONSOLE::eLEVEL eLevel, const CONTEXT::CONTAINER::CID* pCID, const std::string& sMessage, bool bCollapsed = false)
   {
      std::lock_guard<std::recursive_mutex> guard (m_mxConsole);

      const auto* pPooledCID = CID_Pool (pCID);
      STREAM* pStream = Stream_Open (pPooledCID);

      auto pEntry = std::make_shared<const CONSOLE::ENTRY> (
         eLevel, sMessage, SecondsSinceEpoch (), pPooledCID,
         m_nNextIndex++, pStream->GroupDepth (), bCollapsed);

      pStream->Write (pEntry);

      m_aRingBuffer.push_back (pEntry);
      while (m_aRingBuffer.size () > m_nMaxRingEntries)
         m_aRingBuffer.pop_front ();

      m_pContext->Host ()->OnConsoleEntryCreated (pEntry);
   }

   // ---------------------------------------------------------------------------
   // Grouping
   // ---------------------------------------------------------------------------

   void Group (const CONTEXT::CONTAINER::CID* pCID, const std::string& sLabel, bool bCollapsed)
   {
      std::lock_guard<std::recursive_mutex> guard (m_mxConsole);

      const auto* pPooledCID = CID_Pool (pCID);
      STREAM* pStream = Stream_Open (pPooledCID);

      Emit (CONSOLE::kLEVEL_LOG, pCID, sLabel, bCollapsed);

      pStream->Group ();
   }

   void GroupEnd (const CONTEXT::CONTAINER::CID* pCID)
   {
      std::lock_guard<std::recursive_mutex> guard (m_mxConsole);

      const auto* pPooledCID = CID_Pool (pCID);
      STREAM* pStream = Stream_Open (pPooledCID);
      pStream->GroupEnd ();
   }

   // ---------------------------------------------------------------------------
   // Counting
   // ---------------------------------------------------------------------------

   void Count (const CONTEXT::CONTAINER::CID* pCID, const std::string& sLabel)
   {
      std::lock_guard<std::recursive_mutex> guard (m_mxConsole);

      const auto* pPooledCID = CID_Pool (pCID);
      STREAM* pStream = Stream_Open (pPooledCID);

      uint32_t nCount = pStream->Count (sLabel);

      Emit (CONSOLE::kLEVEL_INFO, pCID, sLabel + ": " + std::to_string (nCount));
   }

   void CountReset (const CONTEXT::CONTAINER::CID* pCID, const std::string& sLabel)
   {
      std::lock_guard<std::recursive_mutex> guard (m_mxConsole);

      const auto* pPooledCID = CID_Pool (pCID);
      STREAM* pStream = Stream_Open (pPooledCID);
      pStream->CountReset (sLabel);
   }

   // ---------------------------------------------------------------------------
   // Timing
   // ---------------------------------------------------------------------------

   void Time (const CONTEXT::CONTAINER::CID* pCID, const std::string& sLabel)
   {
      std::lock_guard<std::recursive_mutex> guard (m_mxConsole);

      const auto* pPooledCID = CID_Pool (pCID);
      STREAM* pStream = Stream_Open (pPooledCID);
      pStream->Time (sLabel);
   }

   void TimeEnd (const CONTEXT::CONTAINER::CID* pCID, const std::string& sLabel)
   {
      std::lock_guard<std::recursive_mutex> guard (m_mxConsole);

      const auto* pPooledCID = CID_Pool (pCID);
      STREAM* pStream = Stream_Open (pPooledCID);

      double dElapsed = pStream->TimeEnd (sLabel);
      if (dElapsed >= 0.0)
      {
         std::ostringstream oss;
         oss << sLabel << ": " << std::fixed << std::setprecision (3) << dElapsed << "ms";
         Emit (CONSOLE::kLEVEL_INFO, pCID, oss.str ());
      }
   }

   void TimeLog (const CONTEXT::CONTAINER::CID* pCID, const std::string& sLabel)
   {
      std::lock_guard<std::recursive_mutex> guard (m_mxConsole);

      const auto* pPooledCID = CID_Pool (pCID);
      STREAM* pStream = Stream_Open (pPooledCID);

      double dElapsed = pStream->TimeLog (sLabel);
      if (dElapsed >= 0.0)
      {
         std::ostringstream oss;
         oss << sLabel << ": " << std::fixed << std::setprecision (3) << dElapsed << "ms";
         Emit (CONSOLE::kLEVEL_INFO, pCID, oss.str ());
      }
   }

   void TimeStamp (const CONTEXT::CONTAINER::CID* pCID, const std::string& sLabel)
   {
      std::ostringstream oss;
      oss << std::fixed << std::setprecision (3) << SecondsSinceEpoch () << "s";
      Emit (CONSOLE::kLEVEL_INFO, pCID, sLabel + ": " + oss.str ());
   }

   // ---------------------------------------------------------------------------
   // Clear
   // ---------------------------------------------------------------------------

   void Clear ()
   {
      std::lock_guard<std::recursive_mutex> guard (m_mxConsole);

      for (const auto& pEntry : m_aRingBuffer)
         m_pContext->Host ()->OnConsoleEntryDeleted (pEntry);

      m_aRingBuffer.clear ();
   }

   // ---------------------------------------------------------------------------
   // Enumeration
   // ---------------------------------------------------------------------------

   void Entry_Enum (CONSOLE::IENUM* pEnum)
   {
      if (pEnum)
      {
         std::lock_guard<std::recursive_mutex> guard (m_mxConsole);

         for (const auto& pEntry : m_aRingBuffer)
            pEnum->OnEntry (pEntry);
      }
   }

   void Entry_Enum (const CONTEXT::CONTAINER::CID* pCID, CONSOLE::IENUM* pEnum)
   {
      if (pEnum)
      {
         std::lock_guard<std::recursive_mutex> guard (m_mxConsole);

         const auto* pPooledCID = CID_Pool (pCID);
         STREAM* pStream = Stream_Find (pPooledCID);
         if (pStream && pStream->IsLoaded ())
         {
            for (const auto& pEntry : pStream->Entries ())
               pEnum->OnEntry (pEntry);
         }
      }
   }

   // ---------------------------------------------------------------------------
   // Channel management
   // ---------------------------------------------------------------------------

   void Channel_Load (const CONTEXT::CONTAINER::CID* pCID)
   {
      std::lock_guard<std::recursive_mutex> guard (m_mxConsole);

      const auto* pPooledCID = CID_Pool (pCID);
      STREAM* pStream = Stream_Find (pPooledCID);
      if (pStream)
         pStream->Load ();
   }

   void Channel_Unload (const CONTEXT::CONTAINER::CID* pCID)
   {
      std::lock_guard<std::recursive_mutex> guard (m_mxConsole);

      const auto* pPooledCID = CID_Pool (pCID);
      STREAM* pStream = Stream_Find (pPooledCID);
      if (pStream)
         pStream->Unload ();
   }

   // ---------------------------------------------------------------------------
   // Accessors
   // ---------------------------------------------------------------------------

   double SecondsSinceEpoch () const
   {
      auto tpNow = std::chrono::steady_clock::now ();
      return std::chrono::duration<double> (tpNow - m_tpEpoch).count ();
   }

   // ---------------------------------------------------------------------------
   // Helpers
   // ---------------------------------------------------------------------------

   static std::string StreamKey (const CONTEXT::CONTAINER::CID* pCID)
   {
      std::string sResult;

      if (pCID)
         sResult = pCID->sPersonaHash.substr (0, 12) + "/" + pCID->sFingerprint.substr (0, 2) + "/" + pCID->sFingerprint.substr (2, 22) + "/" + pCID->sContainerName;
      else
         sResult = "_engine";

      return sResult;
   }

   CONSOLE*                                                         m_pConsole;
   CONTEXT*                                                         m_pContext;
   std::string                                                      m_sPath_Temporary;

   std::recursive_mutex                                             m_mxConsole;

   std::unordered_map<std::string, CONTEXT::CONTAINER::CID>        m_umpCID;
   std::unordered_map<std::string, CONSOLE::STREAM*>               m_umpStream;
   std::deque<std::shared_ptr<const CONSOLE::ENTRY>>               m_aRingBuffer;

   uint32_t                                                         m_nNextIndex;
   std::chrono::steady_clock::time_point                            m_tpEpoch;

   uint32_t                                                         m_nEntriesPerBlock;
   uint32_t                                                         m_nMaxBlocks;
   uint32_t                                                         m_nMaxRingEntries;
};

/***********************************************************************************************************************************
**  CONSOLE
***********************************************************************************************************************************/

CONSOLE::CONSOLE (CONTEXT* pContext) :
   m_pImpl (new Impl (this, pContext))
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

SNEEZE::CONTEXT*   CONSOLE::Context         () const { return m_pImpl->m_pContext; }
const std::string& CONSOLE::sPath_Temporary () const { return m_pImpl->m_sPath_Temporary; }

// ---------------------------------------------------------------------------
// Methods
// ---------------------------------------------------------------------------

void     CONSOLE::Log             (const CONTEXT::CONTAINER::CID* pCID,                  const std::string& sMessage) {                  m_pImpl->Emit (kLEVEL_LOG,   pCID,                        sMessage); }
void     CONSOLE::Debug           (const CONTEXT::CONTAINER::CID* pCID,                  const std::string& sMessage) {                  m_pImpl->Emit (kLEVEL_DEBUG, pCID,                        sMessage); }
void     CONSOLE::Info            (const CONTEXT::CONTAINER::CID* pCID,                  const std::string& sMessage) {                  m_pImpl->Emit (kLEVEL_INFO,  pCID,                        sMessage); }
void     CONSOLE::Warn            (const CONTEXT::CONTAINER::CID* pCID,                  const std::string& sMessage) {                  m_pImpl->Emit (kLEVEL_WARN,  pCID,                        sMessage); }
void     CONSOLE::Error           (const CONTEXT::CONTAINER::CID* pCID,                  const std::string& sMessage) {                  m_pImpl->Emit (kLEVEL_ERROR, pCID,                        sMessage); }
void     CONSOLE::Assert          (const CONTEXT::CONTAINER::CID* pCID, bool bCondition, const std::string& sMessage) { if (!bCondition) m_pImpl->Emit (kLEVEL_ERROR, pCID, "Assertion failed: " + sMessage); }

void     CONSOLE::Group           (const CONTEXT::CONTAINER::CID* pCID,                  const std::string& sLabel)   {                  m_pImpl->Group               (pCID, sLabel, false); }
void     CONSOLE::GroupCollapsed  (const CONTEXT::CONTAINER::CID* pCID,                  const std::string& sLabel)   {                  m_pImpl->Group               (pCID, sLabel, true); }
void     CONSOLE::GroupEnd        (const CONTEXT::CONTAINER::CID* pCID)                                               {                  m_pImpl->GroupEnd            (pCID); }

void     CONSOLE::Count           (const CONTEXT::CONTAINER::CID* pCID,                  const std::string& sLabel)   {                  m_pImpl->Count               (pCID, sLabel); }
void     CONSOLE::CountReset      (const CONTEXT::CONTAINER::CID* pCID,                  const std::string& sLabel)   {                  m_pImpl->CountReset          (pCID, sLabel); }

void     CONSOLE::Time            (const CONTEXT::CONTAINER::CID* pCID,                  const std::string& sLabel)   {                  m_pImpl->Time                (pCID, sLabel); }
void     CONSOLE::TimeEnd         (const CONTEXT::CONTAINER::CID* pCID,                  const std::string& sLabel)   {                  m_pImpl->TimeEnd             (pCID, sLabel); }
void     CONSOLE::TimeLog         (const CONTEXT::CONTAINER::CID* pCID,                  const std::string& sLabel)   {                  m_pImpl->TimeLog             (pCID, sLabel); }
void     CONSOLE::TimeStamp       (const CONTEXT::CONTAINER::CID* pCID,                  const std::string& sLabel)   {                  m_pImpl->TimeStamp           (pCID, sLabel); }

void     CONSOLE::Clear           ()                                                                                  {                  m_pImpl->Clear               (); }

void     CONSOLE::Entry_Enum      (                                     IENUM* pEnum)                                 {                  m_pImpl->Entry_Enum          (      pEnum); }
void     CONSOLE::Entry_Enum      (const CONTEXT::CONTAINER::CID* pCID, IENUM* pEnum)                                 {                  m_pImpl->Entry_Enum          (pCID, pEnum); }

void     CONSOLE::Channel_Load    (const CONTEXT::CONTAINER::CID* pCID)                                               {                  m_pImpl->Channel_Load        (pCID); }
void     CONSOLE::Channel_Unload  (const CONTEXT::CONTAINER::CID* pCID)                                               {                  m_pImpl->Channel_Unload      (pCID); }

uint32_t CONSOLE::EntriesPerBlock () const       { return m_pImpl->m_nEntriesPerBlock; }
void     CONSOLE::EntriesPerBlock (uint32_t n)   {        m_pImpl->m_nEntriesPerBlock = n; }
uint32_t CONSOLE::MaxBlocks       () const       { return m_pImpl->m_nMaxBlocks; }
void     CONSOLE::MaxBlocks       (uint32_t n)   {        m_pImpl->m_nMaxBlocks = n; }
uint32_t CONSOLE::MaxRingEntries  () const       { return m_pImpl->m_nMaxRingEntries; }
void     CONSOLE::MaxRingEntries  (uint32_t n)   {        m_pImpl->m_nMaxRingEntries = n; }
