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

#ifndef SNEEZE_CONTAINER_H
#define SNEEZE_CONTAINER_H

#include <string>

namespace som {

// ---------------------------------------------------------------------------
// CONTAINER — the runtime manifestation of an MSF file.
//
// A container wraps WASM modules, SPIR-V shaders, storage units, SOM objects,
// and map data loaded from a single MSF package.
//
// NAME is the identity record for a container.  Uniqueness is determined by
// the tuple (persona hash, fingerprint, container name).  Organization and
// common name are carried for display purposes.
//
//   - For authenticated certificates (bValidated == true), sOrganization and
//     sCommonName come from the certificate's O and CN fields.
//   - For unauthenticated certificates, both are set to the first 16
//     characters of the fingerprint.
//
// The display name is always sCommonName + "/" + sContainerName
// (e.g. "Metaversal/Solar System").
//
// NAME objects are lightweight identity cards — subsystems like the network and
// storage hold their own copies, independent of the container's lifecycle.
// ---------------------------------------------------------------------------

class CONTAINER
{
public:
   class NAME
   {
   public:
      std::string sFingerprint;      // SHA-256 of cert public key (always present)
      std::string sOrganization;     // from cert O field, or fingerprint[0..15] if !bValidated
      std::string sCommonName;       // from cert CN field, or fingerprint[0..15] if !bValidated
      std::string sContainerName;    // from MSF payload
      std::string sPersonaHash;      // from persona
      bool        bValidated;        // whether cert chain validated to a trusted root

      std::string DisplayName () const { return sCommonName + "/" + sContainerName; }
   };
};

} // namespace som

#endif // SNEEZE_CONTAINER_H
