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
   class MSF;
   class NETWORK;
   class MAP_OBJECT;
   class SCENE;
   class FABRIC;

   // ---------------------------------------------------------------------------
   // RMAP Object Index constants
   //
   // Object indices are 48-bit values (TWORD) stored in a uint64_t. The upper
   // 16 bits of the containing QWORD may carry a packed class discriminator
   // (OBJECTIX union), but the object index itself is always in the low 48.
   // ---------------------------------------------------------------------------

   static constexpr uint64_t TWORD_MAX        = 0x0000FFFFFFFFFFFFull;
   static constexpr uint64_t OBJECTIX_MAX     = 0x0000FFFFFFFFFFFCull;
   static constexpr uint64_t OBJECTIX_LAST    = 0x0000FFFFFFFFFFFDull;
   static constexpr uint64_t OBJECTIX_ERROR   = 0x0000FFFFFFFFFFFeull;
   static constexpr uint64_t OBJECTIX_INVALID = 0x0000FFFFFFFFFFFFull;
   static constexpr uint64_t OBJECTIX_IDENTITY= 0x0000FFFFFFFFFFFFull;
   static constexpr uint64_t OBJECTIX_NULL    = 0x0000000000000000ull;

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
      NODE  (FABRIC* pFabric, NODE* pNode_Parent);
      ~NODE ();

      bool               Initialize        (MAP_OBJECT* pMapObject);

      // Accessors
      uint64_t           ObjectIx          () const;
      MAP_OBJECT*        MapObject         () const;
      FABRIC*            Fabric            () const;
      FABRIC*            Fabric_Attachment () const;
      NODE*              Parent            () const;
      NODE*              Child             (int nPosition) const;
      int                Node_Count        () const;
      bool               IsPrivate         () const;

      // Mutators
      void               ObjectIx          (uint64_t twObjectIx);
      void               Private           (bool bPrivate);

      // Methods
      void               Node_Add          (NODE* pNode_Child);
      void               Node_Remove       (NODE* pNode_Child);

   private:
      class Impl;
      Impl*              m_pImpl;
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
      FABRIC  (SCENE* pScene, NODE* pNode_Attach);
      virtual ~FABRIC ();

      bool               Initialize     (const std::string& sUrl);

      // Accessors
      SCENE*             Scene          () const;
      FABRIC*            Fabric_Parent  () const;
      NODE*              Node_Root      () const;
      NODE*              Node_Attach    () const;
      CONTAINER*         Container      () const;
      uint64_t           FabricIx       () const;
      MSF*               Msf            () const;
      const std::string& Url            () const;

      // Mutators
      void               Node_Root      (NODE* pNode_Root);
      void               Container      (CONTAINER* pContainer);
      void               FabricIx       (uint64_t twFabricIx);
      void               Url            (const std::string& sUrl);

      // Methods
      void               Fabric_Add     (FABRIC* pFabric_Child);
      void               Fabric_Remove  (FABRIC* pFabric_Child);
      void               OnMsfReady     (NETWORK::FILE* pFile);
      void               OnMsfFailed    (NETWORK::FILE* pFile);
      void               OnWasmReady    (NETWORK::FILE* pFile, const std::string& sUrl, const std::string& sHash);
      void               OnWasmFailed   (NETWORK::FILE* pFile, const std::string& sUrl);

   protected:
      class Impl;
      Impl*              m_pImpl;
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

      bool               Initialize    (const std::string& sUrl);

      // Accessors
      NODE*              Node_Primary  () const;

   private:
      NODE*              m_pNode_Primary;
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

      bool               Initialize      (const std::string& sUrl);

      // Accessors
      ENGINE*            Engine          () const;
      CONTEXT*           Context         () const;
      NETWORK*           Network         () const;
      FABRIC_ROOT*       Fabric_Root     () const;
      FABRIC*            Fabric_Primary  () const;

      // Mutators
      void               Url             (const std::string& sUrl);

   private:
      CONTEXT*           m_pContext;
      FABRIC_ROOT*       m_pFabric_Root;
   };
}
#endif // SNEEZE_SCENE_H
