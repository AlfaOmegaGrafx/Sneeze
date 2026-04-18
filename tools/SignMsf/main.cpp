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

// SignMsf — JWS signing CLI tool for .msf/.mss files.
//
//   SignMsf --payload <json> --key <key.pem> --cert <cert.pem>
//           [--chain <intermediate.pem>] [--alg RS256] --out <output.mss>

#include "../../src/jws/JwsBase.h"

#include <cstdio>
#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

static std::string ReadFile (const char* sPath)
{
   std::ifstream ifs (sPath, std::ios::binary);
   std::string sResult;
   if (ifs.is_open ())
   {
      std::ostringstream oss;
      oss << ifs.rdbuf ();
      sResult = oss.str ();
   }
   return sResult;
}

static bool WriteFile (const char* sPath, const std::string& sData)
{
   std::ofstream ofs (sPath, std::ios::binary);
   bool bOk = false;
   if (ofs.is_open ())
   {
      ofs.write (sData.data (), (std::streamsize) sData.size ());
      bOk = ofs.good ();
   }
   return bOk;
}

static void PrintUsage ()
{
   std::cerr
      << "Usage: SignMsf --payload <json-file> --key <private-key.pem>\n"
      << "               --cert <leaf-cert.pem> [--chain <intermediate.pem>]\n"
      << "               [--alg RS256] --out <output.mss>\n";
}

int main (int nArgc, char** aArgv)
{
   const char* sPayloadPath = nullptr;
   const char* sKeyPath     = nullptr;
   const char* sOutPath     = nullptr;
   std::string sAlgorithm   = "RS256";
   std::vector<const char*> aCertPaths;

   for (int i = 1; i < nArgc; ++i)
   {
      if (strcmp (aArgv[i], "--payload") == 0  &&  i + 1 < nArgc)
         sPayloadPath = aArgv[++i];
      else if (strcmp (aArgv[i], "--key") == 0  &&  i + 1 < nArgc)
         sKeyPath = aArgv[++i];
      else if (strcmp (aArgv[i], "--cert") == 0  &&  i + 1 < nArgc)
         aCertPaths.push_back (aArgv[++i]);
      else if (strcmp (aArgv[i], "--chain") == 0  &&  i + 1 < nArgc)
         aCertPaths.push_back (aArgv[++i]);
      else if (strcmp (aArgv[i], "--alg") == 0  &&  i + 1 < nArgc)
         sAlgorithm = aArgv[++i];
      else if (strcmp (aArgv[i], "--out") == 0  &&  i + 1 < nArgc)
         sOutPath = aArgv[++i];
      else
      {
         std::cerr << "Unknown argument: " << aArgv[i] << "\n";
         PrintUsage ();
         return 1;
      }
   }

   if (!sPayloadPath  ||  !sKeyPath  ||  aCertPaths.empty ()  ||  !sOutPath)
   {
      PrintUsage ();
      return 1;
   }

   std::string sPayload = ReadFile (sPayloadPath);
   if (sPayload.empty ())
   {
      std::cerr << "Error: cannot read payload file: " << sPayloadPath << "\n";
      return 1;
   }

   std::string sKey = ReadFile (sKeyPath);
   if (sKey.empty ())
   {
      std::cerr << "Error: cannot read key file: " << sKeyPath << "\n";
      return 1;
   }

   std::vector<std::string> aCertChain;
   for (const char* sPath : aCertPaths)
   {
      std::string sCert = ReadFile (sPath);
      if (sCert.empty ())
      {
         std::cerr << "Error: cannot read certificate file: " << sPath << "\n";
         return 1;
      }
      aCertChain.push_back (sCert);
   }

   std::string sJws = sneeze::jws::JWS_BASE::Sign (sPayload, sKey, aCertChain, sAlgorithm);
   if (sJws.empty ())
   {
      std::cerr << "Error: signing failed\n";
      return 1;
   }

   if (!WriteFile (sOutPath, sJws))
   {
      std::cerr << "Error: cannot write output file: " << sOutPath << "\n";
      return 1;
   }

   std::cout << "Signed " << sOutPath << " (" << sJws.size () << " bytes)\n";
   return 0;
}
