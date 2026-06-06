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

   // RMCOBJECT_NAME (96 bytes)
   wsName:                  [u16; 48],

   // RMCOBJECT_TYPE (8 bytes)
   bType:                   u8,
   bSubtype:                u8,
   bFiction:                u8,
   abReserved_Type:         [u8; 5],

   // RMCOBJECT_OWNER (8 bytes)
   twOwner:                 u64,

   // RMCOBJECT_RESOURCE (104 bytes)
   qwResource:              u64,
   sName_Resource:          [u8; 32],
   sReference:              [u8; 64],

   // RMCOBJECT_TRANSFORM (80 bytes)
   d3Position:              [f64; 3],
   d4Rotation:              [f64; 4],
   d3Scale:                 [f64; 3],

   // RMCOBJECT_ORBIT (32 bytes)
   tmPeriod:                i64,
   tmOrigin:                i64,
   dA:                      f64,
   dB:                      f64,

   // RMCOBJECT_BOUND (48 bytes)
   abReserved_Bound:        [u8; 24],
   d3Max:                   [f64; 3],

   // RMCOBJECT_PROPERTIES (32 bytes)
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
}

fn RMCOBJECT_Create (sName: &str, bType: u8, dRadius: f64, fColor: f32) -> RMCOBJECT
{
   let mut RMCObject = RMCOBJECT::New ();
   RMCObject.qwObjectIx_Self = MakeObjectIx (0, OBJECTIX_IDENTITY);
   RMCObject.bType           = bType;
   RMCObject.d3Scale         = [1.0, 1.0, 1.0];
   RMCObject.d3Max           = [dRadius, dRadius, dRadius];
   RMCObject.fColor          = fColor;
   RMCObject.Name_Set (sName);
   RMCObject
}

fn Node_Create_Root (twFabricIx: u64, sName: &str, bType: u8, dRadius: f64, fColor: f32) -> u64
{
   let RMCObject   = RMCOBJECT_Create (sName, bType, dRadius, fColor);
   let dwOffset     = &RMCObject as *const RMCOBJECT as u32;
   let dwLength     = core::mem::size_of::<RMCOBJECT> () as u32;

   unsafe
   {
      Node_Root (twFabricIx, dwOffset, dwLength)
   }
}

fn Node_Create_Child (twParentIx: u64, sName: &str, bType: u8, dRadius: f64, fColor: f32) -> u64
{
   let RMCObject   = RMCOBJECT_Create (sName, bType, dRadius, fColor);
   let dwOffset     = &RMCObject as *const RMCOBJECT as u32;
   let dwLength     = core::mem::size_of::<RMCOBJECT> () as u32;

   unsafe
   {
      Node_Open (twParentIx, dwOffset, dwLength)
   }
}

#[no_mangle]
pub extern "C" fn Init ()
{
   LogMsg ("Scene test WASM: Init");
}

#[no_mangle]
pub extern "C" fn Open (twFabricIx: u64, _dwOffset: u32, _dwLength: u32)
{
   LogMsg (&format! ("Scene test WASM: Open (twFabricIx={})", twFabricIx));

   let twObjectIx_Root = Node_Create_Root (twFabricIx, "Root", 0, 0.0, 0.0);
   LogMsg (&format! ("  Created root node: ix={}", twObjectIx_Root));

   if twObjectIx_Root != 0
   {
      let twObjectIx_Sun = Node_Create_Child (twObjectIx_Root, "Sun", 10, 696340000.0, 1.0);
      LogMsg (&format! ("  Created Sun node: ix={}", twObjectIx_Sun));

      let twObjectIx_Earth = Node_Create_Child (twObjectIx_Root, "Earth", 12, 6371000.0, 0.5);
      LogMsg (&format! ("  Created Earth node: ix={}", twObjectIx_Earth));

      let twObjectIx_Moon = Node_Create_Child (twObjectIx_Earth, "Moon", 13, 1737000.0, 0.3);
      LogMsg (&format! ("  Created Moon node: ix={}", twObjectIx_Moon));

      unsafe
      {
         Node_Position (twObjectIx_Earth, 149_597_870_700.0, 0.0, 0.0);
         Node_Position (twObjectIx_Moon, 384_400_000.0, 0.0, 0.0);

         Node_Scale (twObjectIx_Sun, 1.0);
         Node_Bound (twObjectIx_Sun, 696340000.0);
         Node_Color (twObjectIx_Sun, 0xFFFF00);

         let sName = "Sol";
         Node_Name (twObjectIx_Sun, sName.as_ptr () as u32, sName.len () as u32);

         Node_Radius (twObjectIx_Earth, 6371000.0);
      }

      LogMsg ("  Scene test complete: 4 nodes created");
   }
   else
   {
      LogMsg ("  ERROR: Failed to create root node");
   }
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
