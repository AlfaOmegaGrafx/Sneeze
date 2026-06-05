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

#![no_std]

#[panic_handler]
fn panic (_: &core::panic::PanicInfo) -> ! {
   core::arch::wasm32::unreachable ()
}

#[link (wasm_import_module = "Console")]
extern "C" {
   #[link_name = "Log"]
   fn console_log (ptr: i32, len: i32);
}

fn log (msg: &str) {
   unsafe { console_log (msg.as_ptr () as i32, msg.len () as i32) }
}

#[no_mangle]
pub extern "C" fn Init () {
   log ("Hello from WASM!");
}

#[no_mangle]
pub extern "C" fn Shutdown () {
   log ("WASM module shutting down");
}

#[no_mangle]
pub extern "C" fn Open (fabric_id: i32, _params_ptr: i32, _params_len: i32, _reserved: i32) {
   let _ = fabric_id;
   log ("WASM Open called");
}

#[no_mangle]
pub extern "C" fn Close (fabric_id: i32) {
   let _ = fabric_id;
   log ("WASM Close called");
}
