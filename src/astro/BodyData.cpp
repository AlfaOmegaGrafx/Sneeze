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
#include "BodyData.h"
#include "RMCObject.h"
#include "scene/Fabric.h"
#include "scene/Node.h"
#include "scene/MapObject.h"

namespace SNEEZE
{
   namespace astro
   {
      static RMCOBJECT_COLOR MakeColor (uint8_t r, uint8_t g, uint8_t b)
      {
         uint32_t nNormal = (static_cast<uint32_t>(r) << 16) | (static_cast<uint32_t>(g) << 8) | b;
         uint32_t nDim = (static_cast<uint32_t>(r/2) << 16) | (static_cast<uint32_t>(g/2) << 8) | (b/2);
         auto bright = [] (uint8_t c) -> uint32_t
         {
            int v = static_cast<int>(c) + 64;
            return static_cast<uint32_t>(v > 255 ? 255 : v);
         };
         uint32_t nBright = (bright(r) << 16) | (bright(g) << 8) | bright(b);
         return { nNormal, nDim, nBright };
      }

      static CELESTIAL_TYPE MapCelestialType (RMCOBJECT_TYPE bType)
      {
         switch (bType)
         {
            case RMCOBJECT_TYPE_UNIVERSE:       return CELESTIAL_TYPE_UNIVERSE;
            case RMCOBJECT_TYPE_STARSYSTEM:     return CELESTIAL_TYPE_STARSYSTEM;
            case RMCOBJECT_TYPE_STAR:           return CELESTIAL_TYPE_STAR;
            case RMCOBJECT_TYPE_PLANETSYSTEM:   return CELESTIAL_TYPE_PLANETSYSTEM;
            case RMCOBJECT_TYPE_PLANET:         return CELESTIAL_TYPE_PLANET;
            case RMCOBJECT_TYPE_MOONSYSTEM:     return CELESTIAL_TYPE_MOONSYSTEM;
            case RMCOBJECT_TYPE_MOON:           return CELESTIAL_TYPE_MOON;
            case RMCOBJECT_TYPE_DEBRISSYSTEM:   return CELESTIAL_TYPE_DEBRISSYSTEM;
            case RMCOBJECT_TYPE_DEBRIS:         return CELESTIAL_TYPE_DEBRIS;
            case RMCOBJECT_TYPE_SATELLITE:      return CELESTIAL_TYPE_SATELLITE;
            case RMCOBJECT_TYPE_SURFACE:        return CELESTIAL_TYPE_SURFACE;
            default:                            return CELESTIAL_TYPE_NONE;
         }
      }

      // -----------------------------------------------------------------------
      // CreateBodies -- populate local RMCOBJECT list with solar system data.
      // All objects are heap-allocated; caller owns the returned vector.
      // Parent/child wiring is done via the local registry map.
      // -----------------------------------------------------------------------

      static void CreateBodies (std::vector<RMCOBJECT*>& aBodies, std::map<std::string, RMCOBJECT*>& registry)
      {
         auto Add = [&] (const RMCOBJECT_PROPS& props) -> RMCOBJECT*
         {
            auto* pBody = new RMCOBJECT (props);
            aBodies.push_back (pBody);

            if (!pBody->sId.empty ())
               registry[pBody->sId] = pBody;

            if (!props.sId_Parent.empty ())
            {
               auto it = registry.find (props.sId_Parent);
               if (it != registry.end ())
               {
                  pBody->pParent = it->second;
                  it->second->aChildren.push_back (pBody);
               }
            }

            return pBody;
         };

         {
            RMCOBJECT_PROPS props;
            props.sName       = "Solar System";
            props.sId         = "solar_system";
            props.sId_Parent  = "";
            props.bType       = RMCOBJECT_TYPE_STARSYSTEM;
            props.bHasOrbit   = false;
            Add (props);
         }
         {
            RMCOBJECT_PROPS props;
            props.sName       = "Sun";
            props.sId         = "sun";
            props.sId_Parent  = "solar_system";
            props.bType       = RMCOBJECT_TYPE_STAR;
            props.dRadius     = 695700;
            props.dMass       = 1.98841e30;
            props.pColor      = MakeColor (0xff, 0xdd, 0x66);
            props.sTexture    = "https://cdn.rp1.com/res/texture/celestial/sun.jpg";
            props.bHasOrbit   = false;
            Add (props);
         }
         {
            RMCOBJECT_PROPS props;
            props.sName       = "Mercury System";
            props.sId         = "mercury_system";
            props.sId_Parent  = "solar_system";
            props.bType       = RMCOBJECT_TYPE_PLANETSYSTEM;
            props.dMass       = 3.30100063677090e+23;
            props.pColor      = MakeColor (0xaa, 0xaa, 0xaa);
            props.bHasOrbit   = true;
            props.orbit.dSemiMajorAU      = 0.3870979408063989;
            props.orbit.dEccentricity     = 0.2056362077352538;
            props.orbit.dInclination      = 7.005014303275355;
            props.orbit.dLonAscNode       = 48.33053855197922;
            props.orbit.dLonPerihelion    = 77.45482020935759;
            props.orbit.dMeanLongitude    = 252.2507031263155;
            props.orbit.dSemiMajorAUDot   = -0.0000010855110431640114;
            props.orbit.dEccentricityDot  = 0.000023657458788517438;
            props.orbit.dInclinationDot   = -0.005933273815280415;
            props.orbit.dLonAscNodeDot    = -0.12532166264209366;
            props.orbit.dLonPerihelionDot = 0.15719664460245042;
            props.orbit.dMeanLongitudeDot = 149472.6767390384;
            Add (props);
         }
         {
            RMCOBJECT_PROPS props;
            props.sName       = "Mercury";
            props.sId         = "mercury";
            props.sId_Parent  = "mercury_system";
            props.bType       = RMCOBJECT_TYPE_PLANET;
            props.dRadius     = 2439.4;
            props.dMass       = 3.302e23;
            props.pColor      = MakeColor (0xaa, 0xaa, 0xaa);
            props.sTexture    = "https://cdn.rp1.com/res/texture/celestial/mercury.jpg";
            props.bHasOrbit   = false;
            Add (props);
         }
         {
            RMCOBJECT_PROPS props;
            props.sName       = "Venus System";
            props.sId         = "venus_system";
            props.sId_Parent  = "solar_system";
            props.bType       = RMCOBJECT_TYPE_PLANETSYSTEM;
            props.dMass       = 4.86730581484201e+24;
            props.pColor      = MakeColor (0xee, 0xcc, 0x88);
            props.bHasOrbit   = true;
            props.orbit.dSemiMajorAU      = 0.7233271893568365;
            props.orbit.dEccentricity     = 0.006740197258864125;
            props.orbit.dInclination      = 3.39458964908054;
            props.orbit.dLonAscNode       = 76.67837411646899;
            props.orbit.dLonPerihelion    = 131.8643411458715;
            props.orbit.dMeanLongitude    = 181.9791130199161;
            props.orbit.dSemiMajorAUDot   = 0.000001047508742080261;
            props.orbit.dEccentricityDot  = -0.00006235597208595952;
            props.orbit.dInclinationDot   = -0.0008541355154747521;
            props.orbit.dLonAscNodeDot    = -0.2779654255936208;
            props.orbit.dLonPerihelionDot = -0.3222704575383375;
            props.orbit.dMeanLongitudeDot = 58517.81682084849;
            Add (props);
         }
         {
            RMCOBJECT_PROPS props;
            props.sName       = "Venus";
            props.sId         = "venus";
            props.sId_Parent  = "venus_system";
            props.bType       = RMCOBJECT_TYPE_PLANET;
            props.dRadius     = 6051.84;
            props.dMass       = 4.8685e24;
            props.pColor      = MakeColor (0xee, 0xcc, 0x88);
            props.sTexture    = "https://cdn.rp1.com/res/texture/celestial/venus.jpg";
            props.bHasOrbit   = false;
            Add (props);
         }
         {
            RMCOBJECT_PROPS props;
            props.sName       = "Earth System";
            props.sId         = "earth_system";
            props.sId_Parent  = "solar_system";
            props.bType       = RMCOBJECT_TYPE_PLANETSYSTEM;
            props.dMass       = 5.97216839978724e+24;
            props.dSystemRadiusKm = 384400;
            props.pColor      = MakeColor (0x44, 0x88, 0xff);
            props.bHasOrbit   = true;
            props.orbit.dSemiMajorAU      = 0.9999965989690871;
            props.orbit.dEccentricity     = 0.01669232252742471;
            props.orbit.dInclination      = 0.0001034624342994112;
            props.orbit.dLonAscNode       = 140.2921798841513;
            props.orbit.dLonPerihelion    = 102.91793238306172;
            props.orbit.dMeanLongitude    = 100.46313620499132;
            props.orbit.dSemiMajorAUDot   = 6.868996828002238e-7;
            props.orbit.dEccentricityDot  = -0.0000401587874355909;
            props.orbit.dInclinationDot   = 0.013013439161427678;
            props.orbit.dLonAscNodeDot    = 34.107178971976;
            props.orbit.dLonPerihelionDot = 0.2945490099604058;
            props.orbit.dMeanLongitudeDot = 35999.37371093485;
            Add (props);
         }
         {
            RMCOBJECT_PROPS props;
            props.sName       = "Earth";
            props.sId         = "earth";
            props.sId_Parent  = "earth_system";
            props.bType       = RMCOBJECT_TYPE_PLANET;
            props.dRadius     = 6371.01;
            props.dMass       = 5.97219e24;
            props.pColor      = MakeColor (0x44, 0x88, 0xff);
            props.sTexture    = "https://cdn.rp1.com/res/texture/celestial/earth.jpg";
            props.bHasOrbit   = false;
            Add (props);
         }
         {
            RMCOBJECT_PROPS props;
            props.sName       = "Mars System";
            props.sId         = "mars_system";
            props.sId_Parent  = "solar_system";
            props.bType       = RMCOBJECT_TYPE_PLANETSYSTEM;
            props.dMass       = 6.41690901158174e+23;
            props.dSystemRadiusKm = 23460;
            props.pColor      = MakeColor (0xff, 0x66, 0x44);
            props.bHasOrbit   = true;
            props.orbit.dSemiMajorAU      = 1.523679479011924;
            props.orbit.dEccentricity     = 0.09338848951579180;
            props.orbit.dInclination      = 1.849876455662937;
            props.orbit.dLonAscNode       = 49.56200645402956;
            props.orbit.dLonPerihelion    = 336.09938785991824;
            props.orbit.dMeanLongitude    = 355.4558713443424;
            props.orbit.dSemiMajorAUDot   = 0.000001946853869005949;
            props.orbit.dEccentricityDot  = 0.00029355182112218714;
            props.orbit.dInclinationDot   = -0.008222694189379043;
            props.orbit.dLonAscNodeDot    = -0.30416632268770627;
            props.orbit.dLonPerihelionDot = 0.45333392020074825;
            props.orbit.dMeanLongitudeDot = 19140.28503673707;
            Add (props);
         }
         {
            RMCOBJECT_PROPS props;
            props.sName       = "Mars";
            props.sId         = "mars";
            props.sId_Parent  = "mars_system";
            props.bType       = RMCOBJECT_TYPE_PLANET;
            props.dRadius     = 3389.92;
            props.dMass       = 6.4171e23;
            props.pColor      = MakeColor (0xff, 0x66, 0x44);
            props.sTexture    = "https://cdn.rp1.com/res/texture/celestial/mars.jpg";
            props.bHasOrbit   = false;
            Add (props);
         }
         {
            RMCOBJECT_PROPS props;
            props.sName       = "Jupiter System";
            props.sId         = "jupiter_system";
            props.sId_Parent  = "solar_system";
            props.bType       = RMCOBJECT_TYPE_PLANETSYSTEM;
            props.dMass       = 1.89851765878070e+27;
            props.dSystemRadiusKm = 1882700;
            props.pColor      = MakeColor (0xdd, 0xaa, 0x66);
            props.bHasOrbit   = true;
            props.orbit.dSemiMajorAU      = 5.204305185606453;
            props.orbit.dEccentricity     = 0.04850058718310243;
            props.orbit.dInclination      = 1.304625372771513;
            props.orbit.dLonAscNode       = 100.4916213525931;
            props.orbit.dLonPerihelion    = 15.557633368839163;
            props.orbit.dMeanLongitude    = 34.37610126227887;
            props.orbit.dSemiMajorAUDot   = 0.00015421953231875563;
            props.orbit.dEccentricityDot  = -0.0010971617689138624;
            props.orbit.dInclinationDot   = -0.002194666603669848;
            props.orbit.dLonAscNodeDot    = 0.17640633739979705;
            props.orbit.dLonPerihelionDot = -0.7348520016954012;
            props.orbit.dMeanLongitudeDot = 3034.7452956513416;
            Add (props);
         }
         {
            RMCOBJECT_PROPS props;
            props.sName       = "Jupiter";
            props.sId         = "jupiter";
            props.sId_Parent  = "jupiter_system";
            props.bType       = RMCOBJECT_TYPE_PLANET;
            props.dRadius     = 69911;
            props.dMass       = 1.89819e27;
            props.pColor      = MakeColor (0xdd, 0xaa, 0x66);
            props.sTexture    = "https://cdn.rp1.com/res/texture/celestial/jupiter.jpg";
            props.bHasOrbit   = false;
            Add (props);
         }
         {
            RMCOBJECT_PROPS props;
            props.sName       = "Saturn System";
            props.sId         = "saturn_system";
            props.sId_Parent  = "solar_system";
            props.bType       = RMCOBJECT_TYPE_PLANETSYSTEM;
            props.dMass       = 5.68457888344845e+26;
            props.dSystemRadiusKm = 3560820;
            props.pColor      = MakeColor (0xcc, 0xbb, 0x77);
            props.bHasOrbit   = true;
            props.orbit.dSemiMajorAU      = 9.580688339625405;
            props.orbit.dEccentricity     = 0.05533811933761676;
            props.orbit.dInclination      = 2.485250440984045;
            props.orbit.dLonAscNode       = 113.6429621251987;
            props.orbit.dLonPerihelion    = 89.65658701409211;
            props.orbit.dMeanLongitude    = 50.004437769279036;
            props.orbit.dSemiMajorAUDot   = -0.005315338444466633;
            props.orbit.dEccentricityDot  = -0.0015411012794838905;
            props.orbit.dInclinationDot   = 0.0033027097388700355;
            props.orbit.dLonAscNodeDot    = -0.26331205921039214;
            props.orbit.dLonPerihelionDot = 5.970605449839184;
            props.orbit.dMeanLongitudeDot = 1222.5058143877384;
            Add (props);
         }
         {
            RMCOBJECT_PROPS props;
            props.sName       = "Saturn";
            props.sId         = "saturn";
            props.sId_Parent  = "saturn_system";
            props.bType       = RMCOBJECT_TYPE_PLANET;
            props.dRadius     = 58232;
            props.dMass       = 5.6834e26;
            props.pColor      = MakeColor (0xcc, 0xbb, 0x77);
            props.sTexture    = "https://cdn.rp1.com/res/texture/celestial/saturn.jpg";
            props.bHasOrbit   = false;
            Add (props);
         }
         {
            RMCOBJECT_PROPS props;
            props.sName       = "Uranus System";
            props.sId         = "uranus_system";
            props.sId_Parent  = "solar_system";
            props.bType       = RMCOBJECT_TYPE_PLANETSYSTEM;
            props.dMass       = 8.68189383156286e+25;
            props.dSystemRadiusKm = 583520;
            props.pColor      = MakeColor (0x66, 0xcc, 0xdd);
            props.bHasOrbit   = true;
            props.orbit.dSemiMajorAU      = 19.20215886444208;
            props.orbit.dEccentricity     = 0.04589371030287445;
            props.orbit.dInclination      = 0.7725675263741457;
            props.orbit.dLonAscNode       = 73.9894163012876;
            props.orbit.dLonPerihelion    = 170.53108534745581;
            props.orbit.dMeanLongitude    = 313.4868371827975;
            props.orbit.dSemiMajorAUDot   = -0.10903096134362045;
            props.orbit.dEccentricityDot  = 0.005952738874395659;
            props.orbit.dInclinationDot   = -0.0013128449334149916;
            props.orbit.dLonAscNodeDot    = -0.048966734082526386;
            props.orbit.dLonPerihelionDot = 1.9503989253083773;
            props.orbit.dMeanLongitudeDot = 428.3399609965;
            Add (props);
         }
         {
            RMCOBJECT_PROPS props;
            props.sName       = "Uranus";
            props.sId         = "uranus";
            props.sId_Parent  = "uranus_system";
            props.bType       = RMCOBJECT_TYPE_PLANET;
            props.dRadius     = 25362;
            props.dMass       = 8.6813e25;
            props.pColor      = MakeColor (0x66, 0xcc, 0xdd);
            props.sTexture    = "https://cdn.rp1.com/res/texture/celestial/uranus.jpg";
            props.bHasOrbit   = false;
            Add (props);
         }
         {
            RMCOBJECT_PROPS props;
            props.sName       = "Neptune System";
            props.sId         = "neptune_system";
            props.sId_Parent  = "solar_system";
            props.bType       = RMCOBJECT_TYPE_PLANETSYSTEM;
            props.dMass       = 1.02430623444856e+26;
            props.dSystemRadiusKm = 354760;
            props.pColor      = MakeColor (0x44, 0x66, 0xff);
            props.bHasOrbit   = true;
            props.orbit.dSemiMajorAU      = 30.14481102807876;
            props.orbit.dEccentricity     = 0.01010917913908649;
            props.orbit.dInclination      = 1.76798634857378;
            props.orbit.dLonAscNode       = 131.7938660576663;
            props.orbit.dLonPerihelion    = 37.4428114574813;
            props.orbit.dMeanLongitude    = 305.2085365853071;
            props.orbit.dSemiMajorAUDot   = 0.16468551345476;
            props.orbit.dEccentricityDot  = -0.004423855187017842;
            props.orbit.dInclinationDot   = 0.00059858607458807;
            props.orbit.dLonAscNodeDot    = -0.05091932961559564;
            props.orbit.dLonPerihelionDot = 53.518515451649876;
            props.orbit.dMeanLongitudeDot = 218.2404178632974;
            Add (props);
         }
         {
            RMCOBJECT_PROPS props;
            props.sName       = "Neptune";
            props.sId         = "neptune";
            props.sId_Parent  = "neptune_system";
            props.bType       = RMCOBJECT_TYPE_PLANET;
            props.dRadius     = 24624;
            props.dMass       = 1.02409e26;
            props.pColor      = MakeColor (0x44, 0x66, 0xff);
            props.sTexture    = "https://cdn.rp1.com/res/texture/celestial/neptune.jpg";
            props.bHasOrbit   = false;
            Add (props);
         }
         {
            RMCOBJECT_PROPS props;
            props.sName       = "Pluto System";
            props.sId         = "pluto_system";
            props.sId_Parent  = "solar_system";
            props.bType       = RMCOBJECT_TYPE_PLANETSYSTEM;
            props.dMass       = 1.46157649491332e+22;
            props.dSystemRadiusKm = 64738;
            props.pColor      = MakeColor (0xcc, 0xaa, 0x88);
            props.bHasOrbit   = true;
            props.orbit.dSemiMajorAU      = 39.28126370224696;
            props.orbit.dEccentricity     = 0.2468285575354664;
            props.orbit.dInclination      = 17.15136439626299;
            props.orbit.dLonAscNode       = 110.286929741788;
            props.orbit.dLonPerihelion    = 224.049832230308;
            props.orbit.dMeanLongitude    = 239.07310138032219;
            props.orbit.dSemiMajorAUDot   = 0.06770513915833476;
            props.orbit.dEccentricityDot  = 0.00861618086357388;
            props.orbit.dInclinationDot   = -0.010874333737152853;
            props.orbit.dLonAscNodeDot    = 0.05175840871099524;
            props.orbit.dLonPerihelionDot = -0.4573939302094061;
            props.orbit.dMeanLongitudeDot = 145.1572129622987;
            Add (props);
         }
         {
            RMCOBJECT_PROPS props;
            props.sName       = "Pluto";
            props.sId         = "pluto";
            props.sId_Parent  = "pluto_system";
            props.bType       = RMCOBJECT_TYPE_PLANET;
            props.dRadius     = 1188.3;
            props.dMass       = 1.307e22;
            props.pColor      = MakeColor (0xcc, 0xaa, 0x88);
            props.sTexture    = "https://cdn.rp1.com/res/texture/celestial/pluto.jpg";
            props.bHasOrbit   = false;
            Add (props);
         }
      }

      // -----------------------------------------------------------------------
      // InjectSolarSystem -- stateless injection of solar system test data.
      //
      // Creates RMCOBJECT body data locally, computes orbital mechanics,
      // populates MAP_OBJECT_CELESTIAL instances, injects NODEs into the
      // fabric, then cleans up all temporary RMCOBJECT data before returning.
      // -----------------------------------------------------------------------

      void InjectSolarSystem (VIEWPORT::SCENE::FABRIC* pFabric)
      {
         if (!pFabric  ||  !pFabric->Node_Root ())
            return;

         VIEWPORT::SCENE::FABRIC::NODE* pRoot = pFabric->Node_Root ();

         std::vector<RMCOBJECT*>            aBodies;
         std::map<std::string, RMCOBJECT*>  registry;

         CreateBodies (aBodies, registry);

         for (auto* pBody : aBodies)
            pBody->ComputeRaw ();
         for (auto* pBody : aBodies)
            pBody->ConvertToOutput ();

         // --- Sun node (no orbit, sits at origin) ---

         auto itSun = registry.find ("sun");
         {
            auto* pMapObj = new MAP_OBJECT_CELESTIAL ();
            pMapObj->m_sName           = "Sun";
            pMapObj->m_bCelestialType  = CELESTIAL_TYPE_STAR;
            pMapObj->m_dPosX           = 0.0;
            pMapObj->m_dPosY           = 0.0;
            pMapObj->m_dPosZ           = 0.0;
            pMapObj->m_dRadius         = 695700.0 * 1000.0;
            pMapObj->m_nColor          = 0xFFE666;
            pMapObj->m_nColorDim       = 0x7F7333;
            pMapObj->m_nColorBright    = 0xFFFF9A;
            pMapObj->m_dMass           = 1.98841e30;
            pMapObj->m_sTextureUrl     = (itSun != registry.end ()) ? itSun->second->sTexture : "";

            auto* pNode = new VIEWPORT::SCENE::FABRIC::NODE (pFabric);
            pNode->MapObject_Set (pMapObj);
            pRoot->Node_Add (pNode);
         }

         // --- Orbit bodies ---

         for (auto* pBody : aBodies)
         {
            if (!pBody->pOrbit)
               continue;

            RMCOBJECT* pChildBody = nullptr;
            for (auto* pChild : pBody->aChildren)
            {
               if (pChild->bType == RMCOBJECT_TYPE_PLANET  ||
                   pChild->bType == RMCOBJECT_TYPE_STAR)
               {
                  pChildBody = pChild;
                  break;
               }
            }

            double dRadius = 100.0 * 1000.0;
            uint32_t nColor = pBody->GetColor ();
            uint32_t nColorDim = pBody->pColor.nDim;
            uint32_t nColorBright = pBody->pColor.nBright;
            if (pChildBody)
            {
               dRadius = pChildBody->dRadius.value_or (100.0) * 1000.0;
               nColor = pChildBody->GetColor ();
               nColorDim = pChildBody->pColor.nDim;
               nColorBright = pChildBody->pColor.nBright;
            }

            std::string sTexture;
            if (pChildBody  &&  !pChildBody->sTexture.empty ())
               sTexture = pChildBody->sTexture;

            auto* pMapObj = new MAP_OBJECT_CELESTIAL ();
            pMapObj->m_sName           = pChildBody ? pChildBody->sName : pBody->sName;
            pMapObj->m_bCelestialType  = MapCelestialType (pChildBody ? pChildBody->bType : pBody->bType);
            pMapObj->m_dRadius         = dRadius;
            pMapObj->m_nColor          = nColor;
            pMapObj->m_nColorDim       = nColorDim;
            pMapObj->m_nColorBright    = nColorBright;
            pMapObj->m_dMass           = pBody->dMass;
            pMapObj->m_dGM             = pBody->dGM;
            pMapObj->m_dSystemRadiusKm = pBody->dSystemRadiusKm;
            pMapObj->m_sTextureUrl     = sTexture;
            pMapObj->m_orbit           = *pBody->pOrbit;

            auto* pNode = new VIEWPORT::SCENE::FABRIC::NODE (pFabric);
            pNode->MapObject_Set (pMapObj);
            pRoot->Node_Add (pNode);
         }

         // --- Clean up all temporary body data ---

         for (auto* pBody : aBodies)
            delete pBody;
      }
   }
}
