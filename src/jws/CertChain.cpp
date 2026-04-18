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

#include "CertChain.h"

#include <openssl/bio.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/sha.h>
#include <openssl/x509.h>
#include <openssl/x509_vfy.h>

#ifdef _WIN32
#include <windows.h>
#include <wincrypt.h>
#endif

#include <cstdio>
#include <iomanip>
#include <sstream>
#include <vector>

namespace sneeze
{
namespace jws
{

struct CERT_CHAIN::IMPL
{
   X509_STORE* pStore;
   X509*       pLeaf;

   IMPL () : pStore (nullptr), pLeaf (nullptr) {}

   ~IMPL ()
   {
      if (pLeaf)
         X509_free (pLeaf);
      if (pStore)
         X509_STORE_free (pStore);
   }
};

CERT_CHAIN::CERT_CHAIN ()
   : m_pImpl (new IMPL)
{
}

CERT_CHAIN::~CERT_CHAIN ()
{
   delete m_pImpl;
}

// ---------------------------------------------------------------------------
// LoadTrustStore
// ---------------------------------------------------------------------------

void CERT_CHAIN::LoadTrustStore ()
{
   if (m_pImpl->pStore)
      return;

   m_pImpl->pStore = X509_STORE_new ();

#ifdef _WIN32
   HCERTSTORE hSysStore = CertOpenSystemStoreA (0, "ROOT");
   if (hSysStore)
   {
      PCCERT_CONTEXT pCtx = nullptr;
      while ((pCtx = CertEnumCertificatesInStore (hSysStore, pCtx)) != nullptr)
      {
         const unsigned char* pDer = pCtx->pbCertEncoded;
         X509* pCert = d2i_X509 (nullptr, &pDer, (long) pCtx->cbCertEncoded);
         if (pCert)
         {
            X509_STORE_add_cert (m_pImpl->pStore, pCert);
            X509_free (pCert);
         }
      }
      CertCloseStore (hSysStore, 0);
   }
#else
   X509_STORE_set_default_paths (m_pImpl->pStore);
#endif
}

// ---------------------------------------------------------------------------
// Validate
// ---------------------------------------------------------------------------

static X509* DecodeDerBase64 (const std::string& sB64)
{
   size_t nMaxLen = (sB64.size () / 4 + 1) * 3;
   std::vector<unsigned char> aDer (nMaxLen);

   int nLen = EVP_DecodeBlock (aDer.data (),
      (const unsigned char*) sB64.data (), (int) sB64.size ());
   if (nLen <= 0)
      return nullptr;

   // EVP_DecodeBlock doesn't strip padding -- adjust length for '=' chars
   size_t nPad = 0;
   for (auto it = sB64.rbegin (); it != sB64.rend ()  &&  *it == '='; ++it)
      nPad++;
   nLen -= (int) nPad;

   const unsigned char* pDer = aDer.data ();
   return d2i_X509 (nullptr, &pDer, nLen);
}

bool CERT_CHAIN::Validate (const std::vector<std::string>& aX5cEntries, std::string& sError)
{
   if (aX5cEntries.empty ())
   {
      sError = "x5c chain is empty";
      return false;
   }

   LoadTrustStore ();

   std::vector<X509*> aCerts;
   for (const auto& sEntry : aX5cEntries)
   {
      X509* pCert = DecodeDerBase64 (sEntry);
      if (!pCert)
      {
         sError = "failed to decode x5c certificate";
         for (auto* p : aCerts)
            X509_free (p);
         return false;
      }
      aCerts.push_back (pCert);
   }

   X509* pLeaf = aCerts[0];

   STACK_OF (X509)* pIntermediates = sk_X509_new_null ();
   for (size_t i = 1; i < aCerts.size (); ++i)
      sk_X509_push (pIntermediates, aCerts[i]);

   X509_STORE_CTX* pCtx = X509_STORE_CTX_new ();
   X509_STORE_CTX_init (pCtx, m_pImpl->pStore, pLeaf, pIntermediates);

   bool bResult = false;
   int nRc = X509_verify_cert (pCtx);
   if (nRc == 1)
   {
      bResult = true;

      if (m_pImpl->pLeaf)
         X509_free (m_pImpl->pLeaf);
      m_pImpl->pLeaf = X509_dup (pLeaf);
   }
   else
   {
      int nErr = X509_STORE_CTX_get_error (pCtx);
      sError = X509_verify_cert_error_string (nErr);
   }

   X509_STORE_CTX_free (pCtx);
   sk_X509_free (pIntermediates);

   for (auto* p : aCerts)
      X509_free (p);

   return bResult;
}

// ---------------------------------------------------------------------------
// GetLeafFingerprint
// ---------------------------------------------------------------------------

std::string CERT_CHAIN::GetLeafFingerprint () const
{
   std::string sResult;
   if (!m_pImpl->pLeaf)
      return sResult;

   unsigned char aPubKeyDer[8192];
   unsigned char* pOut = aPubKeyDer;
   int nLen = i2d_PUBKEY (X509_get_pubkey (m_pImpl->pLeaf), &pOut);
   if (nLen <= 0)
      return sResult;

   unsigned char aDigest[SHA256_DIGEST_LENGTH];
   SHA256 (aPubKeyDer, (size_t) nLen, aDigest);

   std::ostringstream oss;
   oss << std::hex << std::setfill ('0');
   for (int i = 0; i < SHA256_DIGEST_LENGTH; ++i)
      oss << std::setw (2) << (int) aDigest[i];

   sResult = oss.str ();
   return sResult;
}

// ---------------------------------------------------------------------------
// AddTrustedCert
// ---------------------------------------------------------------------------

void CERT_CHAIN::AddTrustedCert (const std::string& sPem)
{
   LoadTrustStore ();

   BIO* pBio = BIO_new_mem_buf (sPem.data (), (int) sPem.size ());
   X509* pCert = PEM_read_bio_X509 (pBio, nullptr, nullptr, nullptr);
   BIO_free (pBio);

   if (pCert)
   {
      X509_STORE_add_cert (m_pImpl->pStore, pCert);
      X509_free (pCert);
   }
}

} // namespace jws
} // namespace sneeze
