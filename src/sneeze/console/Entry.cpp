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
#include <ctime>
#include <cstdio>

using namespace SNEEZE;

// ===========================================================================
// CONSOLE::ENTRY
// ===========================================================================

CONSOLE::ENTRY::ENTRY (const CONTEXT::CONTAINER::CID* pCID, eLEVEL eLevel, const std::string& sMessage, uint32_t nIndex, uint32_t nGroupDepth, bool bCollapsed, const std::string& sStackTrace, const std::string& sSource) :
   m_eLevel      (eLevel),
   m_sMessage    (sMessage),
   m_tpStamp     (std::chrono::system_clock::now ()),
   m_pCID        (pCID),
   m_nIndex      (nIndex),
   m_nGroupDepth (nGroupDepth),
   m_bCollapsed  (bCollapsed),
   m_sStackTrace (sStackTrace),
   m_sSource     (sSource)
{
}

// ---------------------------------------------------------------------------
// Const accessors
// ---------------------------------------------------------------------------

CONSOLE::eLEVEL                              CONSOLE::ENTRY::Level       () const { return m_eLevel; }
const std::string&                           CONSOLE::ENTRY::Message     () const { return m_sMessage; }
std::chrono::system_clock::time_point        CONSOLE::ENTRY::tpStamp     () const { return m_tpStamp; }
const SNEEZE::CONTEXT::CONTAINER::CID*       CONSOLE::ENTRY::CID         () const { return m_pCID; }
uint32_t                                     CONSOLE::ENTRY::Index       () const { return m_nIndex; }
uint32_t                                     CONSOLE::ENTRY::GroupDepth  () const { return m_nGroupDepth; }
bool                                         CONSOLE::ENTRY::IsCollapsed () const { return m_bCollapsed; }
const std::string&                           CONSOLE::ENTRY::StackTrace  () const { return m_sStackTrace; }
const std::string&                           CONSOLE::ENTRY::Source      () const { return m_sSource; }

// ---------------------------------------------------------------------------
// LevelString
// ---------------------------------------------------------------------------

const char* CONSOLE::ENTRY::LevelString (eLEVEL eLevel)
{
   const char* szResult = "log";

   switch (eLevel)
   {
      case kLEVEL_LOG:   szResult = "log";   break;
      case kLEVEL_DEBUG: szResult = "debug"; break;
      case kLEVEL_INFO:  szResult = "info";  break;
      case kLEVEL_WARN:  szResult = "warn";  break;
      case kLEVEL_ERROR: szResult = "error"; break;
   }

   return szResult;
}

// ---------------------------------------------------------------------------
// FormatStamp
// ---------------------------------------------------------------------------

std::string CONSOLE::ENTRY::FormatStamp () const
{
   auto tmTime = std::chrono::system_clock::to_time_t (m_tpStamp);
   auto nMillis = std::chrono::duration_cast<std::chrono::milliseconds> (m_tpStamp.time_since_epoch ()).count () % 1000;

   struct tm tmLocal;
#ifdef _WIN32
   localtime_s (&tmLocal, &tmTime);
#else
   localtime_r (&tmTime, &tmLocal);
#endif

   char szBuf[32];
   snprintf (szBuf, sizeof (szBuf), "%02d:%02d:%02d.%03d",
      tmLocal.tm_hour, tmLocal.tm_min, tmLocal.tm_sec, static_cast<int> (nMillis));

   return szBuf;
}

// ---------------------------------------------------------------------------
// MessageParts — parse the message into components.
//
// The message may be a JSON array of mixed types (strings, objects):
//   ["hello", { "a": 5 }, "world"]
//
// If the message is not a JSON array, the entire string is returned as a
// single element. JSON parsing errors return the raw message.
// ---------------------------------------------------------------------------

std::vector<std::string> CONSOLE::ENTRY::MessageParts () const
{
   std::vector<std::string> aParts;

   if (!m_sMessage.empty () && m_sMessage.front () == '[')
   {
      try
      {
         nlohmann::json jArray = nlohmann::json::parse (m_sMessage);
         if (jArray.is_array ())
         {
            for (const auto& jItem : jArray)
            {
               if (jItem.is_string ())
                  aParts.push_back (jItem.get<std::string> ());
               else
                  aParts.push_back (jItem.dump ());
            }
         }
         else aParts.push_back (m_sMessage);
      }
      catch (...)
      {
         aParts.push_back (m_sMessage);
      }
   }
   else aParts.push_back (m_sMessage);

   return aParts;
}

// ---------------------------------------------------------------------------
// ToJson — serialize to a JSON object for JSONL disk storage.
// ---------------------------------------------------------------------------

nlohmann::json CONSOLE::ENTRY::ToJson () const
{
   nlohmann::json jEntry;

   jEntry["level"]      = LevelString (m_eLevel);
   jEntry["message"]    = m_sMessage;
   jEntry["stamp"]      = std::chrono::duration<double> (m_tpStamp.time_since_epoch ()).count ();
   jEntry["index"]      = m_nIndex;
   jEntry["groupDepth"] = m_nGroupDepth;
   jEntry["collapsed"]  = m_bCollapsed;

   if (m_pCID)
      jEntry["container"] = m_pCID->DisplayName ();
   else
      jEntry["container"] = "_engine";

   if (!m_sStackTrace.empty ())
      jEntry["stackTrace"] = m_sStackTrace;

   if (!m_sSource.empty ())
      jEntry["source"] = m_sSource;

   return jEntry;
}

// ---------------------------------------------------------------------------
// FromJson — deserialize from a JSONL line. The CID pointer is provided by
// the STREAM that owns the block file (ENTRY does not resolve CIDs itself).
// ---------------------------------------------------------------------------

std::shared_ptr<const CONSOLE::ENTRY> CONSOLE::ENTRY::FromJson (const nlohmann::json& jEntry, const CONTEXT::CONTAINER::CID* pCID)
{
   eLEVEL eLevel = kLEVEL_LOG;
   std::string sLevelStr = jEntry.value ("level", "log");
   if      (sLevelStr == "debug") eLevel = kLEVEL_DEBUG;
   else if (sLevelStr == "info")  eLevel = kLEVEL_INFO;
   else if (sLevelStr == "warn")  eLevel = kLEVEL_WARN;
   else if (sLevelStr == "error") eLevel = kLEVEL_ERROR;

   auto pEntry = std::make_shared<ENTRY>
   (
      pCID,
      eLevel,
      jEntry.value ("message",    std::string ()),
      jEntry.value ("index",      static_cast<uint32_t> (0)),
      jEntry.value ("groupDepth", static_cast<uint32_t> (0)),
      jEntry.value ("collapsed",  false),
      jEntry.value ("stackTrace", std::string ()),
      jEntry.value ("source",     std::string ())
   );

   double dEpochSeconds = jEntry.value ("stamp", 0.0);
   if (dEpochSeconds > 0.0)
      pEntry->m_tpStamp = std::chrono::system_clock::time_point (std::chrono::duration_cast<std::chrono::system_clock::duration> (std::chrono::duration<double> (dEpochSeconds)));

   return pEntry;
}
