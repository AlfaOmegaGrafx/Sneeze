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

#ifndef SNEEZE_ENGINE_H
#define SNEEZE_ENGINE_H

namespace SNEEZE
{
   // ---------------------------------------------------------------------------
   // THREAD -- managed thread lifecycle (spawn, signal, shutdown, join)
   // ---------------------------------------------------------------------------

   class THREAD
   {
   public:
      THREAD            ();
      virtual ~THREAD   ();

      bool Initialize   ();
      void Signal       (bool bShutdown = false);

      THREAD            (const THREAD&) = delete;
      THREAD& operator= (const THREAD&) = delete;

   protected:
      virtual void Main () = 0;

      void Ready        (bool bResult = true);
      bool IsShutdown   () const;
      void Wait         (std::function<bool ()> fnWork);
      void Wait         (std::chrono::milliseconds duration);
      void Join         ();

   private:
      std::thread*            m_pthThread;
      std::mutex              m_mxThread;
      std::condition_variable m_cvThread;

      bool                    m_bReady;
      bool                    m_bResult_Initialize;
      bool                    m_bShutdown;
   };
}

#endif // SNEEZE_ENGINE_H
