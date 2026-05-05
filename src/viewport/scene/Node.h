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

#include "Types.h"
#include "scene/Fabric.h"
#include "network/Network.h"
#include <atomic>
#include <mutex>
#include <vector>
#include <unordered_map>
#include <cstdint>

class MAP_OBJECT;

// ---------------------------------------------------------------------------
// SNEEZE::VIEWPORT::SCENE::FABRIC::NODE — structural element in the scene.
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

   uint32_t GetObjectIx () const { return m_twObjectIx; }
   void     SetObjectIx (uint32_t twObjectIx) { m_twObjectIx = twObjectIx; }

   // --- Tree structure ---

   NODE*   Parent () const;
   int     ChildCount () const;
   NODE*   Child (int nPosition) const;
   NODE*   FindChild (uint32_t twObjectIx) const;
   const std::vector<NODE*>& Children () const { return m_apChildren; }

   void    AddChild (NODE* pChild);
   void    RemoveChild (NODE* pChild);

   // --- Fabric (owner — immutable after construction) ---

   FABRIC* Fabric () const { return m_pFabric; }

   // --- Map object ---

   MAP_OBJECT* GetMapObject () const { return m_pMapObject; }
   void        SetMapObject (MAP_OBJECT* pMapObject);

   // --- Attached fabric (this node is an attachment point) ---

   FABRIC* GetAttachedFabric () const { return m_pAttachedFabric; }
   void    SetAttachedFabric (FABRIC* pFabric) { m_pAttachedFabric = pFabric; }

   // --- Flags ---

   bool IsPrivate () const { return m_bPrivate; }
   void SetPrivate (bool bPrivate) { m_bPrivate = bPrivate; }

   bool IsPrimary () const { return m_bPrimary; }
   void SetPrimary (bool bPrimary) { m_bPrimary = bPrimary; }

   // --- Seqlock (protects map object property reads/writes) ---

   SEQLOCK& GetSeqlock () { return m_pSeqlock; }

   // --- SNEEZE::NETWORK::IFILE ---

   void OnFileReady  (SNEEZE::NETWORK::FILE* pFile) override;
   void OnFileFailed (SNEEZE::NETWORK::FILE* pFile) override;

private:
   void RequestTexture ();
   void ReleaseTexture ();

   uint32_t             m_twObjectIx;

   NODE*                m_pParent;
   std::vector<NODE*>   m_apChildren;
   std::unordered_map<uint32_t, NODE*> m_mapChildren;
   mutable std::mutex   m_childMutex;

   FABRIC*              m_pFabric;
   MAP_OBJECT*          m_pMapObject;
   FABRIC*              m_pAttachedFabric;

   bool                 m_bPrivate;
   bool                 m_bPrimary;

   SEQLOCK              m_pSeqlock;

   SNEEZE::NETWORK::FILE*         m_pFile;
};

#endif // SNEEZE_VIEWPORT_NODE_H
