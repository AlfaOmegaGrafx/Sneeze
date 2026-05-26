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
#include <iomanip>

using namespace SNEEZE;

// ===========================================================================
// CONSOLE::STREAM
// ===========================================================================

CONSOLE::STREAM::STREAM (CONSOLE* pConsole, const CONTEXT::CONTAINER::CID* pCID, const std::string& sBasePath) :
   m_pConsole         (pConsole),
   m_pCID             (pCID),
   m_nBlock           (0),
   m_nBlockEntryCount (0),
   m_nGroupDepth      (0),
   m_bLoaded          (false)
{
   if (pCID)
   {
      std::string sPersona = pCID->sPersonaHash.substr (0, 12);
      std::string sFp2     = pCID->sFingerprint.substr (0, 2);
      std::string sFp22    = pCID->sFingerprint.substr (2, 22);

      m_sPath   = (std::filesystem::path (sBasePath) / sPersona / sFp2 / sFp22).string ();
      m_sPrefix = pCID->sContainerName + "-";
   }
   else
   {
      m_sPath   = (std::filesystem::path (sBasePath) / "_engine").string ();
      m_sPrefix = "";
   }
}

CONSOLE::STREAM::~STREAM ()
{
   Close ();
}

// ---------------------------------------------------------------------------
// Write — append one JSONL line to the active block file.
//
// Creates the directory and opens the file on first write. Adds the entry to
// the loaded cache if the channel is currently loaded. Rotates to a new block
// when the current block reaches EntriesPerBlock().
// ---------------------------------------------------------------------------

void CONSOLE::STREAM::Write (std::shared_ptr<const CONSOLE::ENTRY> pEntry)
{
   if (!m_ofsBlock.is_open ())
   {
      std::error_code ec;
      std::filesystem::create_directories (m_sPath, ec);
      m_ofsBlock.open (Pathname (m_nBlock), std::ios::app);
   }

   if (m_ofsBlock.is_open ())
   {
      m_ofsBlock << pEntry->ToJson ().dump () << "\n";
      m_nBlockEntryCount++;

      if (m_bLoaded)
         m_aEntry.push_back (pEntry);

      if (m_nBlockEntryCount >= m_pConsole->EntriesPerBlock ())
         Rotate ();
   }
}

// ---------------------------------------------------------------------------
// Load — read all existing block files into the in-memory entry cache.
// Called when the inspector drills into a specific container.
// ---------------------------------------------------------------------------

void CONSOLE::STREAM::Load ()
{
   if (!m_bLoaded)
   {
      m_aEntry.clear ();

      uint32_t nMaxBlocks = m_pConsole->MaxBlocks ();
      uint32_t nFirstBlock = (m_nBlock >= nMaxBlocks) ? (m_nBlock - nMaxBlocks + 1) : 0;

      for (uint32_t nBlock = nFirstBlock; nBlock <= m_nBlock; ++nBlock)
      {
         std::ifstream ifs (Pathname (nBlock));
         if (ifs.is_open ())
         {
            std::string sLine;
            while (std::getline (ifs, sLine))
            {
               if (!sLine.empty ())
               {
                  try
                  {
                     nlohmann::json jEntry = nlohmann::json::parse (sLine);
                     auto pEntry = CONSOLE::ENTRY::FromJson (jEntry, m_pCID);
                     if (pEntry)
                        m_aEntry.push_back (pEntry);
                  }
                  catch (...)
                  {
                  }
               }
            }
         }
      }

      m_bLoaded = true;
   }
}

// ---------------------------------------------------------------------------
// Unload — evict the in-memory entry cache.
// ---------------------------------------------------------------------------

void CONSOLE::STREAM::Unload ()
{
   m_aEntry.clear ();
   m_bLoaded = false;
}

// ---------------------------------------------------------------------------
// Close — flush and close the active block file.
// ---------------------------------------------------------------------------

void CONSOLE::STREAM::Close ()
{
   if (m_ofsBlock.is_open ())
      m_ofsBlock.close ();
}

// ---------------------------------------------------------------------------
// Rotate — close the current block, advance to the next, delete the oldest
// block if the rolling window exceeds MaxBlocks().
// ---------------------------------------------------------------------------

void CONSOLE::STREAM::Rotate ()
{
   Close ();

   m_nBlock++;
   m_nBlockEntryCount = 0;

   uint32_t nMaxBlocks = m_pConsole->MaxBlocks ();
   if (m_nBlock >= nMaxBlocks)
   {
      uint32_t nOldBlock = m_nBlock - nMaxBlocks;
      std::error_code ec;
      std::filesystem::remove (Pathname (nOldBlock), ec);
   }
}

// ---------------------------------------------------------------------------
// Grouping
// ---------------------------------------------------------------------------

uint32_t CONSOLE::STREAM::GroupDepth () const { return m_nGroupDepth; }

void CONSOLE::STREAM::Group ()
{
   m_nGroupDepth++;
}

void CONSOLE::STREAM::GroupEnd ()
{
   if (m_nGroupDepth > 0)
      m_nGroupDepth--;
}

// ---------------------------------------------------------------------------
// Counting
// ---------------------------------------------------------------------------

uint32_t CONSOLE::STREAM::Count (const std::string& sLabel)
{
   return ++m_umpCount[sLabel];
}

void CONSOLE::STREAM::CountReset (const std::string& sLabel)
{
   m_umpCount.erase (sLabel);
}

// ---------------------------------------------------------------------------
// Timing
// ---------------------------------------------------------------------------

void CONSOLE::STREAM::Time (const std::string& sLabel)
{
   m_umpTime[sLabel] = std::chrono::steady_clock::now ();
}

double CONSOLE::STREAM::TimeEnd (const std::string& sLabel)
{
   double dResult = -1.0;

   auto it = m_umpTime.find (sLabel);
   if (it != m_umpTime.end ())
   {
      auto tpNow = std::chrono::steady_clock::now ();
      dResult = std::chrono::duration<double, std::milli> (tpNow - it->second).count ();
      m_umpTime.erase (it);
   }

   return dResult;
}

double CONSOLE::STREAM::TimeLog (const std::string& sLabel) const
{
   double dResult = -1.0;

   auto it = m_umpTime.find (sLabel);
   if (it != m_umpTime.end ())
   {
      auto tpNow = std::chrono::steady_clock::now ();
      dResult = std::chrono::duration<double, std::milli> (tpNow - it->second).count ();
   }

   return dResult;
}

// ---------------------------------------------------------------------------
// Accessors
// ---------------------------------------------------------------------------

bool CONSOLE::STREAM::IsLoaded () const { return m_bLoaded; }

const std::deque<std::shared_ptr<const CONSOLE::ENTRY>>& CONSOLE::STREAM::Entries () const { return m_aEntry; }

// ---------------------------------------------------------------------------
// Path helpers
// ---------------------------------------------------------------------------

std::string CONSOLE::STREAM::Filename (uint32_t nBlock) const
{
   std::ostringstream oss;
   oss << m_sPrefix << std::setfill ('0') << std::setw (4) << nBlock << ".log";

   return oss.str ();
}

std::string CONSOLE::STREAM::Pathname (uint32_t nBlock) const
{
   return (std::filesystem::path (m_sPath) / Filename (nBlock)).string ();
}
