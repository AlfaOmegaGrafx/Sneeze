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

#include "Node.h"
#include "Fabric.h"
#include "Scene.h"
#include "MapObject.h"
#include "Sneeze.h"
#include "container/Container.h"
#include "stb/stb_image.h"
#include <algorithm>
#include <memory>

namespace som {

NODE::NODE (FABRIC* pFabric)
   : m_twObjectIx (0)
   , m_pParent (nullptr)
   , m_pFabric (pFabric)
   , m_pMapObject (nullptr)
   , m_pAttachedFabric (nullptr)
   , m_bPrivate (false)
   , m_bPrimary (false)
   , m_pFile (nullptr)
{
}

NODE::~NODE ()
{
   ReleaseTexture ();
}

// ---------------------------------------------------------------------------
// Parent () — returns the logical parent, crossing fabric boundaries.
//
// If this node is the root of its fabric (parent is null within the fabric),
// we traverse upward: fabric -> attaching node in the parent fabric.
// ---------------------------------------------------------------------------

NODE* NODE::Parent () const
{
   if (m_pParent)
      return m_pParent;

   if (m_pFabric)
   {
      NODE* pAttaching = m_pFabric->GetAttachingNode ();
      if (pAttaching)
         return pAttaching;
   }

   return nullptr;
}

int NODE::ChildCount () const
{
   std::lock_guard<std::mutex> guard (m_childMutex);
   return static_cast<int> (m_apChildren.size ());
}

NODE* NODE::Child (int nPosition) const
{
   std::lock_guard<std::mutex> guard (m_childMutex);
   if (nPosition < 0  ||  nPosition >= static_cast<int> (m_apChildren.size ()))
      return nullptr;
   return m_apChildren[nPosition];
}

NODE* NODE::FindChild (uint32_t twObjectIx) const
{
   std::lock_guard<std::mutex> guard (m_childMutex);
   auto it = m_mapChildren.find (twObjectIx);
   if (it != m_mapChildren.end ())
      return it->second;
   return nullptr;
}

// ---------------------------------------------------------------------------
// AddChild — appends to the child vector and inserts into the lookup map.
// ---------------------------------------------------------------------------

void NODE::AddChild (NODE* pChild)
{
   std::lock_guard<std::mutex> guard (m_childMutex);
   pChild->m_pParent = this;
   m_apChildren.push_back (pChild);
   m_mapChildren[pChild->m_twObjectIx] = pChild;
}

// ---------------------------------------------------------------------------
// RemoveChild — removes from both the vector (swap-and-pop) and the map.
// ---------------------------------------------------------------------------

void NODE::RemoveChild (NODE* pChild)
{
   if (!pChild)
      return;

   std::lock_guard<std::mutex> guard (m_childMutex);
   m_mapChildren.erase (pChild->m_twObjectIx);

   auto it = std::find (m_apChildren.begin (), m_apChildren.end (), pChild);
   if (it != m_apChildren.end ())
   {
      (*it)->m_pParent = nullptr;
      *it = m_apChildren.back ();
      m_apChildren.pop_back ();
   }
}

// ---------------------------------------------------------------------------
// SetMapObject — assigns the map object and initiates a texture fetch if the
// object has a texture URL and the node can reach the network.
// ---------------------------------------------------------------------------

void NODE::SetMapObject (MAP_OBJECT* pMapObject)
{
   ReleaseTexture ();
   m_pMapObject = pMapObject;
   RequestTexture ();
}

// ---------------------------------------------------------------------------
// RequestTexture — if the map object has a texture URL, request it from the
// network via NODE -> FABRIC -> SCENE -> SNEEZE -> Network().
// ---------------------------------------------------------------------------

void NODE::RequestTexture ()
{
   if (m_pMapObject  &&  !m_pMapObject->m_sTextureUrl.empty ())
   {
      auto pName = std::make_shared<CONTAINER::NAME> ();
      pName->sFingerprint   = "5YTB6YjNQWnpBTkJna3Foa2lHOXcwQkFRc0ZBQU9DQVFFQWxrVFR0Z0pTWXRoMDJ";
      pName->sOrganization  = "Metaversal Corporation";
      pName->sCommonName    = "Metaversal";
      pName->sContainerName = "Solar System";
      pName->sPersonaHash   = "ZklkNVZTY0cxb2ZqUmtTWGpMVHE2bHkyQT09IiwiTUlJRFBUQ0NBaVdnQXdJQkFn";
      pName->bValidated     = true;

      m_pFile = m_pFabric->Scene ()->Sneeze ()->Network ()->Request (this, pName, m_pMapObject->m_sTextureUrl);
   }
}

// ---------------------------------------------------------------------------
// ReleaseTexture — release any outstanding network file handle.
// ---------------------------------------------------------------------------

void NODE::ReleaseTexture ()
{
   if (m_pFile)
   {
      m_pFile->Release ();
      m_pFile = nullptr;
   }
}

// ---------------------------------------------------------------------------
// OnFileReady — decode the fetched texture data and populate the map object.
// ---------------------------------------------------------------------------

void NODE::OnFileReady (SNEEZE::NETWORK::FILE* pFile)
{
   std::vector<uint8_t> aData;

   if (m_pMapObject)
      aData = pFile->ReadData ();

   pFile->Release ();
   m_pFile = nullptr;

   if (!aData.empty ()  &&  m_pMapObject)
   {
      int nW = 0, nH = 0, nChannels = 0;
      unsigned char* pPixels = stbi_load_from_memory (
         aData.data (),
         static_cast<int> (aData.size ()),
         &nW, &nH, &nChannels, 4);

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
// OnFileFailed — release the network file handle.
// ---------------------------------------------------------------------------

void NODE::OnFileFailed (SNEEZE::NETWORK::FILE* pFile)
{
   pFile->Release ();
   m_pFile = nullptr;
}

} // namespace som
