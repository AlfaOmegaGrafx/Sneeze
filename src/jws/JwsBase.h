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

#ifndef SNEEZE_JWS_JWSBASE_H
#define SNEEZE_JWS_JWSBASE_H

#include <nlohmann/json.hpp>
#include <string>
#include <vector>

#include "CertChain.h"

namespace sneeze
{
namespace jws
{

struct JWS_RESULT
{
   nlohmann::json  payload;
   std::string     sFingerprint;
   std::string     sSuccessorFingerprint;
   std::string     sAlgorithm;
   std::string     sError;
};

class JWS_BASE
{
public:
   JWS_BASE ();
   virtual ~JWS_BASE ();

   JWS_BASE (const JWS_BASE&) = delete;
   JWS_BASE& operator= (const JWS_BASE&) = delete;
   JWS_BASE (JWS_BASE&&) = delete;
   JWS_BASE& operator= (JWS_BASE&&) = delete;

   static std::string Sign (const std::string& sPayload,
                            const std::string& sPrivateKeyPem,
                            const std::vector<std::string>& aCertChainPem,
                            const std::string& sAlgorithm = "RS256");

   JWS_RESULT Verify (const std::string& sJws);

   nlohmann::json GetPayloadJson () const;
   std::string    GetSignerFingerprint () const;
   std::string    GetSuccessorFingerprint () const;

   CERT_CHAIN& GetCertChain ();

protected:
   JWS_RESULT  m_result;
   CERT_CHAIN  m_certChain;
};

} // namespace jws
} // namespace sneeze

#endif // SNEEZE_JWS_JWSBASE_H
