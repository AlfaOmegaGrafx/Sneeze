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

#ifndef SNEEZE_VIEWPORT_FABRIC_H
#define SNEEZE_VIEWPORT_FABRIC_H

#include <Scene.h>

namespace SNEEZE
{
   // ---------------------------------------------------------------------------
   // SCENE::FABRIC -- a spatial fabric's branch in the scene graph.
   //
   // Each fabric owns a tree of NODEs rooted at m_pNode_Root. Fabrics form their
   // own hierarchy (parent/child) mirroring the attachment relationships in the
   // SOM tree. The attaching node is the NODE in the parent fabric's tree that
   // serves as the attachment point for this fabric.
   // ---------------------------------------------------------------------------

   class SCENE::FABRIC
   {
   public:
      class NODE;

      explicit FABRIC (SCENE* pScene);
      ~FABRIC ();

      // --- Scene (owner -- immutable after construction) ---

      SCENE*  Scene () const;

      // --- Fabric hierarchy ---

      FABRIC* Fabric_Parent () const;
      void    Fabric_Set_Parent (FABRIC* pParent);

      void    Fabric_Add (FABRIC* pChild);
      void    Fabric_Remove (FABRIC* pChild);
      const std::vector<FABRIC*>& Fabric_Children () const;

      // --- Root node (the single root of this fabric's node tree) ---

      NODE*   Node_Root () const;
      void    Node_Set_Root (NODE* pNode);

      // --- Attaching node (the node in the parent fabric where this hangs) ---

      NODE*   Node_Attaching () const;
      void    Node_Set_Attaching (NODE* pNode);

      // --- Owning store (opaque for now; will be WASM::STORE*) ---

      void*   Owner () const;
      void    Owner_Set (void* pOwner);

      // --- Flags ---

      bool    IsPrivate () const;
      void    SetPrivate (bool bPrivate);

      // --- Identity ---

      const std::string& Url () const;
      void  Url_Set (const std::string& sUrl);

   private:
      FABRIC*               m_pFabric_Parent;
      std::vector<FABRIC*>  m_apFabric;

      NODE*                 m_pNode_Root;
      NODE*                 m_pNode_Attaching;
      SCENE*                m_pScene;
      void*                 m_pOwner;

      bool                  m_bPrivate;
      std::string           m_sUrl;
   };
}
#endif // SNEEZE_VIEWPORT_FABRIC_H
