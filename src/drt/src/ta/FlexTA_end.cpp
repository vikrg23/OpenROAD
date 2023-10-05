/* Authors: Lutong Wang and Bangqi Xu */
/*
 * Copyright (c) 2019, The Regents of the University of California
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

#include "distributed/drUpdate.h"
#include "ta/FlexTA.h"

namespace fr {

void FlexTAWorker::saveToGuides()
{
  for (auto& iroute : iroutes_) {
    for (auto& uPinFig : iroute->getFigs()) {
      if (uPinFig->typeId() == tacPathSeg) {
        unique_ptr<frPathSeg> pathSeg
            = make_unique<frPathSeg>(*static_cast<taPathSeg*>(uPinFig.get()));
        if (save_updates_) {
          drUpdate update(drUpdate::ADD_GUIDE);
          update.setPathSeg(*pathSeg);
          update.setIndexInOwner(iroute->getGuide()->getIndexInOwner());
          update.setNet(iroute->getGuide()->getNet());
          design_->addUpdate(update);
        }
        pathSeg->addToNet(iroute->getGuide()->getNet());
        auto guide = iroute->getGuide();
        vector<unique_ptr<frConnFig>> tmp;
        tmp.push_back(std::move(pathSeg));
        guide->setRoutes(tmp);
      }
      // modify upper/lower segs
      // upper/lower seg will have longest wirelength
    }
  }
}

void FlexTAWorker::end()
{
  saveToGuides();
}

}  // namespace fr
