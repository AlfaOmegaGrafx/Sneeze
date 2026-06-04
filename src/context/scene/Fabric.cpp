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
#include <Scene.h>
#include "MapObject.h"
#include "astro/BodyData.h"
#include <algorithm>

using namespace SNEEZE;


// ---------------------------------------------------------------------------
// FABRIC::Impl
// ---------------------------------------------------------------------------

class FABRIC::Impl
{
public:
   Impl (FABRIC* pFabric, SCENE* pScene, NODE* pNode_Attach) :
      m_pFabric        (pFabric),
      m_pScene         (pScene),
      m_pNode_Attach   (pNode_Attach),
      m_pFabric_Parent (pNode_Attach ? pNode_Attach->Fabric () : nullptr),
      m_pNode_Root     (nullptr),
      m_pContainer     (nullptr)
   {
      if (m_pFabric_Parent)
         m_pFabric_Parent->Fabric_Add (m_pFabric);
   }

   bool Initialize (const std::string& sUrl)
   {
      bool bResult = false;

      m_sUrl = sUrl;

      if (m_sUrl == "<primary>")
      {
         auto* pNode_Root = new NODE (m_pFabric, nullptr);

         if (pNode_Root->Initialize (nullptr))
         {
            astro::InjectSolarSystem (m_pFabric);

            bResult = true;
         }
      }

      return bResult;
   }

   ~Impl ()
   {
      if (m_pNode_Root)
      {
         delete m_pNode_Root;
         m_pNode_Root = nullptr;
      }

      if (!m_apFabric.empty ())
         m_pScene->Engine ()->Log (IENGINE::kLOGLEVEL_Error, "FABRIC", "Leaked " + std::to_string (m_apFabric.size ()) + " child fabric(s)");

      if (m_pFabric_Parent)
         m_pFabric_Parent->Fabric_Remove (m_pFabric);
   }

   void Url (const std::string& sUrl)
   {
      // This will reload the fabric with the new URL.

      // Delete nodes from the root
      // Delete the MSF file
      // Open a new MSF file
      // Reset the Container
   }

// -----------------------------------------------------------------------
// Called internally from child fabrics
// -----------------------------------------------------------------------

   void Fabric_Add (FABRIC* pFabric_Child)
   {
      std::lock_guard<std::recursive_mutex> lock (m_mxFabric);
      m_apFabric.push_back (pFabric_Child);
   }

   void Fabric_Remove (FABRIC* pFabric_Child)
   {
      std::lock_guard<std::recursive_mutex> lock (m_mxFabric);

      auto it = std::find (m_apFabric.begin (), m_apFabric.end (), pFabric_Child);
      if (it != m_apFabric.end ())
      {
         (*it)->m_pImpl->m_pFabric_Parent = nullptr;
         m_apFabric.erase (it);
      }
   }

public:
   SCENE*                        m_pScene;
   FABRIC*                       m_pFabric;
   FABRIC*                       m_pFabric_Parent;
   std::vector<FABRIC*>          m_apFabric;
   NODE*                         m_pNode_Root;
   NODE*                         m_pNode_Attach;
   CONTAINER*                    m_pContainer;
   std::string                   m_sUrl;
   mutable std::recursive_mutex  m_mxFabric;
};

// ---------------------------------------------------------------------------
// FABRIC
// ---------------------------------------------------------------------------

FABRIC::FABRIC (SCENE* pScene, NODE* pNode_Attach) :
   m_pImpl (new Impl (this, pScene, pNode_Attach))
{
}

bool FABRIC::Initialize (const std::string& sUrl)
{
   return m_pImpl->Initialize (sUrl);
}

FABRIC::~FABRIC ()
{
   delete m_pImpl;
   m_pImpl = nullptr;
}

// -----------------------------------------------------------------------
// Accessors
// -----------------------------------------------------------------------

SCENE*             FABRIC::Scene          ()                         const { return m_pImpl->m_pScene; }
CONTAINER*         FABRIC::Container      ()                         const { return m_pImpl->m_pContainer; }
FABRIC*            FABRIC::Fabric_Parent  ()                         const { return m_pImpl->m_pFabric_Parent; }
NODE*              FABRIC::Node_Root      ()                         const { return m_pImpl->m_pNode_Root; }
NODE*              FABRIC::Node_Attach    ()                         const { return m_pImpl->m_pNode_Attach; }
const std::string& FABRIC::Url            ()                         const { return m_pImpl->m_sUrl; }

// -----------------------------------------------------------------------
// Mutators
// -----------------------------------------------------------------------

void               FABRIC::Container      (CONTAINER* pContainer)         { m_pImpl->m_pContainer = pContainer; }
void               FABRIC::Node_Root      (NODE* pNode_Root)              { m_pImpl->m_pNode_Root = pNode_Root; }
void               FABRIC::Url            (const std::string& sUrl)       { m_pImpl->Url (sUrl); }

// -----------------------------------------------------------------------
// Called internally from child fabrics
// -----------------------------------------------------------------------

void               FABRIC::Fabric_Add     (FABRIC* pFabric_Child)         { m_pImpl->Fabric_Add (pFabric_Child); }
void               FABRIC::Fabric_Remove  (FABRIC* pFabric_Child)         { m_pImpl->Fabric_Remove (pFabric_Child); }

// ===========================================================================
// FABRIC_ROOT
// ===========================================================================


FABRIC_ROOT::FABRIC_ROOT (SCENE* pScene) :
   FABRIC (pScene, nullptr),
   m_pNode_Primary (nullptr)
{
}

bool FABRIC_ROOT::Initialize (const std::string& sUrl)
{
   bool bResult = false;

   auto* pNode_Root = new NODE (this, nullptr);

   if (pNode_Root->Initialize (nullptr))
   {
      auto* pMap_Object  = new MAP_OBJECT_ROOT ();
      pMap_Object->m_sUrl_Fabric = sUrl;

      m_pNode_Primary = new NODE (this, pNode_Root);

      bResult = m_pNode_Primary->Initialize (pMap_Object);
   }

   return bResult;
}

NODE* FABRIC_ROOT::Node_Primary () const { return m_pNode_Primary; }
