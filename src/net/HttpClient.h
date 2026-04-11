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

#ifndef SNEEZE_NET_HTTP_CLIENT_H
#define SNEEZE_NET_HTTP_CLIENT_H

#include <string>

namespace sneeze
{
namespace net
{

class HTTP_CLIENT
{
public:
   HTTP_CLIENT ();
   ~HTTP_CLIENT ();

   bool Initialize ();
   void Shutdown ();

   bool Get (const std::string& sUrl, std::string& sResponse, long& nHttpCode);
   bool DownloadToFile (const std::string& sUrl, const std::string& sFilePath, long& nHttpCode);

private:
   bool bInitialized;
};

} // namespace net
} // namespace sneeze

#endif // SNEEZE_NET_HTTP_CLIENT_H
