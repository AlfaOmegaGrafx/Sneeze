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

#ifndef SNEEZE_CACHE_TYPES_H
#define SNEEZE_CACHE_TYPES_H

#include <cstdint>

namespace SNEEZE { namespace CACHE {

class FILE;

enum STATE
{
   STATE_IDLE       = 0,
   STATE_FETCHING   = 1,
   STATE_VALIDATING = 2,
   STATE_READY      = 3,
   STATE_FAILED     = 4,
};

enum REQUEST
{
   REQUEST_CREATE = 0x01,
   REQUEST_FETCH  = 0x02,
};

static const uint32_t kREQUEST_DEFAULT = REQUEST_CREATE | REQUEST_FETCH;

enum DISKFILE
{
   DISKFILE_DATA = 0,
   DISKFILE_TEMP = 1,
   DISKFILE_META = 2,
};

class IFILE
{
public:
   virtual ~IFILE () {}
   virtual void OnFileReady  (FILE* pFile) = 0;
   virtual void OnFileFailed (FILE* pFile) = 0;
};

class IENUM
{
public:
   virtual ~IENUM () {}
   virtual void OnEntry (FILE* pFile) = 0;
};

}} // namespace SNEEZE::CACHE

#endif // SNEEZE_CACHE_TYPES_H
