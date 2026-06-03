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

#include <Container.h>
#include <Context.h>

using namespace SNEEZE;


// ============================================================================
// CONTAINER::Impl
// ============================================================================

class CONTAINER::Impl
{
public:

   Impl (CONTEXT* pContext, const CID* pCID) :
      m_pContext  (pContext),
      m_CID      (*pCID),
      m_nCount   (0),
      m_bInitialized (false)
   {
   }

  ~Impl ()
   {
      if (m_bInitialized)
         Shutdown ();
   }

   // -----------------------------------------------------------------------
   // Lifecycle
   // -----------------------------------------------------------------------

   bool Initialize ()
   {
      bool bResult = false;

      if (!m_bInitialized)
      {
         m_bInitialized = true;
         bResult = true;
      }

      return bResult;
   }

   void Shutdown ()
   {
      if (m_bInitialized)
      {
         m_bInitialized = false;
      }
   }

   // -----------------------------------------------------------------------
   // Reference counting
   // -----------------------------------------------------------------------

   int Open ()
   {
      return ++m_nCount;
   }

   int Close ()
   {
      return --m_nCount;
   }

   int Count () const
   {
      return m_nCount;
   }

   // -----------------------------------------------------------------------
   // Accessors
   // -----------------------------------------------------------------------

   CONTEXT*   Context () const { return m_pContext; }
   const CID* CID_Get () const { return &m_CID; }

   // -----------------------------------------------------------------------
   // Members
   // -----------------------------------------------------------------------

   CONTEXT*   m_pContext;
   CID        m_CID;
   int        m_nCount;
   bool       m_bInitialized;
};


// ============================================================================
// CONTAINER
// ============================================================================

CONTAINER::CONTAINER (CONTEXT* pContext, const CID* pCID) :
   m_pImpl (new Impl (pContext, pCID))
{
}

CONTAINER::~CONTAINER ()
{
   delete m_pImpl;
}

bool           CONTAINER::Initialize ()              { return m_pImpl->Initialize (); }
void           CONTAINER::Shutdown   ()              {        m_pImpl->Shutdown   (); }

int            CONTAINER::Open       ()              { return m_pImpl->Open       (); }
int            CONTAINER::Close      ()              { return m_pImpl->Close      (); }
int            CONTAINER::Count      () const        { return m_pImpl->Count      (); }

SNEEZE::CONTEXT* CONTAINER::Context  () const        { return m_pImpl->Context    (); }
const CONTAINER::CID* CONTAINER::CID_Get () const    { return m_pImpl->CID_Get   (); }
