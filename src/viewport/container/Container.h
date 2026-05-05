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

#ifndef SNEEZE_VIEWPORT_CONTAINER_H
#define SNEEZE_VIEWPORT_CONTAINER_H

#include "viewport/Viewport.h"
#include <string>

// ---------------------------------------------------------------------------
// SNEEZE::VIEWPORT::CONTAINER — the runtime manifestation of an MSF file.
//
// NAME is the identity record for a container. Uniqueness is determined by
// the tuple (persona hash, fingerprint, container name).
// ---------------------------------------------------------------------------

class SNEEZE::VIEWPORT::CONTAINER
{
public:
   class NAME
   {
   public:
      std::string sFingerprint;
      std::string sOrganization;
      std::string sCommonName;
      std::string sContainerName;
      std::string sPersonaHash;
      bool        bValidated;

      std::string DisplayName () const { return sCommonName + "/" + sContainerName; }
   };
};

#endif // SNEEZE_VIEWPORT_CONTAINER_H
