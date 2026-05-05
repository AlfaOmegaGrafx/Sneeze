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

#ifndef SNEEZE_SOM_SPATIALINDEX_H
#define SNEEZE_SOM_SPATIALINDEX_H

#include <vector>
#include <cstdint>

namespace som {

class NODE;

// ---------------------------------------------------------------------------
// AABB — axis-aligned bounding box
// ---------------------------------------------------------------------------

struct AABB
{
   double dMinX, dMinY, dMinZ;
   double dMaxX, dMaxY, dMaxZ;

   bool Contains (double x, double y, double z) const;
   bool Intersects (const AABB& other) const;
   void Expand (const AABB& other);
   void Expand (double x, double y, double z, double dRadius);
};

// ---------------------------------------------------------------------------
// FRUSTUM — six-plane view frustum for culling
// ---------------------------------------------------------------------------

struct FRUSTUM_PLANE
{
   double dA, dB, dC, dD;
};

struct FRUSTUM
{
   FRUSTUM_PLANE aPlanes[6];

   bool TestSphere (double x, double y, double z, double dRadius) const;
   bool TestAABB (const AABB& pBox) const;
};

// ---------------------------------------------------------------------------
// BVH_NODE — internal node of the bounding volume hierarchy
// ---------------------------------------------------------------------------

struct BVH_NODE
{
   AABB     pBounds;
   int      nLeft;
   int      nRight;
   int      nLeafIndex;

   bool IsLeaf () const { return nLeafIndex >= 0; }
};

// ---------------------------------------------------------------------------
// SPATIAL_INDEX — bounding volume hierarchy built from SOM nodes.
//
// Rebuilt periodically (or on demand) from the current SOM state. Supports
// frustum culling (return all visible nodes) and proximity queries (return
// all nodes within a sphere).
// ---------------------------------------------------------------------------

class SPATIAL_INDEX
{
public:
   SPATIAL_INDEX ();
   ~SPATIAL_INDEX ();

   // --- Build/rebuild from a flat list of nodes ---

   void Build (const std::vector<NODE*>& apNodes);
   void Clear ();

   // --- Queries ---

   void QueryFrustum (const FRUSTUM& pFrustum, std::vector<NODE*>& aResults) const;
   void QuerySphere (double x, double y, double z, double dRadius, std::vector<NODE*>& aResults) const;

   int  GetNodeCount () const { return static_cast<int> (m_apLeaves.size ()); }

private:
   int  BuildRecursive (std::vector<int>& aIndices, int nStart, int nEnd);
   AABB ComputeBounds (const std::vector<int>& aIndices, int nStart, int nEnd) const;
   void QueryFrustumRecursive (int nBvhIndex, const FRUSTUM& pFrustum, std::vector<NODE*>& aResults) const;
   void QuerySphereRecursive (int nBvhIndex, double x, double y, double z, double dRadius, std::vector<NODE*>& aResults) const;

   std::vector<BVH_NODE>  m_aBvhNodes;
   std::vector<NODE*>     m_apLeaves;
   std::vector<AABB>      m_aLeafBounds;
   int                    m_nRootIndex;
};

} // namespace som

#endif // SNEEZE_SOM_SPATIALINDEX_H
