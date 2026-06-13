#![allow(non_snake_case, non_camel_case_types, dead_code)]

// ---------------------------------------------------------------------------
// Generic map module
//
// This module owns no scene data. On Open it hands the fabric over to the
// browser's map-managed path: a single call to Scene.Node_Map injects the
// entire node tree from the MSF "data" block. This stands in for a real map
// service until network connectivity to one is built in.
// ---------------------------------------------------------------------------

#[link(wasm_import_module = "Console")]
extern "C"
{
   fn Log (dwOffset: u32, dwLength: u32);
}

#[link(wasm_import_module = "Scene")]
extern "C"
{
   fn Node_Map (twFabricIx: u64) -> u64;
}

const OBJECTIX_ERROR: u64 = 0x0000_FFFF_FFFF_FFFE;
const OBJECTIX_NULL:  u64 = 0x0000_0000_0000_0000;

fn LogMsg (sMsg: &str)
{
   unsafe
   {
      Log (sMsg.as_ptr () as u32, sMsg.len () as u32);
   }
}

#[no_mangle]
pub extern "C" fn Init ()
{
   LogMsg ("Map WASM: Init");
}

#[no_mangle]
pub extern "C" fn Open (twFabricIx: u64, _dwOffset: u32, _dwLength: u32)
{
   LogMsg (&format! ("Map WASM: Open (twFabricIx={})", twFabricIx));

   let twRoot = unsafe { Node_Map (twFabricIx) };

   if twRoot == OBJECTIX_NULL  ||  twRoot == OBJECTIX_ERROR
   {
      LogMsg ("  ERROR: Node_Map failed");
   }
   else
   {
      LogMsg (&format! ("  Map loaded: root={}", twRoot));
   }
}

#[no_mangle]
pub extern "C" fn Close (_twFabricIx: u64)
{
   LogMsg ("Map WASM: Close");
}

#[no_mangle]
pub extern "C" fn Shutdown ()
{
   LogMsg ("Map WASM: Shutdown");
}
