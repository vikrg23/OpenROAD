/* Authors: Matt Liberty */
/*
 * Copyright (c) 2020, The Regents of the University of California
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *     * Neither the name of the University nor the
 *       names of its contributors may be used to endorse or promote products
 *       derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE REGENTS BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef _FR_PROFILE_TASK_H_
#define _FR_PROFILE_TASK_H_

#ifdef HAS_VTUNE
#include <ittnotify.h>
#endif

namespace fr {

#ifdef HAS_VTUNE
// This class make a VTune task in its scope (RAII).  This is useful
// in VTune to see where the runtime is going with more domain specific
// display.
class ProfileTask
{
 public:
  ProfileTask(const char* name) : done_(false)
  {
    domain_ = __itt_domain_create("TritonRoute");
    name_ = __itt_string_handle_create(name);
    __itt_task_begin(domain_, __itt_null, __itt_null, name_);
  }

  ~ProfileTask()
  {
    if (!done_)
      __itt_task_end(domain_);
  }

  // Useful if you don't want to have to introduce a scope
  // just to note a task.
  void done()
  {
    done_ = true;
    __itt_task_end(domain_);
  }

 private:
  __itt_domain* domain_;
  __itt_string_handle* name_;
  bool done_;
};

#else

// No-op version
class ProfileTask
{
 public:
  ProfileTask(const char* name) {}
  void done() {}
};
#endif

}  // namespace fr

#endif
