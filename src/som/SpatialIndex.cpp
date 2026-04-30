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

#include "SpatialIndex.h"
#include "Node.h"
#include "MapObject.h"
#include <algorithm>
#include <cmath>
#include <limits>

namespace sneeze { namespace som {

// ===========================================================================
// AABB
// ===========================================================================

bool AABB::Contains (double x, double y, double z) const
{
   return x >= dMinX  &&  x <= dMaxX  &&
          y >= dMinY  &&  y <= dMaxY  &&
          z >= dMinZ  &&  z <= dMaxZ;
}

bool AABB::Intersects (const AABB& other) const
{
   return dMinX <= other.dMaxX  &&  dMaxX >= other.dMinX  &&
          dMinY <= other.dMaxY  &&  dMaxY >= other.dMinY  &&
          dMinZ <= other.dMaxZ  &&  dMaxZ >= other.dMinZ;
}

void AABB::Expand (const AABB& other)
{
   if (other.dMinX < dMinX) dMinX = other.dMinX;
   if (other.dMinY < dMinY) dMinY = other.dMinY;
   if (other.dMinZ < dMinZ) dMinZ = other.dMinZ;
   if (other.dMaxX > dMaxX) dMaxX = other.dMaxX;
   if (other.dMaxY > dMaxY) dMaxY = other.dMaxY;
   if (other.dMaxZ > dMaxZ) dMaxZ = other.dMaxZ;
}

void AABB::Expand (double x, double y, double z, double dRadius)
{
   double dLo_X = x - dRadius, dHi_X = x + dRadius;
   double dLo_Y = y - dRadius, dHi_Y = y + dRadius;
   double dLo_Z = z - dRadius, dHi_Z = z + dRadius;
   if (dLo_X < dMinX) dMinX = dLo_X;
   if (dLo_Y < dMinY) dMinY = dLo_Y;
   if (dLo_Z < dMinZ) dMinZ = dLo_Z;
   if (dHi_X > dMaxX) dMaxX = dHi_X;
   if (dHi_Y > dMaxY) dMaxY = dHi_Y;
   if (dHi_Z > dMaxZ) dMaxZ = dHi_Z;
}

// ===========================================================================
// FRUSTUM
// ===========================================================================

bool FRUSTUM::TestSphere (double x, double y, double z, double dRadius) const
{
   for (int i = 0; i < 6; i++)
   {
      double dDist = aPlanes[i].dA * x + aPlanes[i].dB * y + aPlanes[i].dC * z + aPlanes[i].dD;
      if (dDist < -dRadius)
         return false;
   }
   return true;
}

bool FRUSTUM::TestAABB (const AABB& pBox) const
{
   for (int i = 0; i < 6; i++)
   {
      double dPx = (aPlanes[i].dA > 0) ? pBox.dMaxX : pBox.dMinX;
      double dPy = (aPlanes[i].dB > 0) ? pBox.dMaxY : pBox.dMinY;
      double dPz = (aPlanes[i].dC > 0) ? pBox.dMaxZ : pBox.dMinZ;
      double dDist = aPlanes[i].dA * dPx + aPlanes[i].dB * dPy + aPlanes[i].dC * dPz + aPlanes[i].dD;
      if (dDist < 0)
         return false;
   }
   return true;
}

// ===========================================================================
// SPATIAL_INDEX
// ===========================================================================

SPATIAL_INDEX::SPATIAL_INDEX ()
   : m_nRootIndex (-1)
{
}

SPATIAL_INDEX::~SPATIAL_INDEX ()
{
}

void SPATIAL_INDEX::Clear ()
{
   m_aBvhNodes.clear ();
   m_apLeaves.clear ();
   m_aLeafBounds.clear ();
   m_nRootIndex = -1;
}

// ---------------------------------------------------------------------------
// Build — constructs the BVH from a flat list of nodes with map objects.
// ---------------------------------------------------------------------------

void SPATIAL_INDEX::Build (const std::vector<NODE*>& apNodes)
{
   Clear ();

   for (auto* pNode : apNodes)
   {
      MAP_OBJECT* pObj = pNode->GetMapObject ();
      if (!pObj) continue;

      AABB pBounds;
      double dR = pObj->m_dBound > 0 ? pObj->m_dBound : pObj->m_dScale;
      pBounds.dMinX = pObj->m_dPosX - dR;
      pBounds.dMinY = pObj->m_dPosY - dR;
      pBounds.dMinZ = pObj->m_dPosZ - dR;
      pBounds.dMaxX = pObj->m_dPosX + dR;
      pBounds.dMaxY = pObj->m_dPosY + dR;
      pBounds.dMaxZ = pObj->m_dPosZ + dR;

      m_apLeaves.push_back (pNode);
      m_aLeafBounds.push_back (pBounds);
   }

   if (m_apLeaves.empty ())
      return;

   std::vector<int> aIndices (m_apLeaves.size ());
   for (int i = 0; i < static_cast<int> (aIndices.size ()); i++)
      aIndices[i] = i;

   m_nRootIndex = BuildRecursive (aIndices, 0, static_cast<int> (aIndices.size ()));
}

int SPATIAL_INDEX::BuildRecursive (std::vector<int>& aIndices, int nStart, int nEnd)
{
   int nCount = nEnd - nStart;

   BVH_NODE pBvh;
   pBvh.pBounds    = ComputeBounds (aIndices, nStart, nEnd);
   pBvh.nLeft      = -1;
   pBvh.nRight     = -1;
   pBvh.nLeafIndex = -1;

   if (nCount == 1)
   {
      pBvh.nLeafIndex = aIndices[nStart];
      int nIndex = static_cast<int> (m_aBvhNodes.size ());
      m_aBvhNodes.push_back (pBvh);
      return nIndex;
   }

   // Split along the longest axis at the midpoint
   AABB& b = pBvh.pBounds;
   double dExtX = b.dMaxX - b.dMinX;
   double dExtY = b.dMaxY - b.dMinY;
   double dExtZ = b.dMaxZ - b.dMinZ;

   int nAxis = 0;
   if (dExtY > dExtX  &&  dExtY > dExtZ) nAxis = 1;
   else if (dExtZ > dExtX) nAxis = 2;

   int nMid = (nStart + nEnd) / 2;
   std::nth_element (aIndices.begin () + nStart, aIndices.begin () + nMid, aIndices.begin () + nEnd,
      [this, nAxis] (int a, int b)
      {
         const AABB& ba = m_aLeafBounds[a];
         const AABB& bb = m_aLeafBounds[b];
         double dCenterA = 0, dCenterB = 0;
         if (nAxis == 0) { dCenterA = (ba.dMinX + ba.dMaxX) * 0.5; dCenterB = (bb.dMinX + bb.dMaxX) * 0.5; }
         else if (nAxis == 1) { dCenterA = (ba.dMinY + ba.dMaxY) * 0.5; dCenterB = (bb.dMinY + bb.dMaxY) * 0.5; }
         else { dCenterA = (ba.dMinZ + ba.dMaxZ) * 0.5; dCenterB = (bb.dMinZ + bb.dMaxZ) * 0.5; }
         return dCenterA < dCenterB;
      });

   int nIndex = static_cast<int> (m_aBvhNodes.size ());
   m_aBvhNodes.push_back (pBvh);

   m_aBvhNodes[nIndex].nLeft  = BuildRecursive (aIndices, nStart, nMid);
   m_aBvhNodes[nIndex].nRight = BuildRecursive (aIndices, nMid, nEnd);

   return nIndex;
}

AABB SPATIAL_INDEX::ComputeBounds (const std::vector<int>& aIndices, int nStart, int nEnd) const
{
   AABB pResult;
   pResult.dMinX = pResult.dMinY = pResult.dMinZ =  std::numeric_limits<double>::max ();
   pResult.dMaxX = pResult.dMaxY = pResult.dMaxZ = -std::numeric_limits<double>::max ();

   for (int i = nStart; i < nEnd; i++)
      pResult.Expand (m_aLeafBounds[aIndices[i]]);

   return pResult;
}

// ---------------------------------------------------------------------------
// QueryFrustum — returns all leaf nodes whose bounds intersect the frustum.
// ---------------------------------------------------------------------------

void SPATIAL_INDEX::QueryFrustum (const FRUSTUM& pFrustum, std::vector<NODE*>& aResults) const
{
   if (m_nRootIndex < 0)
      return;
   QueryFrustumRecursive (m_nRootIndex, pFrustum, aResults);
}

void SPATIAL_INDEX::QueryFrustumRecursive (int nBvhIndex, const FRUSTUM& pFrustum, std::vector<NODE*>& aResults) const
{
   const BVH_NODE& pBvh = m_aBvhNodes[nBvhIndex];

   if (!pFrustum.TestAABB (pBvh.pBounds))
      return;

   if (pBvh.IsLeaf ())
   {
      aResults.push_back (m_apLeaves[pBvh.nLeafIndex]);
      return;
   }

   if (pBvh.nLeft >= 0)  QueryFrustumRecursive (pBvh.nLeft, pFrustum, aResults);
   if (pBvh.nRight >= 0) QueryFrustumRecursive (pBvh.nRight, pFrustum, aResults);
}

// ---------------------------------------------------------------------------
// QuerySphere — returns all leaf nodes whose bounds intersect a sphere.
// ---------------------------------------------------------------------------

void SPATIAL_INDEX::QuerySphere (double x, double y, double z, double dRadius, std::vector<NODE*>& aResults) const
{
   if (m_nRootIndex < 0)
      return;
   QuerySphereRecursive (m_nRootIndex, x, y, z, dRadius, aResults);
}

void SPATIAL_INDEX::QuerySphereRecursive (int nBvhIndex, double x, double y, double z, double dRadius, std::vector<NODE*>& aResults) const
{
   const BVH_NODE& pBvh = m_aBvhNodes[nBvhIndex];

   // AABB-sphere intersection test
   double dClosestX = std::max (pBvh.pBounds.dMinX, std::min (x, pBvh.pBounds.dMaxX));
   double dClosestY = std::max (pBvh.pBounds.dMinY, std::min (y, pBvh.pBounds.dMaxY));
   double dClosestZ = std::max (pBvh.pBounds.dMinZ, std::min (z, pBvh.pBounds.dMaxZ));

   double dDx = dClosestX - x;
   double dDy = dClosestY - y;
   double dDz = dClosestZ - z;
   double dDistSq = dDx * dDx + dDy * dDy + dDz * dDz;

   if (dDistSq > dRadius * dRadius)
      return;

   if (pBvh.IsLeaf ())
   {
      aResults.push_back (m_apLeaves[pBvh.nLeafIndex]);
      return;
   }

   if (pBvh.nLeft >= 0)  QuerySphereRecursive (pBvh.nLeft, x, y, z, dRadius, aResults);
   if (pBvh.nRight >= 0) QuerySphereRecursive (pBvh.nRight, x, y, z, dRadius, aResults);
}

}} // namespace sneeze::som
