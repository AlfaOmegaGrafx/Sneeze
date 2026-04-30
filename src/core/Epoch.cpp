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

#include "Epoch.h"
#include <cmath>
#include <chrono>
#include <regex>

namespace SNEEZE { namespace CORE {

// Leap-second offset: TAI - UTC.  As of 2017-01-01, DeltaAT = 37 s.
// TT = UTC + DeltaAT + 32.184 s
static constexpr double dDeltaAT = 37.0;

EPOCH::EPOCH ()
   : m_sEpoch ("J2000.0")
   , m_dJD (2451545.0)
{
}

EPOCH::EPOCH (const std::string& sEpoch)
   : m_sEpoch (sEpoch)
   , m_dJD (Parse (sEpoch))
{
}

double EPOCH::GetJD () const
{
   return m_dJD;
}

double EPOCH::DeltaYearsFrom (const EPOCH& pOther) const
{
   return (m_dJD - pOther.m_dJD) / 365.25;
}

double EPOCH::PropagateMeanAnomaly (double dM, double dPeriodYears, const EPOCH& pTarget) const
{
   double dDeltaYears = pTarget.DeltaYearsFrom (*this);
   double dMeanMotion = 360.0 / dPeriodYears;
   double dResult     = dM + dMeanMotion * dDeltaYears;

   dResult = std::fmod (dResult, 360.0);
   if (dResult < 0.0) dResult += 360.0;

   return dResult;
}

double EPOCH::NowTT ()
{
   auto   now      = std::chrono::system_clock::now ();
   double dMillis  = static_cast<double> (
      std::chrono::duration_cast<std::chrono::milliseconds> (now.time_since_epoch ()).count ());
   double dUTC_JD  = dMillis / 86400000.0 + 2440587.5;

   return dUTC_JD + (dDeltaAT + 32.184) / 86400.0;
}

double EPOCH::Parse (const std::string& sEpoch)
{
   double dJD = 2451545.0;

   if (!sEpoch.empty ()  &&  sEpoch.substr (0, 5) != "J2000")
   {
      std::regex reMJD  ("MJD\\s+([\\d.]+)");
      std::regex reJD   ("(?<!M)JD\\s+([\\d.]+)");
      std::regex reDate ("^(\\d{4})-(\\d{2})-(\\d{2})");
      std::smatch match;

      if (std::regex_search (sEpoch, match, reMJD))
      {
         dJD = std::stod (match[1].str ()) + 2400000.5;
      }
      else if (std::regex_search (sEpoch, match, reJD))
      {
         dJD = std::stod (match[1].str ());
      }
      else if (std::regex_search (sEpoch, match, reDate))
      {
         dJD = CalendarToJD (std::stoi (match[1].str ()), std::stoi (match[2].str ()), std::stoi (match[3].str ()));
      }
   }

   return dJD;
}

double EPOCH::CalendarToJD (int dwY, int dwM, int dwD)
{
   if (dwM <= 2)
   {
      dwY -= 1;
      dwM += 12;
   }
   int dwA = dwY / 100;
   int dwB = 2 - dwA + dwA / 4;

   return std::trunc (365.25 * (dwY + 4716)) + std::trunc (30.6001 * (dwM + 1)) + dwD + dwB - 1524.5;
}

const EPOCH EPOCH_J2000;

}} // namespace SNEEZE::CORE
