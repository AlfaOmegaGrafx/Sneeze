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

#ifndef SNEEZE_VIEWPORT_NODE_H
#define SNEEZE_VIEWPORT_NODE_H

#include "scene/Fabric.h"
#include "scene/MapObject.h"
#include <Sneeze.h>
#include <atomic>
#include <mutex>
#include <vector>
#include <unordered_map>
#include <cstdint>

class MAP_OBJECT;

// ---------------------------------------------------------------------------
// CAS Multi-Writer Seqlock
//
// Protects MAP_OBJECT references on NODEs from concurrent read/write.
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

// ---------------------------------------------------------------------------
// SNEEZE::VIEWPORT::SCENE::FABRIC::NODE -- structural element in the scene.
//
// Each node participates in a tree owned by a single FABRIC. When a
// MAP_OBJECT with a non-empty texture URL is assigned, the node requests the
// texture from the network and decodes it on completion.
// ---------------------------------------------------------------------------

class SNEEZE::VIEWPORT::SCENE::FABRIC::NODE : public SNEEZE::NETWORK::IFILE
{
public:
   explicit NODE (FABRIC* pFabric);
   ~NODE () override;

   // --- Identity ---

   uint32_t ObjectIx () const;
   void     ObjectIx_Set (uint32_t twObjectIx);

   // --- Tree structure ---

   NODE*   Parent () const;
   int     Node_Count () const;
   NODE*   Child (int nPosition) const;
   NODE*   Node_Find (uint32_t twObjectIx) const;
   const std::vector<NODE*>& Node_Children () const;

   void    Node_Add (NODE* pChild);
   void    Node_Remove (NODE* pChild);

   // --- Fabric (owner -- immutable after construction) ---

   FABRIC* Fabric () const;

   // --- Map object ---

   MAP_OBJECT* MapObject () const;
   void        MapObject_Set (MAP_OBJECT* pMapObject);

   // --- Attached fabric (this node is an attachment point) ---

   FABRIC* Fabric_Attached () const;
   void    Fabric_Set_Attached (FABRIC* pFabric);

   // --- Flags ---

   bool IsPrivate () const;
   void SetPrivate (bool bPrivate);

   bool IsPrimary () const;
   void SetPrimary (bool bPrimary);

   // --- Seqlock (protects map object property reads/writes) ---

   SEQLOCK& Seqlock ();

   // --- SNEEZE::NETWORK::IFILE ---

   void OnFileReady  (SNEEZE::NETWORK::FILE* pFile) override;
   void OnFileFailed (SNEEZE::NETWORK::FILE* pFile) override;

private:
   void Texture_Request ();
   void Texture_Release ();

   uint32_t             m_twObjectIx;

   NODE*                m_pNode_Parent;
   std::vector<NODE*>   m_apNode;
   std::unordered_map<uint32_t, NODE*> m_umpNode;
   mutable std::mutex   m_mutex_pNode;

   FABRIC*              m_pFabric;
   MAP_OBJECT*          m_pMapObject;
   FABRIC*              m_pFabric_Attached;

   bool                 m_bPrivate;
   bool                 m_bPrimary;

   SEQLOCK              m_Seqlock;

   SNEEZE::NETWORK::FILE*         m_pFile;
};

#endif // SNEEZE_VIEWPORT_NODE_H
