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

#include <Scene.h>
#include <Network.h>
#include "MapObject.h"
#include <Container.h>
#include "stb/stb_image.h"
#include <algorithm>

using namespace SNEEZE;

using CONTAINER = SNEEZE::CONTAINER;

// ---------------------------------------------------------------------------
// SEQLOCK
// ---------------------------------------------------------------------------

class SEQLOCK
{
public:
   SEQLOCK ();

   uint32_t BeginRead () const;
   bool     EndRead (uint32_t nSeq) const;
   void     BeginWrite ();
   void     EndWrite ();

private:
   std::atomic<uint32_t> m_nSequence;
};

SEQLOCK::SEQLOCK () : m_nSequence (0) {}

uint32_t SEQLOCK::BeginRead () const
{
   uint32_t nSeq;
   do
   {
      nSeq = m_nSequence.load (std::memory_order_acquire);
   }
   while (nSeq & 1);
   return nSeq;
}

bool SEQLOCK::EndRead (uint32_t nSeq) const
{
   std::atomic_thread_fence (std::memory_order_acquire);
   return m_nSequence.load (std::memory_order_relaxed) == nSeq;
}

void SEQLOCK::BeginWrite ()
{
   uint32_t nExpected = m_nSequence.load (std::memory_order_relaxed);
   uint32_t nDesired;
   do
   {
      while (nExpected & 1)
         nExpected = m_nSequence.load (std::memory_order_relaxed);
      nDesired = nExpected + 1;
   }
   while (!m_nSequence.compare_exchange_weak (nExpected, nDesired,
      std::memory_order_acquire, std::memory_order_relaxed));
}

void SEQLOCK::EndWrite ()
{
   m_nSequence.fetch_add (1, std::memory_order_release);
}

// ---------------------------------------------------------------------------
// NODE::Impl
// ---------------------------------------------------------------------------

class NODE::Impl : public NETWORK::IFILE
{
public:
   Impl (NODE* pNode, FABRIC* pFabric, NODE* pNode_Parent) :
      m_pNode              (pNode),
      m_pFabric            (pFabric),
      m_pNode_Parent       (pNode_Parent),
      m_pMap_Object        (nullptr),
      m_pFabric_Attachment (nullptr),
      m_pFile              (nullptr),
      m_twObjectIx         (0),
      m_bPrivate           (false)
   {
      if (m_pNode_Parent)
         m_pNode_Parent->Node_Add (m_pNode);
      else m_pFabric->Node_Root (m_pNode);
   }

   bool Initialize (MAP_OBJECT* pMap_Object)
   {
      bool bResult = true;

      m_pMap_Object = pMap_Object;

      if (m_pMap_Object)
      {
         if (!m_pMap_Object->m_sUrl_Fabric.empty ())
            bResult = Fabric_Open (m_pMap_Object->m_sUrl_Fabric);
         else if (!m_pMap_Object->m_sUrl_Texture.empty ())
            Texture_Request ();
      }

      return bResult;
   }

   ~Impl ()
   {
      while (!m_apNode.empty ())
         delete m_apNode.back ();

      Fabric_Close ();

      Texture_Release ();

      if (m_pNode_Parent)
         m_pNode_Parent->Node_Remove (m_pNode);
      else m_pFabric->Node_Root (nullptr);
   }

   bool Fabric_Open (const std::string& sUrl)
   {
      bool bResult = true;

      if (!sUrl.empty ())
      {
         if (m_pFabric_Attachment)
            Fabric_Close ();

         m_pFabric_Attachment = new FABRIC (m_pFabric->Scene (), m_pNode);

         if (!m_pFabric_Attachment->Initialize (sUrl))
         {
            delete m_pFabric_Attachment;
            m_pFabric_Attachment = nullptr;

            bResult = false;
         }
      }

      return bResult;
   }

   void Fabric_Close ()
   {
      if (m_pFabric_Attachment)
      {
         delete m_pFabric_Attachment;
         m_pFabric_Attachment = nullptr;
      }
   }

// -----------------------------------------------------------------------
// Texture management
// -----------------------------------------------------------------------

void Texture_Request ()
   {
      if (m_pMap_Object  &&  !m_pMap_Object->m_sUrl_Texture.empty ())
      {
         ENGINE* pEngine = m_pFabric->Scene ()->Engine ();

         std::string sPersonaHash = (pEngine->Persona ()  &&  pEngine->Persona ()->IsLoggedIn ())
            ? pEngine->Persona ()->Hash ()
            : "012PERSONABC";

         CONTAINER::CID CID;
         CID.sFingerprint       = "0123456789FINGERPRINT0123456789FINGERPRINT0123456789FINGERPRINT0";
         CID.sOrganization      = "Metaversal Corporation";
         CID.sOrganizationHash  = "012345abcdef";
         CID.sContainer         = "Solar System";
         CID.sPersonaHash       = sPersonaHash;
         CID.eTrust             = kTRUST_VERIFIED;

         NETWORK* pNetwork = m_pFabric->Scene ()->Network ();
         if (pNetwork)
            m_pFile = pNetwork->File_Open (&CID, m_pMap_Object->m_sUrl_Texture, this);
      }
   }

   void Texture_Release ()
   {
      if (m_pFile)
      {
         m_pFile->Close ();
         m_pFile = nullptr;
      }
   }

   void OnFileReady (NETWORK::FILE* pFile) override
   {
      std::vector<uint8_t> aData;

      if (m_pMap_Object)
         pFile->ReadData (aData);

      pFile->Close ();
      m_pFile = nullptr;

      if (!aData.empty ()  &&  m_pMap_Object)
      {
         int nW = 0, nH = 0, nChannels = 0;
         unsigned char* pPixels = stbi_load_from_memory (aData.data (), static_cast<int> (aData.size ()), &nW, &nH, &nChannels, 4);

         if (pPixels)
         {
            {
               std::lock_guard<std::mutex> lock (m_pMap_Object->m_textureMutex);
               m_pMap_Object->m_aTexturePixels.assign (pPixels, pPixels + nW * nH * 4);
               m_pMap_Object->m_nTextureWidth    = nW;
               m_pMap_Object->m_nTextureHeight   = nH;
               m_pMap_Object->m_nTextureChannels = 4;
            }
            m_pMap_Object->m_bTextureReady.store (true);
            stbi_image_free (pPixels);
         }
      }
   }

   void OnFileFailed (NETWORK::FILE* pFile) override
   {
      pFile->Close ();
      m_pFile = nullptr;
   }

// -----------------------------------------------------------------------
// Accessors
// -----------------------------------------------------------------------

NODE* Parent () const
   {
      NODE* pResult = m_pNode_Parent;

      if (!pResult  &&  m_pFabric)
         pResult = m_pFabric->Node_Attach ();

      return pResult;
   }

   NODE* Child (int nPosition) const
   {
      std::lock_guard<std::mutex> guard (m_mutex_pNode);

      NODE* pResult = nullptr;
      if (nPosition >= 0  &&  nPosition < static_cast<int> (m_apNode.size ()))
         pResult = m_apNode[nPosition];

      return pResult;
   }

   int Node_Count () const
   {
      std::lock_guard<std::mutex> guard (m_mutex_pNode);
      return static_cast<int> (m_apNode.size ());
   }


// -----------------------------------------------------------------------
// Called internally from child nodes
// -----------------------------------------------------------------------

   void Node_Add (NODE* pNode_Child)
   {
      std::lock_guard<std::mutex> guard (m_mutex_pNode);

      m_apNode.push_back (pNode_Child);
   }

   void Node_Remove (NODE* pNode_Child)
   {
      if (pNode_Child)
      {
         std::lock_guard<std::mutex> guard (m_mutex_pNode);

         auto it = std::find (m_apNode.begin (), m_apNode.end (), pNode_Child);
         if (it != m_apNode.end ())
         {
            *it = m_apNode.back ();
            m_apNode.pop_back ();
         }
      }
   }

public:
   FABRIC*                             m_pFabric;
   NODE*                               m_pNode;
   NODE*                               m_pNode_Parent;
   MAP_OBJECT*                         m_pMap_Object;
   FABRIC*                             m_pFabric_Attachment;
   NETWORK::FILE*                      m_pFile;
   SEQLOCK                             m_Seqlock;

   uint32_t                            m_twObjectIx;
   bool                                m_bPrivate;

   std::vector<NODE*>                  m_apNode;
   mutable std::mutex                  m_mutex_pNode;
};

// ---------------------------------------------------------------------------
// NODE
// ---------------------------------------------------------------------------

NODE::NODE (FABRIC* pFabric, NODE* pNode_Parent) :
   m_pImpl (new Impl (this, pFabric, pNode_Parent))
{
}

bool NODE::Initialize (MAP_OBJECT* pMap_Object)
{
   return m_pImpl->Initialize (pMap_Object);
}

NODE::~NODE ()
{
   delete m_pImpl;
   m_pImpl = nullptr;
}

// -----------------------------------------------------------------------
// Accessors
// -----------------------------------------------------------------------

uint32_t    NODE::ObjectIx          ()                    const { return m_pImpl->m_twObjectIx; }
FABRIC*     NODE::Fabric            ()                    const { return m_pImpl->m_pFabric; }
FABRIC*     NODE::Fabric_Attachment ()                    const { return m_pImpl->m_pFabric_Attachment; }

bool        NODE::IsPrivate         ()                    const { return m_pImpl->m_bPrivate; }
MAP_OBJECT* NODE::MapObject         ()                    const { return m_pImpl->m_pMap_Object; }

NODE*       NODE::Parent            ()                    const { return m_pImpl->Parent (); }
NODE*       NODE::Child             (int nPosition)       const { return m_pImpl->Child (nPosition); }
int         NODE::Node_Count        ()                    const { return m_pImpl->Node_Count (); }

// -----------------------------------------------------------------------
// Mutators
// -----------------------------------------------------------------------

void        NODE::Private           (bool bPrivate)             { m_pImpl->m_bPrivate = bPrivate; }

// -----------------------------------------------------------------------
// Called internally from child nodes
// -----------------------------------------------------------------------

void  NODE::Node_Add        (NODE* pNode_Child)         { m_pImpl->Node_Add (pNode_Child); }
void  NODE::Node_Remove     (NODE* pNode_Child)         { m_pImpl->Node_Remove (pNode_Child); }
