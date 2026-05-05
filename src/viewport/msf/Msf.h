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

#ifndef SNEEZE_VIEWPORT_MSF_H
#define SNEEZE_VIEWPORT_MSF_H

#include "viewport/Viewport.h"
#include <nlohmann/json.hpp>
#include <map>
#include <string>
#include <vector>

class SNEEZE::VIEWPORT::MSF
{
public:

   // --- Nested types ---

   struct SERVICE
   {
      std::string              sName;
      std::string              sType;
      std::string              sEndpoint;
      std::vector<std::string> aModules;
   };

   struct MODULE
   {
      std::string sUrl;
      std::string sSha256;
   };

   struct CERT
   {
      std::string sSubject;
      std::string sIssuer;
      std::string sSerial;
      std::string sNotBefore;
      std::string sNotAfter;
      std::string sKeyType;      // "RSA", "EC", or "unknown"
      int         nKeyBits;
      bool        bIsCA;
   };

   class CHAIN
   {
   public:
      CHAIN ();
      ~CHAIN ();

      CHAIN (const CHAIN&) = delete;
      CHAIN& operator= (const CHAIN&) = delete;
      CHAIN (CHAIN&&) = delete;
      CHAIN& operator= (CHAIN&&) = delete;

      bool Validate (const std::vector<std::string>& aX5cEntries, std::string& sError);

      std::string GetLeafFingerprint () const;

      const std::vector<CERT>& GetCertInfos () const;

      void AddTrustedCert (const std::string& sPem);

      static CERT        DecodeInfoDerBase64 (const std::string& sB64, bool bIsCA);
      static CERT        DecodeInfoPem       (const std::string& sPem, bool bIsCA);
      static std::string ComputeFingerprint  (const std::string& sB64Der);
      static std::string ExtractPublicKeyPem (const std::string& sB64Der);
      static std::string PemToDerBase64      (const std::string& sPem);

   private:
      void LoadTrustStore ();

      struct IMPL;
      IMPL*              m_pImpl;
      std::vector<CERT>  m_aCertInfos;
   };

   // --- Lifecycle ---

   explicit MSF (SNEEZE* pSneeze = nullptr);
   ~MSF ();

   MSF (const MSF&) = delete;
   MSF& operator= (const MSF&) = delete;
   MSF (MSF&&) = delete;
   MSF& operator= (MSF&&) = delete;

   // --- Parse & Export ---

   bool        Parse (const std::string& sJws);
   std::string Sign  (const std::string& sPrivateKeyPem,
                      const std::string& sAlgorithm = "RS256");

   // --- Verification ---

   bool VerifySignature ();
   bool VerifyChain ();

   // --- Trust store ---

   void AddTrustedCert (const std::string& sPem);

   // --- Certificate chain ---

   void                      AddCert (const std::string& sPem);
   bool                      RemoveCert (int nIndex);
   const std::vector<CERT>&  GetCertInfos () const;
   int                       GetCertCount () const;

   // --- Payload (bulk) ---

   void           SetPayload (const nlohmann::json& payload);
   nlohmann::json GetPayload () const;

   // --- Payload (typed fields) ---

   void        SetNamespace    (const std::string& sNamespace);
   std::string GetNamespace    () const;
   void        SetOrganization (const std::string& sOrganization);
   std::string GetOrganization () const;
   void        SetSuccessor    (const std::string& sSuccessor);
   std::string GetSuccessor    () const;

   // --- Services ---

   void                  AddService    (const SERVICE& service);
   bool                  RemoveService (const std::string& sName);
   std::vector<SERVICE>  GetServices   () const;

   // --- Modules ---

   void                            AddModule    (const std::string& sName,
                                                 const std::string& sUrl,
                                                 const std::string& sSha256);
   bool                            RemoveModule (const std::string& sName);
   std::map<std::string, MODULE>   GetModules   () const;

   // --- Status ---

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

   std::vector<std::string>   m_aX5cEntries;
   std::vector<CERT>          m_aCertInfos;
   std::vector<std::string>   m_aCertsPem;

   CHAIN                      m_certChain;
   SNEEZE*                    m_pSneeze;
};

#endif // SNEEZE_VIEWPORT_MSF_H
