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

#ifndef SNEEZE_PCH_H
#define SNEEZE_PCH_H

// ---------------------------------------------------------------------------
// Sneeze precompiled header.
// CMake's target_precompile_headers force-includes this at the top of every
// .cpp in the Sneeze static library. Only add headers here that are (a) used
// by many translation units, and (b) heavy enough to justify the cost --
// every source file rebuilds when this header changes.
// ---------------------------------------------------------------------------

// --- Standard library ---

#include <unordered_map>
#include <vector>
#include <queue>
#include <fstream>
#include <iostream>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <sstream>
#include <random>

#include <openssl/evp.h>
#include <openssl/sha.h>
#include <openssl/pem.h>

#include <nlohmann/json.hpp>

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#include <wincrypt.h>
#undef X509_NAME
#undef X509_EXTENSIONS

#pragma comment (lib, "winmm.lib")
#endif

// --- Sneeze public umbrella (included by almost every .cpp) ---

#include <Sneeze.h>

std::string NowIso8601 ();

#endif // SNEEZE_PCH_H
