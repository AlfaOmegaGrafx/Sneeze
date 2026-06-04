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

#ifndef SNEEZE_SCENE_H
#define SNEEZE_SCENE_H

namespace SNEEZE
{
   class ENGINE;
   class CONTEXT;
   class CONTAINER;
   class NETWORK;
   class MAP_OBJECT;
   class SCENE;
   class FABRIC;

   // ---------------------------------------------------------------------------
   // NODE -- structural element in the scene.
   //
   // Each node participates in a tree owned by a single FABRIC. When a
   // MAP_OBJECT with a non-empty texture URL is assigned, the node requests the
   // texture from the network and decodes it on completion.
   // ---------------------------------------------------------------------------

   class NODE
   {
   public:
      NODE (FABRIC* pFabric, NODE* pNode_Parent);
      ~NODE ();

      bool Initialize (MAP_OBJECT* pMapObject);

      // --- Identity ---

      uint32_t    ObjectIx          () const;
      MAP_OBJECT* MapObject         () const;
      FABRIC*     Fabric            () const;
      FABRIC*     Fabric_Attachment () const;

      // --- Tree structure ---

      NODE* Parent      () const;
      NODE* Child       (int nPosition) const;
      int   Node_Count  () const;

      void  Node_Add    (NODE* pNode_Child);
      void  Node_Remove (NODE* pNode_Child);

      // --- Flags ---

      bool IsPrivate () const;
      void Private   (bool bPrivate);

   private:
      class Impl;
      Impl* m_pImpl;
   };

   // ---------------------------------------------------------------------------
   // FABRIC -- a spatial fabric's branch in the scene graph.
   //
   // Each fabric owns a tree of NODEs rooted at Node_Root(). Fabrics form their
   // own hierarchy (parent/child) mirroring the attachment relationships in the
   // SOM tree. The attaching node is the NODE in the parent fabric's tree that
   // serves as the attachment point for this fabric.
   // ---------------------------------------------------------------------------

   class FABRIC
   {
   public:
      FABRIC (SCENE* pScene, NODE* pNode_Attach);
      virtual ~FABRIC ();

      bool Initialize (const std::string& sUrl);

      SCENE*  Scene () const;

      FABRIC* Fabric_Parent () const;
      void    Fabric_Add (FABRIC* pFabric_Child);
      void    Fabric_Remove (FABRIC* pFabric_Child);

      NODE*   Node_Root () const;
      void    Node_Root (NODE* pNode_Root);

      NODE*   Node_Attach () const;

      CONTAINER* Container () const;
      void       Container (CONTAINER* pContainer);

      const std::string& Url () const;
      void               Url (const std::string& sUrl);

   protected:
      class Impl;
      Impl* m_pImpl;
   };

   // ---------------------------------------------------------------------------
   // FABRIC_ROOT -- the structural root fabric.
   //
   // Derived from FABRIC. Creates the root node tree with named attachment
   // points for the primary fabric and (future) overlay fabrics. Has no URL,
   // no MSF, no container.
   // ---------------------------------------------------------------------------

   class FABRIC_ROOT : public FABRIC
   {
   public:
      FABRIC_ROOT (SCENE* pScene);

      bool  Initialize (const std::string& sUrl);

      NODE* Node_Primary () const;

   private:
      NODE* m_pNode_Primary;
   };

   // ---------------------------------------------------------------------------
   // SCENE -- root container for the scene object model.
   //
   // Owned by CONTEXT. Every FABRIC in the scene holds a back-pointer to
   // the SCENE, giving any NODE a path to engine services:
   //     NODE -> FABRIC -> SCENE -> Engine() / Network()
   // ---------------------------------------------------------------------------

   class SCENE
   {
   public:
      explicit SCENE (CONTEXT* pContext);
      ~SCENE ();

      bool Initialize (const std::string& sUrl);
      void Url (const std::string& sUrl);

      ENGINE*       Engine         () const;
      CONTEXT*      Context        () const;
      NETWORK*      Network        () const;

      FABRIC_ROOT*  Fabric_Root    () const;
      FABRIC*       Fabric_Primary () const;
      
   private:
      CONTEXT*     m_pContext;
      FABRIC_ROOT* m_pFabric_Root;
   };
}
#endif // SNEEZE_SCENE_H
