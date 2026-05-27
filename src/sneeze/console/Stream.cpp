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
#include <Console.h>
#include "Block.h"
#include <iomanip>

using namespace SNEEZE;

// ---------------------------------------------------------------------------
// CONSOLE::STREAM::Impl
// ---------------------------------------------------------------------------

class CONSOLE::STREAM::Impl
{
public:
   Impl (CONSOLE* pConsole, const CONTEXT::CONTAINER::CID* pCID) :
      m_pConsole         (pConsole),
      m_pCID             (pCID),
      m_bAttached        (false),
      m_nBlocks          (0),
      m_nEntries_Block   (0),
      m_nBlock           (-1),
      m_nBlockEntryCount (0),
      m_nGroupDepth      (0)
   {
   }

   void Initialize (int nBlocks, int nEntries_Block)
   {
      m_nBlocks        = nBlocks;
      m_nEntries_Block = nEntries_Block;

      Meta_Load ();
   }

   ~Impl ()
   {
      if (m_bAttached)
         Detach ();

      for (int nBlock = 0; nBlock < (int) m_apBlock.size (); nBlock++)
      {
         if (m_apBlock[nBlock])
         {
            m_pConsole->Block_Close (m_apBlock[nBlock]);
         }
      }

      m_apBlock.clear ();
   }

   // ---------------------------------------------------------------------------
   // Path helpers
   // ---------------------------------------------------------------------------

   std::string Path (uint32_t nBlock) const
   {
      const std::string& sBasePath = m_pConsole->Path_Temporary ();

      return (std::filesystem::path (sBasePath) / m_pCID->sPersonaHash / m_pCID->sFingerprint.substr (0, 2) / m_pCID->sFingerprint.substr (2, 22)).string ();
   }

   std::string Filename (uint32_t nBlock, const std::string& sExt = "") const
   {
      char szBlock[5];
      snprintf (szBlock, sizeof (szBlock), "%04u", nBlock);

      std::string sName = m_pCID->sContainerName + "-" + szBlock;

      if (!sExt.empty ())
         sName += "." + sExt;

      return sName;
   }

   std::string Pathname (uint32_t nBlock, const std::string& sExt = "") const
   {
      return (std::filesystem::path (Path (nBlock)) / Filename (nBlock, sExt)).string ();
   }

   std::string Pathname_Meta () const
   {
      return (std::filesystem::path (Path (0)) / (m_pCID->sContainerName + ".meta")).string ();
   }

   // ---------------------------------------------------------------------------
   // Meta sidecar — read on Initialize, written on Detach.
   // ---------------------------------------------------------------------------

   void Meta_Load ()
   {
      std::string sPathname_Meta = Pathname_Meta ();

      std::ifstream file (sPathname_Meta);
      if (file.is_open ())
      {
         try
         {
            nlohmann::json jMeta = nlohmann::json::parse (file);

            m_nBlock           = jMeta.value ("block", -1);
            m_nBlockEntryCount = jMeta.value ("blockEntryCount", 0);
         }
         catch (...)
         {
            m_nBlock           = -1;
            m_nBlockEntryCount = 0;
         }
      }

      if (m_nBlock >= 0)
      {
         int nFirstBlock = std::max (0, m_nBlock - m_nBlocks + 1);

         for (int nBlock = nFirstBlock; nBlock <= m_nBlock; nBlock++)
         {
            m_apBlock.push_back (m_pConsole->Block_Open (nBlock, Pathname (nBlock, "log")));
         }
      }
   }

   void Meta_Save ()
   {
      std::string sPathname_Meta = Pathname_Meta ();

      std::error_code ec;
      std::filesystem::create_directories (std::filesystem::path (sPathname_Meta).parent_path (), ec);

      nlohmann::json jMeta;

      jMeta["block"]           = m_nBlock;
      jMeta["blockEntryCount"] = m_nBlockEntryCount;

      jMeta["fingerprint"]     = m_pCID->sFingerprint;
      jMeta["organization"]    = m_pCID->sOrganization;
      jMeta["commonName"]      = m_pCID->sCommonName;
      jMeta["containerName"]   = m_pCID->sContainerName;
      jMeta["personaHash"]     = m_pCID->sPersonaHash;

      std::string sTmpPath = sPathname_Meta + ".temp";
      std::ofstream ofs (sTmpPath, std::ios::trunc);
      if (ofs.is_open ())
      {
         ofs << jMeta.dump (2);
         ofs.close ();

         std::filesystem::rename (sTmpPath, sPathname_Meta, ec);
      }
   }

   // ---------------------------------------------------------------------------
   // Attach / Detach
   // ---------------------------------------------------------------------------

   void Attach ()
   {
      std::lock_guard<std::recursive_mutex> guard (m_mxStream);

      if (!m_bAttached)
      {
         m_bAttached = true;

         for (int nBlock = 0; nBlock < (int) m_apBlock.size (); nBlock++)
            m_apBlock[nBlock]->Attach ();
      }
   }

   void Detach ()
   {
      std::lock_guard<std::recursive_mutex> guard (m_mxStream);

      if (m_bAttached)
      {
         for (int nBlock = 0; nBlock < (int) m_apBlock.size (); nBlock++)
            m_apBlock[nBlock]->Detach (m_pCID);

         Meta_Save ();

         m_bAttached = false;
      }
   }

   // ---------------------------------------------------------------------------
   // Rotate — create a new block, advance to it, delete the oldest if the
   // rolling window exceeds m_nBlocks.
   // ---------------------------------------------------------------------------

   void Rotate ()
   {
      m_nBlock++;
      m_nBlockEntryCount = 0;

      m_apBlock.push_back (m_pConsole->Block_Open (m_nBlock, Pathname (m_nBlock, "log")));

      if (m_bAttached)
         m_apBlock.back ()->Attach ();

      if ((int) m_apBlock.size () > m_nBlocks)
      {
         BLOCK* pOldBlock = m_apBlock.front ();

         if (m_bAttached)
            pOldBlock->Detach (m_pCID);

         m_pConsole->Block_Close (pOldBlock);
         m_apBlock.erase (m_apBlock.begin ());

         int nOldBlock = m_nBlock - m_nBlocks;
         std::error_code ec;
         std::filesystem::remove (Pathname (nOldBlock, "log"), ec);
      }
   }

   // ---------------------------------------------------------------------------
   // Entry — core write path. Calls back to CONSOLE for entry creation
   // (timestamp, sequence, ring buffer), then writes to the active block.
   // ---------------------------------------------------------------------------

   void Entry (CONSOLE::eLEVEL eLevel, const std::string& sMessage, bool bCollapsed = false)
   {
      std::lock_guard<std::recursive_mutex> guard (m_mxStream);

      if (m_nBlock < 0  ||  m_nBlockEntryCount >= m_nEntries_Block)
         Rotate ();

      auto pEntry = m_pConsole->Entry_Create (m_pCID, eLevel, sMessage, m_nGroupDepth, bCollapsed);

      m_apBlock.back ()->Write (pEntry);
      m_nBlockEntryCount++;
   }

   // ---------------------------------------------------------------------------
   // Grouping
   // ---------------------------------------------------------------------------

   void Group (const std::string& sLabel, bool bCollapsed)
   {
      std::lock_guard<std::recursive_mutex> guard (m_mxStream);

      Entry (CONSOLE::kLEVEL_LOG, sLabel, bCollapsed);
      m_nGroupDepth++;
   }

   void GroupEnd ()
   {
      std::lock_guard<std::recursive_mutex> guard (m_mxStream);

      if (m_nGroupDepth > 0)
         m_nGroupDepth--;
   }

   // ---------------------------------------------------------------------------
   // Counting
   // ---------------------------------------------------------------------------

   void Count (const std::string& sLabel)
   {
      std::lock_guard<std::recursive_mutex> guard (m_mxStream);

      uint32_t nCount = ++m_umpCount[sLabel];
      Entry (CONSOLE::kLEVEL_INFO, sLabel + ": " + std::to_string (nCount));
   }

   void CountReset (const std::string& sLabel)
   {
      std::lock_guard<std::recursive_mutex> guard (m_mxStream);

      m_umpCount.erase (sLabel);
   }

   // ---------------------------------------------------------------------------
   // Timing
   // ---------------------------------------------------------------------------

   void Time (const std::string& sLabel)
   {
      std::lock_guard<std::recursive_mutex> guard (m_mxStream);

      m_umpTime[sLabel] = std::chrono::steady_clock::now ();
   }

   void TimeEnd (const std::string& sLabel)
   {
      std::lock_guard<std::recursive_mutex> guard (m_mxStream);

      auto it = m_umpTime.find (sLabel);
      if (it != m_umpTime.end ())
      {
         auto tpNow = std::chrono::steady_clock::now ();
         double dElapsed = std::chrono::duration<double, std::milli> (tpNow - it->second).count ();
         m_umpTime.erase (it);

         std::ostringstream oss;
         oss << sLabel << ": " << std::fixed << std::setprecision (3) << dElapsed << "ms";
         Entry (CONSOLE::kLEVEL_INFO, oss.str ());
      }
   }

   void TimeLog (const std::string& sLabel)
   {
      std::lock_guard<std::recursive_mutex> guard (m_mxStream);

      auto it = m_umpTime.find (sLabel);
      if (it != m_umpTime.end ())
      {
         auto tpNow = std::chrono::steady_clock::now ();
         double dElapsed = std::chrono::duration<double, std::milli> (tpNow - it->second).count ();

         std::ostringstream oss;
         oss << sLabel << ": " << std::fixed << std::setprecision (3) << dElapsed << "ms";
         Entry (CONSOLE::kLEVEL_INFO, oss.str ());
      }
   }

public:
   CONSOLE*                                                               m_pConsole;
   const CONTEXT::CONTAINER::CID*                                         m_pCID;
   std::vector<BLOCK*>                                                    m_apBlock;
   std::recursive_mutex                                                   m_mxStream;
   bool                                                                   m_bAttached;

   int                                                                    m_nBlocks;
   int                                                                    m_nEntries_Block;
   int                                                                    m_nBlock;
   int                                                                    m_nBlockEntryCount;

   uint32_t                                                               m_nGroupDepth;

   std::unordered_map<std::string, uint32_t>                              m_umpCount;
   std::unordered_map<std::string, std::chrono::steady_clock::time_point> m_umpTime;
};

// ===========================================================================
// CONSOLE::STREAM
// ===========================================================================

CONSOLE::STREAM::STREAM (CONSOLE* pConsole, const CONTEXT::CONTAINER::CID* pCID) :
   m_pImpl (new Impl (pConsole, pCID))
{
}

void CONSOLE::STREAM::Initialize (int nBlocks, int nEntries_Block)
{
   m_pImpl->Initialize (nBlocks, nEntries_Block);
}

CONSOLE::STREAM::~STREAM ()
{
   delete m_pImpl;
}

// ---------------------------------------------------------------------------
// Accessors
// ---------------------------------------------------------------------------

std::string CONSOLE::STREAM::DisplayName    ()                                         const { return m_pImpl->m_pCID->DisplayName (); }

std::string CONSOLE::STREAM::Path          (uint32_t nBlock)                          const { return m_pImpl->Path     (nBlock); }
std::string CONSOLE::STREAM::Filename      (uint32_t nBlock, const std::string& sExt) const { return m_pImpl->Filename (nBlock, sExt); }
std::string CONSOLE::STREAM::Pathname      (uint32_t nBlock, const std::string& sExt) const { return m_pImpl->Pathname (nBlock, sExt); }

// ---------------------------------------------------------------------------
// Modifiers
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// STREAM Caching
// ---------------------------------------------------------------------------

void CONSOLE::STREAM::Attach              ()                                                 {                  m_pImpl->Attach (); }
void CONSOLE::STREAM::Detach              ()                                                 {                  m_pImpl->Detach (); }

// ---------------------------------------------------------------------------
// STREAM Pass-through
// ---------------------------------------------------------------------------

void     CONSOLE::STREAM::Log             (                 const std::string& sMessage)     {                  m_pImpl->Entry      (CONSOLE::kLEVEL_LOG,                          sMessage); }
void     CONSOLE::STREAM::Debug           (                 const std::string& sMessage)     {                  m_pImpl->Entry      (CONSOLE::kLEVEL_DEBUG,                        sMessage); }
void     CONSOLE::STREAM::Info            (                 const std::string& sMessage)     {                  m_pImpl->Entry      (CONSOLE::kLEVEL_INFO,                         sMessage); }
void     CONSOLE::STREAM::Warn            (                 const std::string& sMessage)     {                  m_pImpl->Entry      (CONSOLE::kLEVEL_WARN,                         sMessage); }
void     CONSOLE::STREAM::Error           (                 const std::string& sMessage)     {                  m_pImpl->Entry      (CONSOLE::kLEVEL_ERROR,                        sMessage); }
void     CONSOLE::STREAM::Assert          (bool bCondition, const std::string& sMessage)     { if (!bCondition) m_pImpl->Entry      (CONSOLE::kLEVEL_ERROR, "Assertion failed: " + sMessage); }

void     CONSOLE::STREAM::Group           (                 const std::string& sLabel)       {                  m_pImpl->Group      (sLabel, false); }
void     CONSOLE::STREAM::GroupCollapsed  (                 const std::string& sLabel)       {                  m_pImpl->Group      (sLabel, true);  }
void     CONSOLE::STREAM::GroupEnd        ()                                                 {                  m_pImpl->GroupEnd   ();              }

void     CONSOLE::STREAM::Count           (                 const std::string& sLabel)       {                  m_pImpl->Count      (sLabel); }
void     CONSOLE::STREAM::CountReset      (                 const std::string& sLabel)       {                  m_pImpl->CountReset (sLabel); }

void     CONSOLE::STREAM::Time            (                 const std::string& sLabel)       {                  m_pImpl->Time       (sLabel); }
void     CONSOLE::STREAM::TimeEnd         (                 const std::string& sLabel)       {                  m_pImpl->TimeEnd    (sLabel); }
void     CONSOLE::STREAM::TimeLog         (                 const std::string& sLabel)       {                  m_pImpl->TimeLog    (sLabel); }
