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

#ifndef SNEEZE_SOM_FABRIC_H
#define SNEEZE_SOM_FABRIC_H

#include <vector>
#include <string>

namespace som {

class NODE;
class SCENE;

// ---------------------------------------------------------------------------
// SOM::FABRIC — represents a spatial fabric's branch in the scene graph.
//
// Each fabric owns a tree of NODEs rooted at m_pRootNode. Fabrics form their
// own hierarchy (parent/child) mirroring the attachment relationships in the
// SOM tree. The attaching node is the NODE in the parent fabric's tree that
// serves as the attachment point for this fabric.
// ---------------------------------------------------------------------------

class FABRIC
{
public:
   explicit FABRIC (SCENE* pScene);
   ~FABRIC ();

   // --- Scene (owner — immutable after construction) ---

   SCENE*  Scene () const { return m_pScene; }

   // --- Fabric hierarchy ---

   FABRIC* GetParent () const { return m_pParent; }
   void    SetParent (FABRIC* pParent) { m_pParent = pParent; }

   void    AddChildFabric (FABRIC* pChild);
   void    RemoveChildFabric (FABRIC* pChild);
   const std::vector<FABRIC*>& GetChildren () const { return m_apChildren; }

   // --- Root node (the single root of this fabric's node tree) ---

   NODE*   GetRootNode () const { return m_pRootNode; }
   void    SetRootNode (NODE* pNode) { m_pRootNode = pNode; }

   // --- Attaching node (the node in the parent fabric where this hangs) ---

   NODE*   GetAttachingNode () const { return m_pAttachingNode; }
   void    SetAttachingNode (NODE* pNode) { m_pAttachingNode = pNode; }

   // --- Owning store (opaque for now; will be WASM::STORE*) ---

   void*   GetOwner () const { return m_pOwner; }
   void    SetOwner (void* pOwner) { m_pOwner = pOwner; }

   // --- Flags ---

   bool    IsPrivate () const { return m_bPrivate; }
   void    SetPrivate (bool bPrivate) { m_bPrivate = bPrivate; }

   // --- Identity ---

   const std::string& GetUrl () const { return m_sUrl; }
   void  SetUrl (const std::string& sUrl) { m_sUrl = sUrl; }

private:
   FABRIC*               m_pParent;
   std::vector<FABRIC*>  m_apChildren;

   NODE*                 m_pRootNode;
   NODE*                 m_pAttachingNode;
   SCENE*                m_pScene;
   void*                 m_pOwner;

   bool                  m_bPrivate;
   std::string           m_sUrl;
};

} // namespace som

#endif // SNEEZE_SOM_FABRIC_H
