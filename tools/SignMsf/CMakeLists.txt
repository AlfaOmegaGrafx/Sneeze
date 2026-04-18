# Copyright 2026 Metaversal Corporation
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

# ---------------------------------------------------------------------------
# SignMsf — JWS signing CLI for .msf / .mss files
# ---------------------------------------------------------------------------

add_executable (SignMsf main.cpp)

target_link_libraries (SignMsf PRIVATE Sneeze)

# Sneeze is a static library; the JWS headers it exports (and the jwt-cpp /
# BoringSSL headers transitively pulled in by JwsBase.h's include path) are
# already on the target's PUBLIC include interface.
