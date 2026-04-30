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
#include <openssl/sha.h>
#include <cstdio>
#include <cstring>

namespace sneeze { namespace persona {

PERSONA::PERSONA ()
   : m_bLoggedIn (false)
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

   std::fprintf (stdout, "PERSONA: Logged in as \"%s\" (hash: %s)\n",
      m_sName.c_str (), m_sHash.c_str ());
}

void PERSONA::Logout ()
{
   std::fprintf (stdout, "PERSONA: Logged out \"%s\"\n", m_sName.c_str ());
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

}} // namespace sneeze::persona
