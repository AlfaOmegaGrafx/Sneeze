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

#ifndef SNEEZE_JWS_MSFFILE_H
#define SNEEZE_JWS_MSFFILE_H

#include <nlohmann/json.hpp>
#include <map>
#include <string>
#include <vector>

#include "CertChain.h"
#include "CertInfo.h"

namespace sneeze
{
namespace msf
{

struct MSF_SERVICE
{
   std::string              sName;
   std::string              sType;
   std::string              sEndpoint;
   std::vector<std::string> aModules;
};

struct MSF_MODULE
{
   std::string sUrl;
   std::string sSha256;
};

class MSF_FILE
{
public:
   MSF_FILE ();
   ~MSF_FILE ();

   MSF_FILE (const MSF_FILE&) = delete;
   MSF_FILE& operator= (const MSF_FILE&) = delete;
   MSF_FILE (MSF_FILE&&) = delete;
   MSF_FILE& operator= (MSF_FILE&&) = delete;

   // --- Parse & Export ---------------------------------------------------

   bool        Parse (const std::string& sJws);
   std::string Sign  (const std::string& sPrivateKeyPem,
                      const std::string& sAlgorithm = "RS256");

   // --- Verification -----------------------------------------------------

   bool VerifySignature ();
   bool VerifyChain ();

   // --- Trust store (for verification) -----------------------------------

   void AddTrustedCert (const std::string& sPem);

   // --- Certificate chain (for composition) ------------------------------

   void                           AddCert (const std::string& sPem);
   bool                           RemoveCert (int nIndex);
   const std::vector<CERT_INFO>&  GetCertInfos () const;
   int                            GetCertCount () const;

   // --- Payload (bulk) ---------------------------------------------------

   void           SetPayload (const nlohmann::json& payload);
   nlohmann::json GetPayload () const;

   // --- Payload (typed fields) -------------------------------------------

   void        SetNamespace    (const std::string& sNamespace);
   std::string GetNamespace    () const;
   void        SetOrganization (const std::string& sOrganization);
   std::string GetOrganization () const;
   void        SetSuccessor    (const std::string& sSuccessor);
   std::string GetSuccessor    () const;

   // --- Services ---------------------------------------------------------

   void                     AddService    (const MSF_SERVICE& service);
   bool                     RemoveService (const std::string& sName);
   std::vector<MSF_SERVICE> GetServices   () const;

   // --- Modules ----------------------------------------------------------

   void                             AddModule    (const std::string& sName,
                                                  const std::string& sUrl,
                                                  const std::string& sSha256);
   bool                             RemoveModule (const std::string& sName);
   std::map<std::string, MSF_MODULE> GetModules  () const;

   // --- Status -----------------------------------------------------------

   std::string GetAlgorithm      () const;
   std::string GetFingerprint    () const;
   bool        IsSignatureValid  () const;
   bool        IsChainTrusted    () const;
   std::string GetSignatureError () const;
   std::string GetChainError     () const;

private:
   nlohmann::json             m_payload;
   std::string                m_sAlgorithm;
   std::string                m_sFingerprint;
   std::string                m_sRawJws;
   std::string                m_sSignatureError;
   std::string                m_sChainError;
   bool                       m_bSignatureValid;
   bool                       m_bChainTrusted;
   bool                       m_bParsed;

   std::vector<std::string>   m_aX5cEntries;    // base64 DER (from parse)
   std::vector<CERT_INFO>     m_aCertInfos;
   std::vector<std::string>   m_aCertsPem;      // PEM certs (for composition)

   CERT_CHAIN                 m_certChain;
};

} // namespace msf
} // namespace sneeze

#endif // SNEEZE_JWS_MSFFILE_H
