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

#ifndef SNEEZE_CORE_EPOCH_H
#define SNEEZE_CORE_EPOCH_H

#include <string>
#include <cstdint>

namespace sneeze { namespace core {

class EPOCH
{
public:
   EPOCH ();
   explicit EPOCH (const std::string& sEpoch);

   double GetJD () const;
   double DeltaYearsFrom (const EPOCH& pOther) const;
   double PropagateMeanAnomaly (double dM, double dPeriodYears, const EPOCH& pTarget) const;

   static double NowTT ();
   static double Parse (const std::string& sEpoch);
   static double CalendarToJD (int dwY, int dwM, int dwD);

private:
   std::string m_sEpoch;
   double      m_dJD;
};

extern const EPOCH EPOCH_J2000;

}} // namespace sneeze::core

#endif // SNEEZE_CORE_EPOCH_H
