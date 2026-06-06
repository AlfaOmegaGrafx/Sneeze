#![allow(non_snake_case, non_camel_case_types, dead_code)]

#[link(wasm_import_module = "Console")]
extern "C"
{
   fn Log (dwOffset: u32, dwLength: u32);
}

#[link(wasm_import_module = "Scene")]
extern "C"
{
   fn Node_Root     (twFabricIx: u64, dwOffset: u32, dwLength: u32) -> u64;
   fn Node_Open     (twParentIx: u64, dwOffset: u32, dwLength: u32) -> u64;
   fn Node_Close    (twObjectIx: u64) -> i32;
   fn Node_Position (twObjectIx: u64, dX: f64, dY: f64, dZ: f64);
   fn Node_Scale    (twObjectIx: u64, dScale: f64);
   fn Node_Bound    (twObjectIx: u64, dBound: f64);
   fn Node_Color    (twObjectIx: u64, nColor: i32);
   fn Node_Name     (twObjectIx: u64, dwOffset: u32, dwLength: u32);
   fn Node_Radius   (twObjectIx: u64, dRadius: f64);
}

fn LogMsg (sMsg: &str)
{
   unsafe
   {
      Log (sMsg.as_ptr () as u32, sMsg.len () as u32);
   }
}

// RMAP constants
const OBJECTIX_IDENTITY: u64 = 0x0000_FFFF_FFFF_FFFF;
const OBJECTIX_NULL:     u64 = 0x0000_0000_0000_0000;

fn MakeObjectIx (wClass: u16, twObjectIx: u64) -> u64
{
   ((wClass as u64) << 48) | (twObjectIx & 0x0000_FFFF_FFFF_FFFF)
}

// RMCOBJECT layout (432 bytes, packed)
#[repr(C, packed)]
struct RMCOBJECT
{
   // OBJECT_HEAD (24 bytes)
   qwObjectIx_Parent:       u64,
   qwObjectIx_Self:         u64,
   qwEvent:                 u64,

   // MAP_OBJECT_NAME (96 bytes)
   wsName:                  [u16; 48],

   // MAP_OBJECT_TYPE (8 bytes)
   bType:                   u8,
   bSubtype:                u8,
   bFiction:                u8,
   abReserved_Type:         [u8; 5],

   // MAP_OBJECT_OWNER (8 bytes)
   twOwner:                 u64,

   // MAP_OBJECT_RESOURCE (104 bytes)
   qwResource:              u64,
   sName_Resource:          [u8; 32],
   sReference:              [u8; 64],

   // MAP_OBJECT_TRANSFORM (80 bytes)
   d3Position:              [f64; 3],
   d4Rotation:              [f64; 4],
   d3Scale:                 [f64; 3],

   // MAP_OBJECT_ORBIT (32 bytes)
   tmPeriod:                i64,
   tmOrigin:                i64,
   dA:                      f64,
   dB:                      f64,

   // MAP_OBJECT_BOUND (48 bytes)
   abReserved_Bound:        [u8; 24],
   d3Max:                   [f64; 3],

   // MAP_OBJECT_PROPERTIES (32 bytes)
   fMass:                   f32,
   fGravity:                f32,
   fColor:                  f32,
   fBrightness:             f32,
   fReflectivity:           f32,
   abReserved_Properties:   [u8; 12],
}

const _: () = assert!(core::mem::size_of::<RMCOBJECT> () == 432);

impl RMCOBJECT
{
   fn New () -> Self
   {
      unsafe
      {
         core::mem::zeroed ()
      }
   }

   fn Name_Set (&mut self, sName: &str)
   {
      for (i, c) in sName.chars ().enumerate ()
      {
         if i >= 48
         {
            break;
         }
         self.wsName[i] = c as u16;
      }
   }

   fn Reference_Set (&mut self, sRef: &str)
   {
      let abRef = sRef.as_bytes ();
      let nLen  = if abRef.len () < 63 { abRef.len () } else { 63 };
      self.sReference[..nLen].copy_from_slice (&abRef[..nLen]);
   }
}

fn RMCObject_Create (sName: &str, bType: u8, bSubtype: u8, dRadius: f64, dX: f64, dY: f64, dZ: f64, fColor: f32, fMass: f32, sTexture: &str) -> RMCOBJECT
{
   let mut RMCObject = RMCOBJECT::New ();
   RMCObject.qwObjectIx_Self = MakeObjectIx (0, OBJECTIX_IDENTITY);
   RMCObject.bType           = bType;
   RMCObject.bSubtype        = bSubtype;
   RMCObject.d3Position      = [dX, dY, dZ];
   RMCObject.d3Scale         = [1.0, 1.0, 1.0];
   RMCObject.d3Max           = [dRadius, dRadius, dRadius];
   RMCObject.fColor          = fColor;
   RMCObject.fMass           = fMass;
   RMCObject.Name_Set (sName);
   if !sTexture.is_empty ()
   {
      RMCObject.Reference_Set (sTexture);
   }
   RMCObject
}

fn Node_Create_Root (twFabricIx: u64, sName: &str, bType: u8, bSubtype: u8, dRadius: f64, dX: f64, dY: f64, dZ: f64, fColor: f32, fMass: f32) -> u64
{
   let RMCObject = RMCObject_Create (sName, bType, bSubtype, dRadius, dX, dY, dZ, fColor, fMass, "");
   let dwOffset  = &RMCObject as *const RMCOBJECT as u32;
   let dwLength  = core::mem::size_of::<RMCOBJECT> () as u32;

   unsafe { Node_Root (twFabricIx, dwOffset, dwLength) }
}

fn Node_Create_Child (twParentIx: u64, sName: &str, bType: u8, bSubtype: u8, dRadius: f64, dX: f64, dY: f64, dZ: f64, fColor: f32, fMass: f32, sTexture: &str) -> u64
{
   let RMCObject = RMCObject_Create (sName, bType, bSubtype, dRadius, dX, dY, dZ, fColor, fMass, sTexture);
   let dwOffset  = &RMCObject as *const RMCOBJECT as u32;
   let dwLength  = core::mem::size_of::<RMCOBJECT> () as u32;

   unsafe { Node_Open (twParentIx, dwOffset, dwLength) }
}

const TYPE_ROOT:      u8 = 0;
const TYPE_CELESTIAL: u8 = 1;

const SUBTYPE_NONE:   u8 = 0;
const SUBTYPE_STAR:   u8 = 10;
const SUBTYPE_PLANET: u8 = 12;

const AU: f64 = 149_597_870_700.0;

#[no_mangle]
pub extern "C" fn Init ()
{
   LogMsg ("Solar System WASM: Init");
}

#[no_mangle]
pub extern "C" fn Open (twFabricIx: u64, _dwOffset: u32, _dwLength: u32)
{
   LogMsg (&format! ("Solar System WASM: Open (twFabricIx={})", twFabricIx));

   let sTexBase = "https://cdn.rp1.com/res/texture/celestial/";

   //                                                                              radius (m)       x position             y    z    color (f32 bits)                        mass (f32)   texture
   let twRoot = Node_Create_Root (twFabricIx, "Solar System", TYPE_ROOT, SUBTYPE_NONE, 0.0, 0.0,                 0.0, 0.0, 0.0,                                    0.0);
   if twRoot == 0 { LogMsg ("  ERROR: Failed to create root node"); return; }

   Node_Create_Child (twRoot, "Sun",      TYPE_CELESTIAL, SUBTYPE_STAR,   695_700_000.0,       0.0,                                0.0, 0.0, f32::from_bits (0x00FFDD66),            1.989e30,    &format! ("{}sun.jpg",     sTexBase));
   Node_Create_Child (twRoot, "Mercury",  TYPE_CELESTIAL, SUBTYPE_PLANET,   2_439_400.0,       0.387 * AU,                         0.0, 0.0, f32::from_bits (0x00AAAAAA),            3.302e23,    &format! ("{}mercury.jpg", sTexBase));
   Node_Create_Child (twRoot, "Venus",    TYPE_CELESTIAL, SUBTYPE_PLANET,   6_051_840.0,       0.723 * AU,                         0.0, 0.0, f32::from_bits (0x00EECC88),            4.869e24,    &format! ("{}venus.jpg",   sTexBase));
   Node_Create_Child (twRoot, "Earth",    TYPE_CELESTIAL, SUBTYPE_PLANET,   6_371_010.0,       1.000 * AU,                         0.0, 0.0, f32::from_bits (0x004488FF),            5.972e24,    &format! ("{}earth.jpg",   sTexBase));
   Node_Create_Child (twRoot, "Mars",     TYPE_CELESTIAL, SUBTYPE_PLANET,   3_389_920.0,       1.524 * AU,                         0.0, 0.0, f32::from_bits (0x00FF6644),            6.417e23,    &format! ("{}mars.jpg",    sTexBase));
   Node_Create_Child (twRoot, "Jupiter",  TYPE_CELESTIAL, SUBTYPE_PLANET,  69_911_000.0,       5.204 * AU,                         0.0, 0.0, f32::from_bits (0x00DDAA66),            1.898e27,    &format! ("{}jupiter.jpg", sTexBase));
   Node_Create_Child (twRoot, "Saturn",   TYPE_CELESTIAL, SUBTYPE_PLANET,  58_232_000.0,       9.581 * AU,                         0.0, 0.0, f32::from_bits (0x00CCBB77),            5.684e26,    &format! ("{}saturn.jpg",  sTexBase));
   Node_Create_Child (twRoot, "Uranus",   TYPE_CELESTIAL, SUBTYPE_PLANET,  25_362_000.0,      19.202 * AU,                         0.0, 0.0, f32::from_bits (0x0066CCDD),            8.682e25,    &format! ("{}uranus.jpg",  sTexBase));
   Node_Create_Child (twRoot, "Neptune",  TYPE_CELESTIAL, SUBTYPE_PLANET,  24_624_000.0,      30.145 * AU,                         0.0, 0.0, f32::from_bits (0x004466FF),            1.024e26,    &format! ("{}neptune.jpg", sTexBase));
   Node_Create_Child (twRoot, "Pluto",    TYPE_CELESTIAL, SUBTYPE_PLANET,   1_188_300.0,      39.281 * AU,                         0.0, 0.0, f32::from_bits (0x00CCAA88),            1.307e22,    &format! ("{}pluto.jpg",   sTexBase));

   LogMsg ("  Solar system complete: Sun + 9 planets (11 nodes)");
}

#[no_mangle]
pub extern "C" fn Close (_twFabricIx: u64)
{
   LogMsg ("Scene test WASM: Close");
}

#[no_mangle]
pub extern "C" fn Shutdown ()
{
   LogMsg ("Scene test WASM: Shutdown");
}
