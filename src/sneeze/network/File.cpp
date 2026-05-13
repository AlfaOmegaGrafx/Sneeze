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

using namespace SNEEZE;

const std::unordered_map<std::string, std::string> NETWORK::FILE::s_mapEmpty;

NETWORK::FILE::FILE (NETWORK* pNetwork, ASSET* pAsset, VIEWPORT::CONTAINER::NAME* pName, VIEWPORT* pViewport, IFILE* pListener, uint32_t nFileIx) :
   m_pNetwork         (pNetwork),
   m_pAsset           (pAsset),
   m_Name             (*pName),
   m_pViewport        (pViewport),
   m_pListener        (pListener),
   m_nFileIx          (nFileIx),
   m_nAssetIx         (0),
   m_bState           (STATE_IDLE),
   m_nSizeBytes       (0),
   m_nHttpStatus      (0),
   m_dFetchQueuedTime (0.0),
   m_dFetchStartTime  (0.0),
   m_dFetchEndTime    (0.0),
   m_bServedFromCache (false),
   m_bPendingClear    (false),
   m_bReleased        (false)
{
   SnapshotInitial ();
}

NETWORK::FILE::~FILE ()
{
}

// ---------------------------------------------------------------------------
// Snapshot — copies display fields from the attached ASSET
// ---------------------------------------------------------------------------

void NETWORK::FILE::SnapshotInitial ()
{
   if (m_pAsset)
   {
      m_sUrl     = m_pAsset->Url ();
      m_nAssetIx = m_pAsset->AssetIx ();
   }
}

void NETWORK::FILE::SnapshotProgress ()
{
   if (m_pAsset)
   {
      m_bState           = m_pAsset->State ();
      m_dFetchQueuedTime = m_pAsset->FetchQueuedTime ();
      m_dFetchStartTime  = m_pAsset->FetchStartTime ();
   }
}

void NETWORK::FILE::SnapshotFinal ()
{
   if (m_pAsset)
   {
      m_bState           = m_pAsset->State ();
      m_sHash            = m_pAsset->Hash ();
      m_sContentType     = m_pAsset->Header ("content-type");
      m_nSizeBytes       = m_pAsset->SizeBytes ();
      m_nHttpStatus      = m_pAsset->HttpStatus ();
      m_dFetchQueuedTime = m_pAsset->FetchQueuedTime ();
      m_dFetchStartTime  = m_pAsset->FetchStartTime ();
      m_dFetchEndTime    = m_pAsset->FetchEndTime ();
      m_bServedFromCache = m_pAsset->IsServedFromCache ();
   }
}

// ---------------------------------------------------------------------------
// Actions
// ---------------------------------------------------------------------------

bool NETWORK::FILE::Request (IFILE* pListener)
{
   m_pListener = pListener;
   return m_pNetwork->Reopen (this);
}

void NETWORK::FILE::Release ()
{
   m_pNetwork->Release (this);
}

void NETWORK::FILE::Clear (bool b)
{
   m_pNetwork->Clear (this, b);
}

void NETWORK::FILE::Reset (bool b)
{
   m_pNetwork->Reset (this, b);
}

// ---------------------------------------------------------------------------
// ASSET-dependent accessors (require attached ASSET)
// ---------------------------------------------------------------------------

std::vector<uint8_t> NETWORK::FILE::ReadData () const
{
   return m_pAsset->ReadData ();
}

std::string NETWORK::FILE::Header (const std::string& sName) const
{
   return m_pAsset->Header (sName);
}

std::string NETWORK::FILE::DiskPath () const
{
   return m_pAsset->DiskPath ();
}

std::string NETWORK::FILE::CreatedTime () const
{
   return m_pAsset->CreatedTime ();
}

std::string NETWORK::FILE::LastAccessTime () const
{
   return m_pAsset->LastAccessTime ();
}

uint32_t NETWORK::FILE::AccessCount () const
{
   return m_pAsset->AccessCount ();
}

const std::unordered_map<std::string, std::string> NETWORK::FILE::Headers () const
{
   return m_pAsset->Headers ();
}

std::string NETWORK::FILE::ContainerName () const
{
   return m_Name.DisplayName ();
}

NETWORK::STATE NETWORK::FILE::State () const
{ 
   return m_bState; 
}

bool NETWORK::FILE::IsReady () const 
{ 
   return m_bState == STATE_READY; 
}

std::string NETWORK::FILE::Url () const 
{ 
   return m_sUrl; 
}

std::string NETWORK::FILE::Hash () const 
{ 
   return m_sHash; 
}

bool NETWORK::FILE::IsHashed () const 
{ 
   return !m_sHash.empty (); 
}

uint32_t NETWORK::FILE::FileIx () const 
{ 
   return m_nFileIx; 
}

uint32_t NETWORK::FILE::AssetIx () const 
{ 
   return m_nAssetIx; 
}

long NETWORK::FILE::HttpStatus () const 
{ 
   return m_nHttpStatus; 
}

double NETWORK::FILE::FetchQueuedTime () const 
{ 
   return m_dFetchQueuedTime; 
}

double NETWORK::FILE::FetchStartTime () const 
{ 
   return m_dFetchStartTime; 
}

double NETWORK::FILE::FetchEndTime () const 
{ 
   return m_dFetchEndTime; 
}

double NETWORK::FILE::FetchDuration () const 
{ 
   return m_dFetchEndTime - m_dFetchStartTime; 
}

bool NETWORK::FILE::IsServedFromCache () const 
{ 
   return m_bServedFromCache; 
}

std::string NETWORK::FILE::ContentType () const
{ 
   return m_sContentType; 
}

uint64_t NETWORK::FILE::SizeBytes () const
{ 
   return m_nSizeBytes; 
}

NETWORK::ASSET* NETWORK::FILE::Asset () const
{ 
   return m_pAsset; 
}

void NETWORK::FILE::SetAsset (ASSET* pAsset)
{ 
   m_pAsset = pAsset; 
}

bool NETWORK::FILE::IsPendingClear () const
{ 
   return m_bPendingClear; 
}

bool NETWORK::FILE::IsReleased () const
{ 
   return m_bReleased; 
}

bool NETWORK::FILE::IsAttached () const
{ 
   return m_pAsset != nullptr; 
}

void NETWORK::FILE::SetReleased ()
{ 
   m_bReleased = true; 
}

bool NETWORK::FILE::SetPendingClear (bool b)
{ 
   bool bChanged = (b != m_bPendingClear); m_bPendingClear = b; 

   return bChanged; 
}

VIEWPORT* NETWORK::FILE::Viewport () const
{
   return m_pViewport;
}

NETWORK::IFILE* NETWORK::FILE::Listener () const
{ 
   return m_pListener; 
}
