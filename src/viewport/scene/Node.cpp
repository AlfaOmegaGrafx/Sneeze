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
#include "Node.h"
#include "Fabric.h"
#include "Scene.h"
#include "MapObject.h"
#include <Container.h>
#include "stb/stb_image.h"
#include <algorithm>
#include <memory>

using namespace SNEEZE;

using FABRIC    = VIEWPORT::SCENE::FABRIC;
using NODE      = VIEWPORT::SCENE::FABRIC::NODE;
using CONTAINER = SNEEZE::CONTEXT::CONTAINER;

// ---------------------------------------------------------------------------
// SEQLOCK
// ---------------------------------------------------------------------------

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
// NODE
// ---------------------------------------------------------------------------

NODE::NODE (FABRIC* pFabric) :
   m_twObjectIx (0),
   m_pNode_Parent (nullptr),
   m_pFabric (pFabric),
   m_pMapObject (nullptr),
   m_pFabric_Attached (nullptr),
   m_bPrivate (false),
   m_bPrimary (false),
   m_pFile (nullptr)
{
}

NODE::~NODE ()
{
   Texture_Release ();
}

// --- Accessors ---

uint32_t NODE::ObjectIx () const                        { return m_twObjectIx; }
void     NODE::ObjectIx_Set (uint32_t twObjectIx)        { m_twObjectIx = twObjectIx; }
FABRIC*  NODE::Fabric () const                          { return m_pFabric; }
const std::vector<NODE*>& NODE::Node_Children () const       { return m_apNode; }
FABRIC*  NODE::Fabric_Attached () const                 { return m_pFabric_Attached; }
void     NODE::Fabric_Set_Attached (FABRIC* pFabric)    { m_pFabric_Attached = pFabric; }
bool     NODE::IsPrivate () const                       { return m_bPrivate; }
void     NODE::SetPrivate (bool bPrivate)               { m_bPrivate = bPrivate; }
bool     NODE::IsPrimary () const                       { return m_bPrimary; }
void     NODE::SetPrimary (bool bPrimary)               { m_bPrimary = bPrimary; }
SEQLOCK& NODE::Seqlock ()                               { return m_Seqlock; }
MAP_OBJECT* NODE::MapObject () const                    { return m_pMapObject; }

// ---------------------------------------------------------------------------
// Parent () -- returns the logical parent, crossing fabric boundaries.
//
// If this node is the root of its fabric (parent is null within the fabric),
// we traverse upward: fabric -> attaching node in the parent fabric.
// ---------------------------------------------------------------------------

NODE* NODE::Parent () const
{
   NODE* pResult = m_pNode_Parent;

   if (!pResult  &&  m_pFabric)
      pResult = m_pFabric->Node_Attaching ();

   return pResult;
}

int NODE::Node_Count () const
{
   std::lock_guard<std::mutex> guard (m_mutex_pNode);
   return static_cast<int> (m_apNode.size ());
}

NODE* NODE::Child (int nPosition) const
{
   std::lock_guard<std::mutex> guard (m_mutex_pNode);

   NODE* pResult = nullptr;
   if (nPosition >= 0  &&  nPosition < static_cast<int> (m_apNode.size ()))
      pResult = m_apNode[nPosition];

   return pResult;
}

NODE* NODE::Node_Find (uint32_t twObjectIx) const
{
   std::lock_guard<std::mutex> guard (m_mutex_pNode);

   NODE* pResult = nullptr;
   auto it = m_umpNode.find (twObjectIx);
   if (it != m_umpNode.end ())
      pResult = it->second;

   return pResult;
}

// ---------------------------------------------------------------------------
// Node_Add -- appends to the child vector and inserts into the lookup map.
// ---------------------------------------------------------------------------

void NODE::Node_Add (NODE* pChild)
{
   std::lock_guard<std::mutex> guard (m_mutex_pNode);
   pChild->m_pNode_Parent = this;
   m_apNode.push_back (pChild);
   m_umpNode[pChild->m_twObjectIx] = pChild;
}

// ---------------------------------------------------------------------------
// Node_Remove -- removes from both the vector (swap-and-pop) and the map.
// ---------------------------------------------------------------------------

void NODE::Node_Remove (NODE* pChild)
{
   if (pChild)
   {
      std::lock_guard<std::mutex> guard (m_mutex_pNode);
      m_umpNode.erase (pChild->m_twObjectIx);

      auto it = std::find (m_apNode.begin (), m_apNode.end (), pChild);
      if (it != m_apNode.end ())
      {
         (*it)->m_pNode_Parent = nullptr;
         *it = m_apNode.back ();
         m_apNode.pop_back ();
      }
   }
}

// ---------------------------------------------------------------------------
// MapObject_Set -- assigns the map object and initiates a texture fetch if the
// object has a texture URL and the node can reach the network.
// ---------------------------------------------------------------------------

void NODE::MapObject_Set (MAP_OBJECT* pMapObject)
{
   Texture_Release ();
   m_pMapObject = pMapObject;
   Texture_Request ();
}

// ---------------------------------------------------------------------------
// Texture_Request -- if the map object has a texture URL, request it from the
// network via NODE -> FABRIC -> SCENE -> Network().
// ---------------------------------------------------------------------------

void NODE::Texture_Request ()
{
   if (m_pMapObject  &&  !m_pMapObject->m_sTextureUrl.empty ())
   {
      ENGINE* pEngine = m_pFabric->Scene ()->Engine ();

      std::string sPersonaHash = (pEngine->Persona ()  &&  pEngine->Persona ()->IsLoggedIn ())
         ? pEngine->Persona ()->Hash ()
         : "012PERSONABC";

      CONTAINER::CID CID;
      CID.sFingerprint   = "0123456789FINGERPRINT0123456789FINGERPRINT0123456789FINGERPRINT0";
      CID.sOrganization  = "Metaversal Corporation";
      CID.sCommonName    = "Metaversal";
      CID.sContainerName = "Solar System";
      CID.sPersonaHash   = sPersonaHash;
      CID.bValidated     = true;

      NETWORK* pNetwork = m_pFabric->Scene ()->Network ();
      if (pNetwork)
         m_pFile = pNetwork->File_Open (&CID, m_pMapObject->m_sTextureUrl, this);
   }
}

// ---------------------------------------------------------------------------
// Texture_Release -- release any outstanding network file handle.
// ---------------------------------------------------------------------------

void NODE::Texture_Release ()
{
   if (m_pFile)
   {
      m_pFile->Close ();
      m_pFile = nullptr;
   }
}

// ---------------------------------------------------------------------------
// OnFileReady -- decode the fetched texture data and populate the map object.
// ---------------------------------------------------------------------------

void NODE::OnFileReady (NETWORK::FILE* pFile)
{
   std::vector<uint8_t> aData;

   if (m_pMapObject)
      pFile->ReadData (aData);

   pFile->Close ();
   m_pFile = nullptr;

   if (!aData.empty ()  &&  m_pMapObject)
   {
      int nW = 0, nH = 0, nChannels = 0;
      unsigned char* pPixels = stbi_load_from_memory (aData.data (), static_cast<int> (aData.size ()), &nW, &nH, &nChannels, 4);

      if (pPixels)
      {
         {
            std::lock_guard<std::mutex> lock (m_pMapObject->m_textureMutex);
            m_pMapObject->m_aTexturePixels.assign (pPixels, pPixels + nW * nH * 4);
            m_pMapObject->m_nTextureWidth    = nW;
            m_pMapObject->m_nTextureHeight   = nH;
            m_pMapObject->m_nTextureChannels = 4;
         }
         m_pMapObject->m_bTextureReady.store (true);
         stbi_image_free (pPixels);
      }
   }
}

// ---------------------------------------------------------------------------
// OnFileFailed -- release the network file handle.
// ---------------------------------------------------------------------------

void NODE::OnFileFailed (NETWORK::FILE* pFile)
{
   pFile->Close ();
   m_pFile = nullptr;
}
