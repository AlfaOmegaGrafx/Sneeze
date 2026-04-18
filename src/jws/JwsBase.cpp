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

#include "JwsBase.h"

#include <jwt-cpp/traits/nlohmann-json/defaults.h>
#include <openssl/bio.h>
#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/x509.h>

#include <cstdio>
#include <sstream>
#include <vector>

namespace sneeze
{
namespace jws
{

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static std::string CertPemToDerBase64 (const std::string& sPem)
{
   BIO* pBio = BIO_new_mem_buf (sPem.data (), (int) sPem.size ());
   ERR_clear_error ();
   X509* pCert = PEM_read_bio_X509 (pBio, nullptr, nullptr, nullptr);
   BIO_free (pBio);

   std::string sResult;
   if (!pCert)
   {
      std::fprintf (stderr, "  CertPemToDerBase64: PEM_read_bio_X509 failed. Full error stack:\n");
      unsigned long nErr;
      while ((nErr = ERR_get_error ()) != 0)
      {
         char aBuf[256] = {0};
         ERR_error_string_n (nErr, aBuf, sizeof (aBuf));
         std::fprintf (stderr, "    0x%08lx: %s\n", nErr, aBuf);
      }
      return sResult;
   }

   unsigned char* pDer = nullptr;
   int nLen = i2d_X509 (pCert, &pDer);
   X509_free (pCert);

   if (nLen > 0  &&  pDer)
   {
      size_t nB64Len = ((size_t) nLen + 2) / 3 * 4;
      std::vector<unsigned char> aB64 (nB64Len + 1);
      EVP_EncodeBlock (aB64.data (), pDer, nLen);
      sResult.assign ((const char*) aB64.data ());
      OPENSSL_free (pDer);
   }

   return sResult;
}

// ---------------------------------------------------------------------------
// Construction / Destruction
// ---------------------------------------------------------------------------

JWS_BASE::JWS_BASE ()
{
}

JWS_BASE::~JWS_BASE ()
{
}

// ---------------------------------------------------------------------------
// Sign
// ---------------------------------------------------------------------------

std::string JWS_BASE::Sign (const std::string& sPayload,
                            const std::string& sPrivateKeyPem,
                            const std::vector<std::string>& aCertChainPem,
                            const std::string& sAlgorithm)
{
   std::string sResult;

   try
   {
      nlohmann::json aX5c = nlohmann::json::array ();
      for (size_t i = 0; i < aCertChainPem.size (); ++i)
      {
         std::string sB64 = CertPemToDerBase64 (aCertChainPem[i]);
         if (sB64.empty ())
         {
            unsigned long nErr = ERR_peek_last_error ();
            char aErrBuf[256] = {0};
            ERR_error_string_n (nErr, aErrBuf, sizeof (aErrBuf));
            std::fprintf (stderr,
               "JWS_BASE::Sign: cert %zu PEM parse failed "
               "(err=0x%08lx: %s, pem-len=%zu, first-line=\"%.60s\")\n",
               i, nErr, aErrBuf, aCertChainPem[i].size (),
               aCertChainPem[i].c_str ());
            return sResult;
         }
         aX5c.push_back (sB64);
      }

      auto pBuilder = jwt::create ()
         .set_type ("JWS")
         .set_header_claim ("x5c", jwt::claim (aX5c))
         .set_payload_claim ("data", jwt::claim (sPayload));

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
         std::fprintf (stderr, "JWS_BASE::Sign: unknown algorithm \"%s\"\n",
            sAlgorithm.c_str ());
   }
   catch (const std::exception& ex)
   {
      std::fprintf (stderr, "JWS_BASE::Sign: exception: %s\n", ex.what ());
      sResult.clear ();
   }

   return sResult;
}

// ---------------------------------------------------------------------------
// Verify
// ---------------------------------------------------------------------------

JWS_RESULT JWS_BASE::Verify (const std::string& sJws)
{
   m_result = JWS_RESULT {};

   if (sJws.empty ())
   {
      m_result.sError = "empty JWS string";
      return m_result;
   }

   try
   {
      auto decoded = jwt::decode (sJws);

      m_result.sAlgorithm = decoded.get_algorithm ();

      if (!decoded.has_header_claim ("x5c"))
      {
         m_result.sError = "JWS header missing x5c claim";
         return m_result;
      }

      auto x5cClaim = decoded.get_header_claim ("x5c");
      auto aX5cJson = x5cClaim.as_array ();

      std::vector<std::string> aX5cEntries;
      for (const auto& entry : aX5cJson)
         aX5cEntries.push_back (entry.get<std::string> ());

      std::string sChainError;
      if (!m_certChain.Validate (aX5cEntries, sChainError))
      {
         m_result.sError = "certificate chain validation failed: " + sChainError;
         return m_result;
      }

      m_result.sFingerprint = m_certChain.GetLeafFingerprint ();

      std::string sLeafB64 = aX5cEntries[0];
      size_t nMaxDer = (sLeafB64.size () / 4 + 1) * 3;
      std::vector<unsigned char> aDer (nMaxDer);
      int nDerLen = EVP_DecodeBlock (aDer.data (),
         (const unsigned char*) sLeafB64.data (), (int) sLeafB64.size ());
      if (nDerLen <= 0)
      {
         m_result.sError = "failed to base64-decode leaf certificate";
         return m_result;
      }
      size_t nPad = 0;
      for (auto it = sLeafB64.rbegin (); it != sLeafB64.rend ()  &&  *it == '='; ++it)
         nPad++;
      nDerLen -= (int) nPad;

      const unsigned char* pDerPtr = aDer.data ();
      X509* pLeafCert = d2i_X509 (nullptr, &pDerPtr, nDerLen);

      if (!pLeafCert)
      {
         m_result.sError = "failed to decode leaf certificate for signature verification";
         return m_result;
      }

      EVP_PKEY* pPubKey = X509_get_pubkey (pLeafCert);
      X509_free (pLeafCert);

      if (!pPubKey)
      {
         m_result.sError = "failed to extract public key from leaf certificate";
         return m_result;
      }

      BIO* pPubBio = BIO_new (BIO_s_mem ());
      PEM_write_bio_PUBKEY (pPubBio, pPubKey);
      EVP_PKEY_free (pPubKey);

      char* pPubData = nullptr;
      long nPubLen = BIO_get_mem_data (pPubBio, &pPubData);
      std::string sPubKeyPem (pPubData, (size_t) nPubLen);
      BIO_free (pPubBio);

      if (m_result.sAlgorithm == "RS256")
      {
         jwt::verify ().allow_algorithm (jwt::algorithm::rs256 (sPubKeyPem)).verify (decoded);
      }
      else if (m_result.sAlgorithm == "RS384")
      {
         jwt::verify ().allow_algorithm (jwt::algorithm::rs384 (sPubKeyPem)).verify (decoded);
      }
      else if (m_result.sAlgorithm == "RS512")
      {
         jwt::verify ().allow_algorithm (jwt::algorithm::rs512 (sPubKeyPem)).verify (decoded);
      }
      else if (m_result.sAlgorithm == "ES256")
      {
         jwt::verify ().allow_algorithm (jwt::algorithm::es256 (sPubKeyPem)).verify (decoded);
      }
      else if (m_result.sAlgorithm == "ES384")
      {
         jwt::verify ().allow_algorithm (jwt::algorithm::es384 (sPubKeyPem)).verify (decoded);
      }
      else if (m_result.sAlgorithm == "ES512")
      {
         jwt::verify ().allow_algorithm (jwt::algorithm::es512 (sPubKeyPem)).verify (decoded);
      }
      else
      {
         m_result.sError = "unsupported algorithm: " + m_result.sAlgorithm;
         return m_result;
      }

      if (decoded.has_payload_claim ("data"))
      {
         std::string sPayloadStr = decoded.get_payload_claim ("data").as_string ();
         try
         {
            m_result.payload = nlohmann::json::parse (sPayloadStr);
         }
         catch (...)
         {
            m_result.payload = sPayloadStr;
         }
      }

      if (m_result.payload.is_object ()  &&  m_result.payload.contains ("successor"))
         m_result.sSuccessorFingerprint = m_result.payload["successor"].get<std::string> ();
   }
   catch (const std::exception& ex)
   {
      if (m_result.sError.empty ())
         m_result.sError = std::string ("JWS verification failed: ") + ex.what ();
   }

   return m_result;
}

// ---------------------------------------------------------------------------
// Accessors
// ---------------------------------------------------------------------------

nlohmann::json JWS_BASE::GetPayloadJson () const
{
   return m_result.payload;
}

std::string JWS_BASE::GetSignerFingerprint () const
{
   return m_result.sFingerprint;
}

std::string JWS_BASE::GetSuccessorFingerprint () const
{
   return m_result.sSuccessorFingerprint;
}

CERT_CHAIN& JWS_BASE::GetCertChain ()
{
   return m_certChain;
}

} // namespace jws
} // namespace sneeze
