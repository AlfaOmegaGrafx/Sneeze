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

#ifndef SNEEZE_VIEWPORT_SCENE_H
#define SNEEZE_VIEWPORT_SCENE_H

namespace SNEEZE
{
   // ---------------------------------------------------------------------------
   // VIEWPORT::SCENE — root container for the scene object model.
   //
   // Owned by VIEWPORT. Every FABRIC in the scene holds a back-pointer to
   // the SCENE, giving any NODE a path to engine services:
   //     NODE -> FABRIC -> SCENE -> Engine() / Network()
   // ---------------------------------------------------------------------------

   class VIEWPORT::SCENE
   {
   public:
      class FABRIC;

      explicit SCENE (VIEWPORT* pViewport);
      ~SCENE ();

      bool Initialize (const std::string& sUrl);

      ENGINE*   Engine         () const;
      NETWORK*  Network        () const;
      VIEWPORT* Viewport       () const;
      FABRIC*   Fabric_Root    () const;
      FABRIC*   Fabric_Primary () const;

   private:
      bool Fabric_Open_Primary (const std::string& sUrl);

      VIEWPORT*             m_pViewport;
      FABRIC*               m_pFabric_Root;
      FABRIC*               m_pFabric_Primary;
   };
}
#endif // SNEEZE_VIEWPORT_SCENE_H
