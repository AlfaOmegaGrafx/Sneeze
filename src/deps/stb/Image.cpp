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

#include <Image.h>

#include "stb_image.h"

namespace SNEEZE
{
   namespace IMAGE
   {
      bool Decode (const std::vector<uint8_t>& aEncoded, int& nWidth, int& nHeight, std::vector<uint8_t>& aPixels)
      {
         bool bResult = false;

         nWidth  = 0;
         nHeight = 0;
         aPixels.clear ();

         if (!aEncoded.empty ())
         {
            int nW = 0, nH = 0, nChannels = 0;

            unsigned char* pPixels = stbi_load_from_memory (aEncoded.data (), static_cast<int> (aEncoded.size ()), &nW, &nH, &nChannels, 4);

            if (pPixels)
            {
               nWidth  = nW;
               nHeight = nH;
               aPixels.assign (pPixels, pPixels + (static_cast<size_t> (nW) * static_cast<size_t> (nH) * 4));

               stbi_image_free (pPixels);

               bResult = true;
            }
         }

         return bResult;
      }
   }
}
