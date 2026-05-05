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

#include "Persona.h"
#include "core/Sneeze.h"
#include <openssl/sha.h>
#include <cstring>

namespace persona {

PERSONA::PERSONA (SNEEZE* pSneeze)
   : m_pSneeze (pSneeze)
   , m_bLoggedIn (false)
{
}

void PERSONA::Login (const std::string& sFirst, const std::string& sSecond)
{
   if (sSecond.empty ())
      m_sName = sFirst;
   else
      m_sName = sFirst + "." + sSecond;

   m_sHash = ComputeHash (m_sName);
   m_bLoggedIn = true;

   m_pSneeze->Log (SNEEZE::ISNEEZE::kLOGLEVEL_Info, "PERSONA",
      "Logged in as \"" + m_sName + "\" (hash: " + m_sHash + ")");
}

void PERSONA::Logout ()
{
   m_pSneeze->Log (SNEEZE::ISNEEZE::kLOGLEVEL_Info, "PERSONA",
      "Logged out \"" + m_sName + "\"");
   m_bLoggedIn = false;
   m_sName.clear ();
   m_sHash.clear ();
}

std::string PERSONA::ComputeHash (const std::string& sInput)
{
   unsigned char aDigest[SHA256_DIGEST_LENGTH];
   SHA256 (reinterpret_cast<const unsigned char*> (sInput.data ()),
      sInput.size (), aDigest);

   char szHex[SHA256_DIGEST_LENGTH * 2 + 1];
   for (int i = 0; i < SHA256_DIGEST_LENGTH; i++)
      std::sprintf (szHex + i * 2, "%02x", aDigest[i]);
   szHex[SHA256_DIGEST_LENGTH * 2] = '\0';

   return std::string (szHex);
}

} // namespace persona
