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

#ifndef SNEEZE_JWS_CERTCHAIN_H
#define SNEEZE_JWS_CERTCHAIN_H

#include <string>
#include <vector>

namespace sneeze
{
namespace jws
{

class CERT_CHAIN
{
public:
   CERT_CHAIN ();
   ~CERT_CHAIN ();

   CERT_CHAIN (const CERT_CHAIN&) = delete;
   CERT_CHAIN& operator= (const CERT_CHAIN&) = delete;
   CERT_CHAIN (CERT_CHAIN&&) = delete;
   CERT_CHAIN& operator= (CERT_CHAIN&&) = delete;

   bool Validate (const std::vector<std::string>& aX5cEntries, std::string& sError);

   std::string GetLeafFingerprint () const;

   void AddTrustedCert (const std::string& sPem);

private:
   void LoadTrustStore ();

   struct IMPL;
   IMPL* m_pImpl;
};

} // namespace jws
} // namespace sneeze

#endif // SNEEZE_JWS_CERTCHAIN_H
