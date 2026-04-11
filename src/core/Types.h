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

#ifndef SNEEZE_CORE_TYPES_H
#define SNEEZE_CORE_TYPES_H

#include <cmath>
#include <cstdint>

namespace sneeze { namespace core {

struct VEC3
{
   double x;
   double y;
   double z;
};

struct QUAT
{
   double dX;
   double dY;
   double dZ;
   double dW;
};

constexpr double PI         = 3.14159265358979323846;
constexpr double TWO_PI     = 2.0 * PI;
constexpr double DEG_TO_RAD = PI / 180.0;

constexpr double AU_M         = 149597870700.0;
constexpr double GM_SUN_M3S2  = 1.32712440041279419e20;
constexpr double JULIAN_YEAR  = 365.25 * 86400.0;
constexpr double G_M3_KG_S2   = 6.67430e-11;

constexpr int64_t TICKS_PER_S  = 64;
constexpr int64_t TICKS_PER_CY = 36525LL * 86400LL * TICKS_PER_S;

constexpr double OBLIQUITY_J2000 = 23.4392911;

}} // namespace sneeze::core

#endif // SNEEZE_CORE_TYPES_H
