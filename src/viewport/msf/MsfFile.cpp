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
#include "Msf.h"

#include <jwt-cpp/traits/nlohmann-json/defaults.h>

#include <cstdio>

using namespace SNEEZE;

using MSF     = VIEWPORT::MSF;
using SERVICE = MSF::SERVICE;
using MODULE  = MSF::MODULE;

// ---------------------------------------------------------------------------
// Construction / Destruction
// ---------------------------------------------------------------------------

MSF::MSF (ENGINE* pEngine)
   : m_bSignatureValid (false)
   , m_bChainTrusted (false)
   , m_bParsed (false)
   , m_pEngine (pEngine)
{
}

MSF::~MSF ()
{
}

// ---------------------------------------------------------------------------
// Parse
// ---------------------------------------------------------------------------

bool MSF::Parse (const std::string& sJws)
{
   bool bResult = false;

   m_payload         = nlohmann::json ();
   m_sAlgorithm.clear ();
   m_sFingerprint.clear ();
   m_sRawJws.clear ();
   m_sSignatureError.clear ();
   m_sChainError.clear ();
   m_bSignatureValid = false;
   m_bChainTrusted   = false;
   m_bParsed         = false;
   m_aX5cEntries.clear ();
   m_aCertInfos.clear ();

   if (!sJws.empty ())
   {
      try
      {
         m_sRawJws = sJws;
         auto decoded = jwt::decode (sJws);

         m_sAlgorithm = decoded.get_algorithm ();

         if (decoded.has_header_claim ("x5c"))
         {
            auto aX5cJson = decoded.get_header_claim ("x5c").as_array ();
            for (const auto& entry : aX5cJson)
               m_aX5cEntries.push_back (entry.get<std::string> ());

            for (size_t i = 0; i < m_aX5cEntries.size (); ++i)
            {
               m_aCertInfos.push_back (
                  CHAIN::DecodeInfoDerBase64 (m_aX5cEntries[i], i > 0));

               if (i == 0)
                  m_sFingerprint = CHAIN::ComputeFingerprint (m_aX5cEntries[0]);
            }
         }

         if (decoded.has_payload_claim ("data"))
         {
            std::string sPayloadStr = decoded.get_payload_claim ("data").as_string ();
            try
            {
               m_payload = nlohmann::json::parse (sPayloadStr);
            }
            catch (...)
            {
               m_payload = sPayloadStr;
            }
         }

         m_bParsed = true;
         bResult   = true;
      }
      catch (const std::exception& ex)
      {
         if (m_pEngine)
            m_pEngine->Log (ENGINE::IENGINE::kLOGLEVEL_Error, "MSF", std::string ("Parse: ") + ex.what ());
      }
   }

   return bResult;
}

// ---------------------------------------------------------------------------
// Sign
// ---------------------------------------------------------------------------

std::string MSF::Sign (const std::string& sPrivateKeyPem,
                                         const std::string& sAlgorithm)
{
   std::string sResult;

   try
   {
      nlohmann::json aX5c = nlohmann::json::array ();
      bool bCertsOk = true;

      for (size_t i = 0; i < m_aCertsPem.size ()  &&  bCertsOk; ++i)
      {
         std::string sB64 = CHAIN::PemToDerBase64 (m_aCertsPem[i]);
         if (sB64.empty ())
            bCertsOk = false;
         else
            aX5c.push_back (sB64);
      }

      if (bCertsOk)
      {
         std::string sPayloadStr = m_payload.dump ();

         auto pBuilder = jwt::create ()
            .set_type ("JWS")
            .set_header_claim ("x5c", jwt::claim (aX5c))
            .set_payload_claim ("data", jwt::claim (sPayloadStr));

         if (sAlgorithm == "RS256")
            sResult = pBuilder.sign (jwt::algorithm::rs256 ("", sPrivateKeyPem));
         else if (sAlgorithm == "RS384")
            sResult = pBuilder.sign (jwt::algorithm::rs384 ("", sPrivateKeyPem));
         else if (sAlgorithm == "RS512")
            sResult = pBuilder.sign (jwt::algorithm::rs512 ("", sPrivateKeyPem));
         else if (sAlgorithm == "ES256")
            sResult = pBuilder.sign (jwt::algorithm::es256 ("", sPrivateKeyPem));
         else if (sAlgorithm == "ES384")
            sResult = pBuilder.sign (jwt::algorithm::es384 ("", sPrivateKeyPem));
         else if (sAlgorithm == "ES512")
            sResult = pBuilder.sign (jwt::algorithm::es512 ("", sPrivateKeyPem));
         else
         {
            if (m_pEngine)
               m_pEngine->Log (ENGINE::IENGINE::kLOGLEVEL_Error, "MSF", "Sign: unknown algorithm \"" + sAlgorithm + "\"");
         }
      }
   }
   catch (const std::exception& ex)
   {
      if (m_pEngine)
         m_pEngine->Log (ENGINE::IENGINE::kLOGLEVEL_Error, "MSF", std::string ("Sign: exception: ") + ex.what ());

      sResult.clear ();
   }

   return sResult;
}

// ---------------------------------------------------------------------------
// VerifySignature
// ---------------------------------------------------------------------------

bool MSF::VerifySignature ()
{
   bool bResult = false;
   m_bSignatureValid = false;
   m_sSignatureError.clear ();

   if (!m_bParsed)
   {
      m_sSignatureError = "no data parsed (call Parse first)";
   }
   else if (m_aX5cEntries.empty ())
   {
      m_sSignatureError = "no certificates in JWS header";
   }
   else
   {
      try
      {
         auto decoded = jwt::decode (m_sRawJws);

         std::string sPubKeyPem = CHAIN::ExtractPublicKeyPem (m_aX5cEntries[0]);
         if (sPubKeyPem.empty ())
            throw std::runtime_error ("failed to extract public key from leaf certificate");

         if (m_sAlgorithm == "RS256")
            jwt::verify ().allow_algorithm (jwt::algorithm::rs256 (sPubKeyPem)).verify (decoded);
         else if (m_sAlgorithm == "RS384")
            jwt::verify ().allow_algorithm (jwt::algorithm::rs384 (sPubKeyPem)).verify (decoded);
         else if (m_sAlgorithm == "RS512")
            jwt::verify ().allow_algorithm (jwt::algorithm::rs512 (sPubKeyPem)).verify (decoded);
         else if (m_sAlgorithm == "ES256")
            jwt::verify ().allow_algorithm (jwt::algorithm::es256 (sPubKeyPem)).verify (decoded);
         else if (m_sAlgorithm == "ES384")
            jwt::verify ().allow_algorithm (jwt::algorithm::es384 (sPubKeyPem)).verify (decoded);
         else if (m_sAlgorithm == "ES512")
            jwt::verify ().allow_algorithm (jwt::algorithm::es512 (sPubKeyPem)).verify (decoded);
         else
            throw std::runtime_error ("unsupported algorithm: " + m_sAlgorithm);

         m_bSignatureValid = true;
         bResult = true;
      }
      catch (const std::exception& ex)
      {
         if (m_sSignatureError.empty ())
            m_sSignatureError = ex.what ();
      }
   }

   return bResult;
}

// ---------------------------------------------------------------------------
// VerifyChain
// ---------------------------------------------------------------------------

bool MSF::VerifyChain ()
{
   bool bResult = false;
   m_bChainTrusted = false;
   m_sChainError.clear ();

   if (!m_bParsed)
   {
      m_sChainError = "no data parsed (call Parse first)";
   }
   else if (m_aX5cEntries.empty ())
   {
      m_sChainError = "no certificates in JWS header";
   }
   else
   {
      std::string sError;
      if (m_certChain.Validate (m_aX5cEntries, sError))
      {
         m_bChainTrusted = true;
         bResult = true;
      }
      else
      {
         m_sChainError = sError;
      }
   }

   return bResult;
}

// ---------------------------------------------------------------------------
// Trust store
// ---------------------------------------------------------------------------

void MSF::AddTrustedCert (const std::string& sPem)
{
   m_certChain.AddTrustedCert (sPem);
}

// ---------------------------------------------------------------------------
// Certificate chain management
// ---------------------------------------------------------------------------

void MSF::AddCert (const std::string& sPem)
{
   bool bIsCA = !m_aCertsPem.empty ();
   m_aCertsPem.push_back (sPem);
   m_aCertInfos.push_back (CHAIN::DecodeInfoPem (sPem, bIsCA));
}

bool MSF::RemoveCert (int nIndex)
{
   bool bResult = false;

   if (nIndex >= 0  &&  nIndex < (int) m_aCertsPem.size ())
   {
      m_aCertsPem.erase (m_aCertsPem.begin () + nIndex);
      if (nIndex < (int) m_aCertInfos.size ())
         m_aCertInfos.erase (m_aCertInfos.begin () + nIndex);
      bResult = true;
   }

   return bResult;
}

const std::vector<MSF::CERT>& MSF::GetCertInfos () const
{
   return m_aCertInfos;
}

int MSF::GetCertCount () const
{
   return (int) m_aCertInfos.size ();
}

// ---------------------------------------------------------------------------
// Payload (bulk)
// ---------------------------------------------------------------------------

void MSF::SetPayload (const nlohmann::json& payload)
{
   m_payload = payload;
}

nlohmann::json MSF::GetPayload () const
{
   return m_payload;
}

// ---------------------------------------------------------------------------
// Payload (typed fields)
// ---------------------------------------------------------------------------

void MSF::SetNamespace (const std::string& sNamespace)
{
   if (!m_payload.is_object ())
      m_payload = nlohmann::json::object ();
   m_payload["namespace"] = sNamespace;
}

std::string MSF::Namespace () const
{
   std::string sResult;
   if (m_payload.is_object ()  &&  m_payload.contains ("namespace"))
      sResult = m_payload["namespace"].get<std::string> ();
   return sResult;
}

void MSF::SetOrganization (const std::string& sOrganization)
{
   if (!m_payload.is_object ())
      m_payload = nlohmann::json::object ();
   m_payload["organization"] = sOrganization;
}

std::string MSF::GetOrganization () const
{
   std::string sResult;
   if (m_payload.is_object ()  &&  m_payload.contains ("organization"))
      sResult = m_payload["organization"].get<std::string> ();
   return sResult;
}

void MSF::SetSuccessor (const std::string& sSuccessor)
{
   if (!m_payload.is_object ())
      m_payload = nlohmann::json::object ();
   m_payload["successor"] = sSuccessor;
}

std::string MSF::GetSuccessor () const
{
   std::string sResult;
   if (m_payload.is_object ()  &&  m_payload.contains ("successor"))
      sResult = m_payload["successor"].get<std::string> ();
   return sResult;
}

// ---------------------------------------------------------------------------
// Services
// ---------------------------------------------------------------------------

void MSF::AddService (const SERVICE& service)
{
   if (!m_payload.is_object ())
      m_payload = nlohmann::json::object ();
   if (!m_payload.contains ("services"))
      m_payload["services"] = nlohmann::json::array ();

   nlohmann::json svc;
   svc["name"]     = service.sName;
   svc["type"]     = service.sType;
   svc["endpoint"] = service.sEndpoint;
   if (!service.aModules.empty ())
      svc["modules"] = service.aModules;

   m_payload["services"].push_back (svc);
}

bool MSF::RemoveService (const std::string& sName)
{
   bool bResult = false;

   if (m_payload.is_object ()  &&  m_payload.contains ("services")
       &&  m_payload["services"].is_array ())
   {
      auto& aServices = m_payload["services"];
      for (auto it = aServices.begin (); it != aServices.end (); ++it)
      {
         if (it->contains ("name")  &&  (*it)["name"].get<std::string> () == sName)
         {
            aServices.erase (it);
            bResult = true;
            break;
         }
      }
   }

   return bResult;
}

std::vector<SERVICE> MSF::GetServices () const
{
   std::vector<SERVICE> aResult;

   if (m_payload.is_object ()  &&  m_payload.contains ("services")
       &&  m_payload["services"].is_array ())
   {
      for (const auto& svc : m_payload["services"])
      {
         SERVICE entry;
         if (svc.contains ("name"))     entry.sName     = svc["name"].get<std::string> ();
         if (svc.contains ("type"))     entry.sType     = svc["type"].get<std::string> ();
         if (svc.contains ("endpoint")) entry.sEndpoint = svc["endpoint"].get<std::string> ();
         if (svc.contains ("modules"))
         {
            for (const auto& mod : svc["modules"])
               entry.aModules.push_back (mod.get<std::string> ());
         }
         aResult.push_back (entry);
      }
   }

   return aResult;
}

// ---------------------------------------------------------------------------
// Modules
// ---------------------------------------------------------------------------

void MSF::AddModule (const std::string& sName,
                                       const std::string& sUrl,
                                       const std::string& sSha256)
{
   if (!m_payload.is_object ())
      m_payload = nlohmann::json::object ();
   if (!m_payload.contains ("modules"))
      m_payload["modules"] = nlohmann::json::object ();

   nlohmann::json mod;
   mod["url"]    = sUrl;
   mod["sha256"] = sSha256;
   m_payload["modules"][sName] = mod;
}

bool MSF::RemoveModule (const std::string& sName)
{
   bool bResult = false;

   if (m_payload.is_object ()  &&  m_payload.contains ("modules")
       &&  m_payload["modules"].is_object ()  &&  m_payload["modules"].contains (sName))
   {
      m_payload["modules"].erase (sName);
      bResult = true;
   }

   return bResult;
}

std::map<std::string, MODULE> MSF::GetModules () const
{
   std::map<std::string, MODULE> aResult;

   if (m_payload.is_object ()  &&  m_payload.contains ("modules")
       &&  m_payload["modules"].is_object ())
   {
      for (auto it = m_payload["modules"].begin (); it != m_payload["modules"].end (); ++it)
      {
         MODULE mod;
         if (it.value ().contains ("url"))    mod.sUrl    = it.value ()["url"].get<std::string> ();
         if (it.value ().contains ("sha256")) mod.sSha256 = it.value ()["sha256"].get<std::string> ();
         aResult[it.key ()] = mod;
      }
   }

   return aResult;
}

// ---------------------------------------------------------------------------
// Status
// ---------------------------------------------------------------------------

std::string MSF::GetAlgorithm () const      { return m_sAlgorithm; }
std::string MSF::GetFingerprint () const     { return m_sFingerprint; }
bool        MSF::IsSignatureValid () const   { return m_bSignatureValid; }
bool        MSF::IsChainTrusted () const     { return m_bChainTrusted; }
std::string MSF::GetSignatureError () const  { return m_sSignatureError; }
std::string MSF::GetChainError () const      { return m_sChainError; }
