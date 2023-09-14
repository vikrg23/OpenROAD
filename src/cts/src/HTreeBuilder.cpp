/////////////////////////////////////////////////////////////////////////////
//
// BSD 3-Clause License
//
// Copyright (c) 2019, The Regents of the University of California
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//
// * Redistributions of source code must retain the above copyright notice, this
//   list of conditions and the following disclaimer.
//
// * Redistributions in binary form must reproduce the above copyright notice,
//   this list of conditions and the following disclaimer in the documentation
//   and/or other materials provided with the distribution.
//
// * Neither the name of the copyright holder nor the names of its
//   contributors may be used to endorse or promote products derived from
//   this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
// ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
// LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
// CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
// SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
// INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
// CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
// ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
// POSSIBILITY OF SUCH DAMAGE.
//
///////////////////////////////////////////////////////////////////////////////

#include "HTreeBuilder.h"

#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <memory>

#include "Clustering.h"
#include "SinkClustering.h"
#include "utl/Logger.h"

namespace cts {

using utl::CTS;

void HTreeBuilder::preSinkClustering(
    const std::vector<std::pair<float, float>>& sinks,
    const std::vector<const ClockInst*>& sinkInsts,
    const float maxDiameter,
    const unsigned clusterSize,
    const bool secondLevel)
{
  const std::vector<std::pair<float, float>>& points = sinks;
  if (!secondLevel) {
    clock_.forEachSink([&](ClockInst& inst) {
      const Point<double> normLocation((float) inst.getX() / wireSegmentUnit_,
                                       (float) inst.getY() / wireSegmentUnit_);
      mapLocationToSink_[normLocation] = &inst;
    });
  }

  if (sinks.size() <= min_clustering_sinks_
      || !(options_->getSinkClustering())) {
    topLevelSinksClustered_ = sinks;
    return;
  }

  SinkClustering matching(options_, techChar_);
  const unsigned numPoints = points.size();

  for (int pointIdx = 0; pointIdx < numPoints; ++pointIdx) {
    const std::pair<float, float>& point = points[pointIdx];
    matching.addPoint(point.first, point.second);
    if (sinkInsts[pointIdx]->getInputCap() == 0) {
      // Comes here in second level since first level buf cap is not set
      matching.addCap(options_->getSinkBufferInputCap());
    } else {
      matching.addCap(sinkInsts[pointIdx]->getInputCap());
    }
  }
  matching.run(clusterSize, maxDiameter, wireSegmentUnit_);

  unsigned clusterCount = 0;

  std::vector<std::pair<float, float>> newSinkLocations;
  for (const std::vector<unsigned>& cluster :
       matching.sinkClusteringSolution()) {
    if (cluster.size() == 1) {
      const std::pair<float, float>& point = points[cluster[0]];
      newSinkLocations.emplace_back(point);
    }
    if (cluster.size() > 1) {
      std::vector<ClockInst*> clusterClockInsts;  // sink clock insts
      float xSum = 0;
      float ySum = 0;
      for (auto point_idx : cluster) {
        const std::pair<double, double>& point = points[point_idx];
        const Point<double> mapPoint(point.first, point.second);
        xSum += point.first;
        ySum += point.second;
        if (mapLocationToSink_.find(mapPoint) == mapLocationToSink_.end()) {
          logger_->error(CTS, 79, "Sink not found.");
        }
        clusterClockInsts.push_back(mapLocationToSink_[mapPoint]);
        // clock inst needs to be added to the new subnet
      }
      const unsigned pointCounter = cluster.size();
      const float normCenterX
          = (xSum / (float) pointCounter);  // geometric center of cluster
      const float normCenterY = (ySum / (float) pointCounter);
      Point<double> center((double) normCenterX, (double) normCenterY);
      Point<double> legalCenter
          = legalizeOneBuffer(center, options_->getSinkBuffer());
      const char* baseName = secondLevel ? "clkbuf_leaf2_" : "clkbuf_leaf_";
      ClockInst& rootBuffer
          = clock_.addClockBuffer(baseName + std::to_string(clusterCount),
                                  options_->getSinkBuffer(),
                                  legalCenter.getX() * wireSegmentUnit_,
                                  legalCenter.getY() * wireSegmentUnit_);

      if (!secondLevel) {
        addFirstLevelSinkDriver(&rootBuffer);
      } else {
        addSecondLevelSinkDriver(&rootBuffer);
      }

      baseName = secondLevel ? "clknet_leaf2_" : "clknet_leaf_";
      Clock::SubNet& clockSubNet
          = clock_.addSubNet(baseName + std::to_string(clusterCount));
      // Subnet that connects the new -sink- buffer to each specific sink
      clockSubNet.addInst(rootBuffer);
      for (ClockInst* clockInstObj : clusterClockInsts) {
        clockSubNet.addInst(*clockInstObj);
      }
      if (!secondLevel) {
        clockSubNet.setLeafLevel(true);
      }
      const Point<double> newSinkPos(normCenterX, normCenterY);
      const std::pair<float, float> point(normCenterX, normCenterY);
      newSinkLocations.emplace_back(point);
      mapLocationToSink_[newSinkPos] = &rootBuffer;
    }
    clusterCount++;
  }
  topLevelSinksClustered_ = newSinkLocations;
  if (clusterCount) {
    treeBufLevels_++;
  }

  logger_->info(CTS,
                19,
                " Total number of sinks after clustering: {}.",
                topLevelSinksClustered_.size());
}

void HTreeBuilder::initSinkRegion()
{
  const unsigned wireSegmentUnitInDbu = techChar_->getLengthUnit();
  const int dbUnits = options_->getDbUnits();
  wireSegmentUnit_ = wireSegmentUnitInDbu;

  logger_->info(CTS,
                20,
                " Wire segment unit: {}  dbu ({} um).",
                wireSegmentUnit_,
                wireSegmentUnitInDbu / dbUnits);

  if (options_->isSimpleSegmentEnabled()) {
    const int remainingLength
        = options_->getBufferDistance() / (wireSegmentUnitInDbu * 2);
    logger_->info(CTS,
                  21,
                  " Distance between buffers: {} units ({} um).",
                  remainingLength,
                  static_cast<int>(options_->getBufferDistance() / dbUnits));
    if (options_->isVertexBuffersEnabled()) {
      const int vertexBufferLength
          = options_->getVertexBufferDistance() / (wireSegmentUnitInDbu * 2);
      logger_->info(
          CTS,
          22,
          " Branch length for Vertex Buffer: {} units ({} um).",
          vertexBufferLength,
          static_cast<int>(options_->getVertexBufferDistance() / dbUnits));
    }
  }

  std::vector<std::pair<float, float>> topLevelSinks;
  std::vector<const ClockInst*> sinkInsts;
  initTopLevelSinks(topLevelSinks, sinkInsts);

  const float maxDiameter
      = (options_->getMaxDiameter() * dbUnits) / wireSegmentUnit_;

  preSinkClustering(
      topLevelSinks, sinkInsts, maxDiameter, options_->getSizeSinkClustering());
  if (topLevelSinks.size() <= min_clustering_sinks_
      || !(options_->getSinkClustering())) {
    Box<int> sinkRegionDbu = clock_.computeSinkRegion();
    logger_->info(CTS, 23, " Original sink region: {}.", sinkRegionDbu);

    sinkRegion_ = sinkRegionDbu.normalize(1.0 / wireSegmentUnit_);
  } else {
    if (topLevelSinksClustered_.size() > 400
        && options_->getSinkClusteringLevels() > 0) {
      std::vector<std::pair<float, float>> secondLevelLocs;
      std::vector<const ClockInst*> secondLevelInsts;
      initSecondLevelSinks(secondLevelLocs, secondLevelInsts);
      preSinkClustering(secondLevelLocs,
                        secondLevelInsts,
                        maxDiameter * 4,
                        std::ceil(std::sqrt(options_->getSizeSinkClustering())),
                        true);
    }
    sinkRegion_ = clock_.computeSinkRegionClustered(topLevelSinksClustered_);
  }
  logger_->info(CTS, 24, " Normalized sink region: {}.", sinkRegion_);
  logger_->info(CTS, 25, "    Width:  {:.4f}.", sinkRegion_.getWidth());
  logger_->info(CTS, 26, "    Height: {:.4f}.", sinkRegion_.getHeight());
}

// xxx: plot blockages
void plotBlockage(std::ofstream& file, odb::dbDatabase* db_, int z)
{
  unsigned i = 0;
  for (odb::dbBlockage* blockage : db_->getChip()->getBlock()->getBlockages()) {
    // std::string name = inst->getName();
    std::string name = "xxx";
    odb::dbBox* bbox = blockage->getBBox();
    int x = bbox->xMin() / z;
    int y = bbox->yMin() / z;
    int w = bbox->xMax() / z - bbox->xMin() / z;
    int h = bbox->yMax() / z - bbox->yMin() / z;
    file << i++ << " " << x << " " << y << " " << w << " " << h << " block  z=";
    file << z << " " << name << std::endl;
  }
}

//  xxx distance from  legal_loc to original point and all the downstream sinks
double weightedDistance(const Point<double>& legal_loc,
                        const Point<double>& original_loc,
                        const std::vector<Point<double>>& sinks)
{
  // double dist = legal_loc.computeDist( original_loc);
  double dist = 0;
  for (const Point<double>& sink : sinks) {
    dist += legal_loc.computeDist(sink);
    dist += legal_loc.computeDist(original_loc);
  }
  return dist;
}

Point<double> selectBestNewLocation(
    const Point<double>& original_loc,
    const std::vector<Point<double>>& legal_locations,
    const std::vector<Point<double>>& sinks)
{
  Point<double> ans = legal_locations.front();
  double minDist = weightedDistance(ans, original_loc, sinks);
  for (const Point<double>& x : legal_locations) {
    double d = weightedDistance(x, original_loc, sinks);
    if (d < minDist) {  // choose one of legal_locations that is closest to
                        // original_loc
      minDist = d;
      ans = x;
    }
  }
  return ans;
}

void plotSinks(std::ofstream& file, const std::vector<Point<double>>& sinks)
{
  unsigned cnt = 0;
  for (const Point<double>& pt : sinks) {
    double x = pt.getX();
    double y = pt.getY();
    double w = 1;
    double h = 1;
    auto name = "sink_";
    file << cnt++ << " " << x << " " << y << " " << w << " " << h;
    file << " " << name << " " << std::endl;
  }
}

unsigned HTreeBuilder::findSibling(LevelTopology& topology,
                                   unsigned i,
                                   unsigned par)
{
  unsigned idx = 0;
  for (; idx < topology.getBranchingPointSize(); ++idx) {
    unsigned k = topology.getBranchingPointParentIdx(idx);
    if (idx != i && k == par) {
      return idx;
    }
  }
  return i;
}

void scalePosition(Point<double>& loc,
                   const Point<double>& parLoc,
                   double leng,
                   double scale)
{
  double px = parLoc.getX();
  double py = parLoc.getY();
  double ax = loc.getX();
  double ay = loc.getY();

  double d = loc.computeDist(parLoc);
  double x, y;
  if (d > 0) {  // yy8
    double delta = d * scale;
    double dx = ax - px;
    double dy = ay - py;
    dx += (dx > 0) ? delta : -delta;
    dy += (dy > 0) ? -delta : delta;
    double scale = leng / d;
    x = px + dx * scale;
    y = py + dy * scale;
  } else {
    x = px + leng / 2;
    y = py + leng / 2;
  }
  loc.setX(x);
  loc.setY(y);
}

void setSiblingPosition(const Point<double>& a,
                        Point<double>& b,
                        const Point<double>& parLoc)
{
  double px = parLoc.getX();
  double py = parLoc.getY();
  double ax = a.getX();
  double ay = a.getY();
  double bx = 2 * px - ax;
  double by = 2 * py - ay;
  b.setX(bx);
  b.setY(by);
}

/* xxx Balance the two branches on the very top level */
void adjustToplevelTopology(Point<double>& a,
                            Point<double>& b,
                            const Point<double>& parLoc)
{
  double da = a.computeDist(parLoc);
  double db = b.computeDist(parLoc);
  if (da < db) {
    setSiblingPosition(a, b, parLoc);
  } else if (db < da) {
    setSiblingPosition(b, a, parLoc);
  }
}

bool moveOnBlockBounardy(const Point<double>& pt,
                         Point<double>& qt,
                         double& x1,
                         double& y1,
                         double& x2,
                         double& y2)
{
  double px = pt.getX();
  double py = pt.getY();
  double qx = qt.getX();
  double qy = qt.getY();

  // assert ( (!px>=x1 && px <x2 && py>=y1 && py <= y2) ) ;
  // assert ( qx>=x1 && qx <x2 && qy>=y1 && qy <= y2 ) ;

  double dx = px - qx;
  double dy = py - qy;

  std::vector<Point<double>> points;
  if (dx == 0 || dy == 0) {  // vertical or horizontal
    points.emplace_back(qx, y1);
    points.emplace_back(qx, y2);
    points.emplace_back(x1, qy);
    points.emplace_back(x2, qy);
  } else {
    double m = dy / dx;
    points.emplace_back(x1, m * (x1 - qx) + qy);  // y = m*(x-qx) + qy
    points.emplace_back(x2, m * (x2 - qx) + qy);
    points.emplace_back((y1 - qy) / m + qx, y1);  // x = (y-qy)/m + qx
    points.emplace_back((y2 - qy) / m + qx, y2);
  }
  double d1 = pt.computeDist(qt);
  for (Point<double> u : points) {
    double d2 = u.computeDist(pt) + u.computeDist(qt);
    if (abs(d1 - d2) < d1 / 100000) {
      qt.setX(u.getX());
      qt.setY(u.getY());
      return true;
    }
  }

  return false;
}

// Find one blockage that contains qt
// (x1, y1) is the lower left corner
// (x2, y2) is the upper right corner
odb::dbBlockage* findBlockage(
    const Point<double>& qt,
    double z,
    double& x1,  // minx, (x1,y1) is the lower left corner of block
    double& y1,  // miny, (x2,y2) is the upper right corner of block
    double& x2,
    double& y2,
    odb::dbDatabase* db_)
{
  double qx = qt.getX();
  double qy = qt.getY();

  for (odb::dbBlockage* blockage : db_->getChip()->getBlock()->getBlockages()) {
    // std::string name = inst->getName();
    std::string name = "xxx";
    odb::dbBox* bbox = blockage->getBBox();
    x1 = bbox->xMin() / z;
    y1 = bbox->yMin() / z;
    x2 = bbox->xMax() / z;
    y2 = bbox->yMax() / z;

    bool inside = qx > x1 && qx < x2 && qy > y1 && qy < y2;
    if (inside) {
      return blockage;
    }
  }
  return nullptr;
}

void findLegalPlacement(
    const Point<double>& pt,
    unsigned leng,
    double x1,
    double y1,
    double x2,
    double y2,
    std::vector<Point<double>>& points  // candidate new locations
)
{
  double px = pt.getX();
  double py = pt.getY();
  std::vector<Point<double>> temp;
  for (int i = 0; i < 2; ++i) {
    double x = (i == 0) ? x1 : x2;
    double y = (i == 0) ? y1 : y2;
    double dx = leng - abs(px - x);
    double dy = leng - abs(py - y);
    if (x >= px - leng && x <= px + leng) {
      temp.emplace_back(x, py + dx);
      temp.emplace_back(x, py - dx);
    }
    if (y >= py - leng && y <= py + leng) {
      temp.emplace_back(px + dy, y);
      temp.emplace_back(px - dy, y);
    }
  }
  for (Point<double>& tt : temp) {
    double x = tt.getX();
    double y = tt.getY();
    if (x >= x1 && x <= x2 && y >= y1 && y <= y2) {
      points.emplace_back(x, y);
    }
  }
  if (points.empty()) {
    points.emplace_back(px - leng, py);
    points.emplace_back(px + leng, py);
    points.emplace_back(px, py + leng);
    points.emplace_back(px, py - leng);

    points.emplace_back(px - (double) leng / 2, py + (double) leng / 2);
    points.emplace_back(px + (double) leng / 2, py + (double) leng / 2);
    points.emplace_back(px - (double) leng / 2, py - (double) leng / 2);
    points.emplace_back(px + (double) leng / 2, py - (double) leng / 2);
  }
}

// xxxplot
void HTreeBuilder::legalizeDummy()
{
  Point<double> topLevelBufferLoc = sinkRegion_.computeCenter();
  for (int levelIdx = 0; levelIdx < topologyForEachLevel_.size(); ++levelIdx) {
    LevelTopology& topology = topologyForEachLevel_[levelIdx];

    for (unsigned idx = 0; idx < topology.getBranchingPointSize(); ++idx) {
      Point<double>& branchPoint = topology.getBranchingPoint(idx);
      unsigned parentIdx = topology.getBranchingPointParentIdx(idx);

      Point<double> parentPoint
          = (levelIdx == 0)
                ? topLevelBufferLoc
                : topologyForEachLevel_[levelIdx - 1].getBranchingPoint(
                    parentIdx);

      const std::vector<Point<double>>& sinks
          = topology.getBranchSinksLocations(idx);

      double leng = topology.getLength();
      Point<double>& sibLoc = findSiblingLoc(topology, idx, parentIdx);

      double d1 = branchPoint.computeDist(sibLoc);
      double d2 = branchPoint.computeDist(parentPoint);
      bool overlap = d1 == 0 || d2 == 0;
      bool dummy = sinks.empty();  // xxx dummy buffers drive no sinks

      // xxx not important, can be removed
      if (dummy) {
        setSiblingPosition(sibLoc, branchPoint, parentPoint);
        scalePosition(branchPoint, parentPoint, leng, 0.1);
      } else if (overlap) {
        scalePosition(branchPoint, parentPoint, leng, 0.1);
      } else {
        continue;
      }

      double x1, y1, x2, y2;
      int z = wireSegmentUnit_;
      odb::dbBlockage* obs = findBlockage(branchPoint, z, x1, y1, x2, y2, db_);

      if (obs != nullptr) {  // xxx qt is inside the block obs
        Point<double> ans(branchPoint);
        std::vector<Point<double>> legal_locations;
        findLegalPlacement(parentPoint, leng, x1, y1, x2, y2, legal_locations);
        ans = selectBestNewLocation(branchPoint, legal_locations, sinks);

        double d = ans.computeDist(parentPoint);
        logger_->report("yy9 dummy {}:{}-->{} d={},leng={},ratio={} ",
                        levelIdx,
                        branchPoint,
                        ans,
                        d,
                        leng,
                        d / leng);
        branchPoint.setX(ans.getX());
        branchPoint.setY(ans.getY());
      }
    }
  }
}

void HTreeBuilder::legalize()
{
  Point<double> topLevelBufferLoc = sinkRegion_.computeCenter();
  for (int levelIdx = 0; levelIdx < topologyForEachLevel_.size(); ++levelIdx) {
    LevelTopology& topology = topologyForEachLevel_[levelIdx];

    for (unsigned idx = 0; idx < topology.getBranchingPointSize(); ++idx) {
      // idx is the buffer id at level levelIdx
      Point<double>& branchPoint = topology.getBranchingPoint(idx);
      unsigned parentIdx = topology.getBranchingPointParentIdx(idx);

      Point<double> parentPoint
          = (levelIdx == 0)
                ? topLevelBufferLoc
                : topologyForEachLevel_[levelIdx - 1].getBranchingPoint(
                    parentIdx);

      const std::vector<Point<double>>& sinks
          = topology.getBranchSinksLocations(idx);
      // if ( sinks.empty() ) continue ;

      Point<double>& qt = branchPoint;
      double leng = topology.getLength();

      // xxx may change topLevelBufferLoc even when it is not inside blockage
      /*
        if ( levelIdx==1 && idx==0 ) { // only for the root that is at the
        top-evel buffer (at LevelTopology& top = topologyForEachLevel_[0];
        Point<double>& a  = top.getBranchingPoint(0) ; // left child
        Point<double>& b  = top.getBranchingPoint(1) ; // right child
        adjustToplevelTopology ( a,b, topLevelBufferLoc );
        double da = a.computeDist ( topLevelBufferLoc );
        double db = b.computeDist ( topLevelBufferLoc );
        logger_->report("yy8 da,db = {},{}", da, db );
        }
      */

      int z = wireSegmentUnit_;
      double x1, y1, x2, y2;
      odb::dbBlockage* obs = findBlockage(qt, z, x1, y1, x2, y2, db_);
      if (obs != nullptr) {
        Point<double> ans(qt);
        if (levelIdx == 0) {
          bool moved = moveOnBlockBounardy(parentPoint, ans, x1, y1, x2, y2);
          if (moved) {
            // logger_->report("xxx yy8 top {}:{}-->{} ", levelIdx, qt, ans );
          }
        } else {
          if (levelIdx == 1) {
            leng = qt.computeDist(parentPoint);
            topology.setLength(leng);
          }
          std::vector<Point<double>> points;
          // find all the possible locations off the blockage
          findLegalPlacement(parentPoint, leng, x1, y1, x2, y2, points);
          // std::string name = obs->getName();
          std::string name = "xxx";
          // choose the best new location
          ans = selectBestNewLocation(qt, points, sinks);
          if (levelIdx == 2) {
            double moved = ans.computeDist(qt);
            logger_->report(
                "xxx y8 top {}:{}-->{}, moved={} ", levelIdx, qt, ans, moved);
          }
        }
        // set qt to be the answer
        qt.setX(ans.getX());
        qt.setY(ans.getY());
      }
    }
  }

  // optioanl: "further" optimize the location of the "dummy" buffers that drive
  // no sinks
  legalizeDummy();
}

//
// Legalize one buffer (can be L0, L1, L2 or leaf buffer)
// bufferLoc needs to in non-dbu units: without wireSegmentUnit_ multiplier
// bufferName is a string that contains name of buffer master cell
//
// Cho Moon - Sep 1, 2023
Point<double> HTreeBuilder::legalizeOneBuffer(Point<double> bufferLoc,
                                              std::string bufferName)
{
  if (options_->getObstructionAware()) {
    odb::dbMaster* libCell = db_->findMaster(bufferName.c_str());
    assert(libCell != nullptr);
    // check if current buffer sits on top of blockage
    double x1, y1, x2, y2;
    odb::dbBlockage* obs
        = findBlockage(bufferLoc, wireSegmentUnit_, x1, y1, x2, y2, db_);
    if (obs != nullptr) {
      // x1, y1 are lower left corner of blockage
      // x2, y2 are upper right corner of blockage
      // move buffer to the nearest legal location by snapping it to right,
      // left, top or bottom need to consider cell height and width to avoid any
      // overlap with blockage
      Point<double> newLoc = bufferLoc;
      // first, try snapping it to the left
      double delta = bufferLoc.getX() - x1;
      double minDist = delta;
      newLoc.setX(x1 - ((double) libCell->getWidth() / wireSegmentUnit_));
      // second, try snapping it to the right
      delta = x2 - bufferLoc.getX();
      if (delta < minDist) {
        minDist = delta;
        newLoc.setX(x2);
      }
      // third, try snapping it to the bottom
      delta = bufferLoc.getY() - y1;
      if (delta < minDist) {
        minDist = delta;
        newLoc.setX(bufferLoc.getX());
        newLoc.setY(y1 - ((double) libCell->getHeight() / wireSegmentUnit_));
      }
      // fourth, try snapping it to the top
      delta = y2 - bufferLoc.getY();
      if (delta < minDist) {
        newLoc.setX(bufferLoc.getX());
        newLoc.setY(y2);
      }

      return newLoc;
    }
  }
  return bufferLoc;
}

void HTreeBuilder::run()
{
  logger_->info(
      CTS, 27, "Generating H-Tree topology for net {}.", clock_.getName());
  logger_->info(CTS, 28, " Total number of sinks: {}.", clock_.getNumSinks());
  if (options_->getSinkClustering()) {
    if (options_->getSinkClusteringUseMaxCap()) {
      logger_->info(
          CTS, 90, " Sinks will be clustered based on buffer max cap.");
    } else {
      logger_->info(CTS,
                    29,
                    " Sinks will be clustered in groups of up to {} and with "
                    "maximum cluster diameter of {:.1f} um.",
                    options_->getSizeSinkClustering(),
                    options_->getMaxDiameter());
    }
  }
  logger_->info(
      CTS, 30, " Number of static layers: {}.", options_->getNumStaticLayers());

  clockTreeMaxDepth_ = options_->getClockTreeMaxDepth();
  minInputCap_ = techChar_->getActualMinInputCap();
  numMaxLeafSinks_ = options_->getNumMaxLeafSinks();
  minLengthSinkRegion_ = techChar_->getMinSegmentLength() * 2;

  initSinkRegion();

  for (int level = 1; level <= clockTreeMaxDepth_; ++level) {
    const unsigned numSinksPerSubRegion
        = computeNumberOfSinksPerSubRegion(level);
    double regionWidth, regionHeight;
    computeSubRegionSize(level, regionWidth, regionHeight);

    if (isSubRegionTooSmall(regionWidth, regionHeight)) {
      if (options_->isFakeLutEntriesEnabled()) {
        const unsigned minIndex = 1;
        techChar_->createFakeEntries(minLengthSinkRegion_, minIndex);
        minLengthSinkRegion_ = 1;
      } else {
        logger_->info(
            CTS,
            31,
            " Stop criterion found. Min length of sink region is ({}).",
            minLengthSinkRegion_);
        break;
      }
    }

    computeLevelTopology(level, regionWidth, regionHeight);

    if (isNumberOfSinksTooSmall(numSinksPerSubRegion)) {
      logger_->info(CTS,
                    32,
                    " Stop criterion found. Max number of sinks is {}.",
                    numMaxLeafSinks_);
      break;
    }
  }

  if (topologyForEachLevel_.empty()) {
    createSingleBufferClockNet();
    treeBufLevels_++;
    return;
  }

  clock_.setMaxLevel(topologyForEachLevel_.size());

  if (options_->getPlotSolution()
      || logger_->debugCheck(utl::CTS, "HTree", 2)) {
    plotSolution();
  }

  if (CtsObserver* observer = options_->getObserver()) {
    observer->initializeWithClock(this, clock_);
  }

  if (options_->getObstructionAware()) {
    legalize();  // xxx move buffers to legal location
  }
  createClockSubNets();
  // plotHTree(); // xxxplot
}

void HTreeBuilder::plotHTree()
{
  auto name = std::string("xxxy.") + clock_.getName() + ".buffer";
  std::ofstream file(name);

  plotBlockage(file, db_, wireSegmentUnit_);

  Point<double> topLevelBufferLoc = sinkRegion_.computeCenter();

  for (int levelIdx = 0; levelIdx < topologyForEachLevel_.size(); ++levelIdx) {
    LevelTopology& topology = topologyForEachLevel_[levelIdx];

    topology.forEachBranchingPoint(
        [&](unsigned idx, Point<double> branchPoint) {
          unsigned parentIdx = topology.getBranchingPointParentIdx(idx);

          Point<double> parentPoint
              = (levelIdx == 0)
                    ? topLevelBufferLoc
                    : topologyForEachLevel_[levelIdx - 1].getBranchingPoint(
                        parentIdx);

          const std::vector<Point<double>>& sinks
              = topology.getBranchSinksLocations(idx);

          plotSinks(file, sinks);

          double x1 = parentPoint.getX();
          double y1 = parentPoint.getY();
          double x2 = branchPoint.getX();
          double y2 = branchPoint.getY();
          std::string name = "buffer";
          file << levelIdx << " " << x1 << " " << y1 << " " << x2 << " " << y2;
          file << " " << name << std::endl;
        });
  }

  LevelTopology& leafTopology = topologyForEachLevel_.back();
  unsigned numSinks = 0;
  leafTopology.forEachBranchingPoint(
      [&](unsigned idx, Point<double> branchPoint) {
        double px = branchPoint.getX();
        double py = branchPoint.getY();

        const std::vector<Point<double>>& sinkLocs
            = leafTopology.getBranchSinksLocations(idx);

        for (const Point<double>& loc : sinkLocs) {
          auto name2 = mapLocationToSink_[loc]->getName();

          file << numSinks << " " << loc.getX() << " " << loc.getY();
          file << " " << px << " " << py << " leafbuffer " << name2;
          file << " z=" << wireSegmentUnit_ << std::endl;
          ++numSinks;
        }
      });
  file.close();
}

unsigned HTreeBuilder::computeNumberOfSinksPerSubRegion(
    const unsigned level) const
{
  unsigned totalNumSinks = 0;
  if (clock_.getNumSinks() > min_clustering_sinks_
      && options_->getSinkClustering()) {
    totalNumSinks = topLevelSinksClustered_.size();
  } else {
    totalNumSinks = clock_.getNumSinks();
  }
  const unsigned numRoots = std::pow(2, level);
  const double numSinksPerRoot = (double) totalNumSinks / numRoots;
  return std::ceil(numSinksPerRoot);
}

void HTreeBuilder::computeSubRegionSize(const unsigned level,
                                        double& width,
                                        double& height) const
{
  unsigned gridSizeX = 0;
  unsigned gridSizeY = 0;
  if (isVertical(1)) {
    gridSizeY = computeGridSizeX(level);
    gridSizeX = computeGridSizeY(level);
  } else {
    gridSizeX = computeGridSizeX(level);
    gridSizeY = computeGridSizeY(level);
  }
  width = sinkRegion_.getWidth() / gridSizeX;
  height = sinkRegion_.getHeight() / gridSizeY;
}

void HTreeBuilder::computeLevelTopology(const unsigned level,
                                        const double width,
                                        const double height)
{
  const unsigned numSinksPerSubRegion = computeNumberOfSinksPerSubRegion(level);
  logger_->report(" Level {}", level);
  logger_->report("    Direction: {}",
                  (isVertical(level)) ? ("Vertical") : ("Horizontal"));
  logger_->report("    Sinks per sub-region: {}", numSinksPerSubRegion);
  logger_->report("    Sub-region size: {:.4f} X {:.4f}", width, height);

  const unsigned minLength = minLengthSinkRegion_;
  const unsigned clampedMinLength = std::max(minLength, 1u);

  unsigned segmentLength
      = std::round(width / (double) clampedMinLength) * minLength / 2;

  if (isVertical(level)) {
    segmentLength
        = std::round(height / (double) clampedMinLength) * minLength / 2;
  }
  segmentLength = std::max<unsigned>(segmentLength, 1);

  LevelTopology topology(segmentLength);

  logger_->info(CTS, 34, "    Segment length (rounded): {}.", segmentLength);

  const int vertexBufferLength
      = options_->getVertexBufferDistance() / (techChar_->getLengthUnit() * 2);
  int remainingLength
      = options_->getBufferDistance() / (techChar_->getLengthUnit());
  unsigned inputCap = minInputCap_;
  unsigned inputSlew = 1;
  if (level > 1) {
    const LevelTopology& previousLevel = topologyForEachLevel_[level - 2];
    inputCap = previousLevel.getOutputCap();
    inputSlew = previousLevel.getOutputSlew();
    remainingLength = previousLevel.getRemainingLength();
  }

  const unsigned SLEW_THRESHOLD = options_->getMaxSlew();
  const unsigned INIT_TOLERANCE = 1;
  unsigned length = 0;
  for (int charSegLength = techChar_->getMaxSegmentLength(); charSegLength >= 1;
       --charSegLength) {
    const unsigned numWires = (segmentLength - length) / charSegLength;

    if (numWires >= 1) {
      for (int wireCount = 0; wireCount < numWires; ++wireCount) {
        unsigned outCap = 0, outSlew = 0;
        unsigned key = 0;
        if (options_->isSimpleSegmentEnabled()) {
          remainingLength -= charSegLength;

          if (segmentLength >= vertexBufferLength && (wireCount + 1 >= numWires)
              && options_->isVertexBuffersEnabled()) {
            remainingLength = 0;
            key = computeMinDelaySegment(charSegLength,
                                         inputSlew,
                                         inputCap,
                                         SLEW_THRESHOLD,
                                         INIT_TOLERANCE,
                                         outSlew,
                                         outCap,
                                         true,
                                         remainingLength);
            remainingLength
                += options_->getBufferDistance() / (techChar_->getLengthUnit());
          } else {
            if (remainingLength <= 0) {
              key = computeMinDelaySegment(charSegLength,
                                           inputSlew,
                                           inputCap,
                                           SLEW_THRESHOLD,
                                           INIT_TOLERANCE,
                                           outSlew,
                                           outCap,
                                           true,
                                           remainingLength);
              remainingLength += options_->getBufferDistance()
                                 / (techChar_->getLengthUnit());
            } else {
              key = computeMinDelaySegment(charSegLength,
                                           inputSlew,
                                           inputCap,
                                           SLEW_THRESHOLD,
                                           INIT_TOLERANCE,
                                           outSlew,
                                           outCap,
                                           false,
                                           remainingLength);
            }
          }
        } else {
          key = computeMinDelaySegment(charSegLength,
                                       inputSlew,
                                       inputCap,
                                       SLEW_THRESHOLD,
                                       INIT_TOLERANCE,
                                       outSlew,
                                       outCap);
        }

        if (key == std::numeric_limits<unsigned>::max()) {
          // No tech char entry found.
          continue;
        }

        length += charSegLength;
        techChar_->reportSegment(key);

        inputCap = std::max(outCap, minInputCap_);
        inputSlew = outSlew;
        topology.addWireSegment(key);
        topology.setRemainingLength(remainingLength);
      }

      if (length == segmentLength) {
        break;
      }
    }
  }

  topology.setOutputSlew(inputSlew);
  topology.setOutputCap(inputCap);

  computeBranchingPoints(level, topology);
  topologyForEachLevel_.push_back(topology);
}

unsigned HTreeBuilder::computeMinDelaySegment(const unsigned length) const
{
  unsigned minKey = std::numeric_limits<unsigned>::max();
  unsigned minDelay = std::numeric_limits<unsigned>::max();

  techChar_->forEachWireSegment(
      length, 1, 1, [&](unsigned key, const WireSegment& seg) {
        if (!seg.isBuffered()) {
          return;
        }
        if (seg.getDelay() < minDelay) {
          minKey = key;
          minDelay = seg.getDelay();
        }
      });

  return minKey;
}

unsigned HTreeBuilder::computeMinDelaySegment(const unsigned length,
                                              const unsigned inputSlew,
                                              const unsigned inputCap,
                                              const unsigned slewThreshold,
                                              const unsigned tolerance,
                                              unsigned& outputSlew,
                                              unsigned& outputCap) const
{
  unsigned minKey = std::numeric_limits<unsigned>::max();
  unsigned minDelay = std::numeric_limits<unsigned>::max();
  unsigned minBufKey = std::numeric_limits<unsigned>::max();
  unsigned minBufDelay = std::numeric_limits<unsigned>::max();

  for (int load = 1; load <= techChar_->getMaxCapacitance(); ++load) {
    for (int outSlew = 1; outSlew <= techChar_->getMaxSlew(); ++outSlew) {
      techChar_->forEachWireSegment(
          length, load, outSlew, [&](unsigned key, const WireSegment& seg) {
            if (std::abs((int) seg.getInputCap() - (int) inputCap) > tolerance
                || std::abs((int) seg.getInputSlew() - (int) inputSlew)
                       > tolerance) {
              return;
            }

            if (seg.getDelay() < minDelay) {
              minDelay = seg.getDelay();
              minKey = key;
            }

            if (seg.isBuffered() && seg.getDelay() < minBufDelay) {
              minBufDelay = seg.getDelay();
              minBufKey = key;
            }
          });
    }
  }

  const unsigned MAX_TOLERANCE = 10;
  if (inputSlew >= slewThreshold) {
    if (minBufKey < std::numeric_limits<unsigned>::max()) {
      const WireSegment& bestBufSegment = techChar_->getWireSegment(minBufKey);
      outputSlew = bestBufSegment.getOutputSlew();
      outputCap = bestBufSegment.getLoad();
      return minBufKey;
    }
    if (tolerance < MAX_TOLERANCE) {
      // Increasing tolerance
      return computeMinDelaySegment(length,
                                    inputSlew,
                                    inputCap,
                                    slewThreshold,
                                    tolerance + 1,
                                    outputSlew,
                                    outputCap);
    }
  }

  if (minKey == std::numeric_limits<unsigned>::max()) {
    if (tolerance >= MAX_TOLERANCE) {
      return minKey;
    }
    // Increasing tolerance
    return computeMinDelaySegment(length,
                                  inputSlew,
                                  inputCap,
                                  slewThreshold,
                                  tolerance + 1,
                                  outputSlew,
                                  outputCap);
  }

  const WireSegment& bestSegment = techChar_->getWireSegment(minKey);
  outputSlew = std::max((unsigned) bestSegment.getOutputSlew(), inputSlew + 1);
  outputCap = bestSegment.getLoad();

  return minKey;
}

unsigned HTreeBuilder::computeMinDelaySegment(const unsigned length,
                                              const unsigned inputSlew,
                                              const unsigned inputCap,
                                              const unsigned slewThreshold,
                                              const unsigned tolerance,
                                              unsigned& outputSlew,
                                              unsigned& outputCap,
                                              const bool forceBuffer,
                                              const int expectedLength) const
{
  unsigned minKey = std::numeric_limits<unsigned>::max();
  unsigned minDelay = std::numeric_limits<unsigned>::max();
  unsigned minBufKey = std::numeric_limits<unsigned>::max();
  unsigned minBufDelay = std::numeric_limits<unsigned>::max();
  unsigned minBufKeyFallback = std::numeric_limits<unsigned>::max();
  unsigned minDelayFallback = std::numeric_limits<unsigned>::max();

  for (int load = 1; load <= techChar_->getMaxCapacitance(); ++load) {
    for (int outSlew = 1; outSlew <= techChar_->getMaxSlew(); ++outSlew) {
      techChar_->forEachWireSegment(
          length, load, outSlew, [&](unsigned key, const WireSegment& seg) {
            // Same as the other functions, however, forces a segment
            // to have a buffer in a specific location.
            const unsigned normalLength = length;
            if (!seg.isBuffered() && seg.getDelay() < minDelay) {
              minDelay = seg.getDelay();
              minKey = key;
            }
            if (seg.isBuffered() && seg.getDelay() < minBufDelay
                && seg.getNumBuffers() == 1) {
              // If buffer is in the range of 10% of the expected location, save
              // its key.
              if (seg.getBufferLocation(0)
                      > ((((double) normalLength + (double) expectedLength)
                          / (double) normalLength)
                         * 0.9)
                  && seg.getBufferLocation(0)
                         < ((((double) normalLength + (double) expectedLength)
                             / (double) normalLength)
                            * 1.1)) {
                minBufDelay = seg.getDelay();
                minBufKey = key;
              }
              if (seg.getDelay() < minDelayFallback) {
                minDelayFallback = seg.getDelay();
                minBufKeyFallback = key;
              }
            }
          });
    }
  }

  if (forceBuffer) {
    if (minBufKey != std::numeric_limits<unsigned>::max()) {
      return minBufKey;
    }

    if (minBufKeyFallback != std::numeric_limits<unsigned>::max()) {
      return minBufKeyFallback;
    }
  }

  return minKey;
}

void HTreeBuilder::computeBranchingPoints(const unsigned level,
                                          LevelTopology& topology)
{
  if (level == 1) {
    const Point<double> clockRoot(sinkRegion_.computeCenter());
    Point<double> low(clockRoot);
    Point<double> high(clockRoot);
    if (isHorizontal(level)) {
      low.setX(low.getX() - topology.getLength());
      high.setX(high.getX() + topology.getLength());
    } else {
      low.setY(low.getY() - topology.getLength());
      high.setY(high.getY() + topology.getLength());
    }
    const unsigned branchPtIdx1
        = topology.addBranchingPoint(low, LevelTopology::NO_PARENT);
    const unsigned branchPtIdx2
        = topology.addBranchingPoint(high, LevelTopology::NO_PARENT);

    refineBranchingPointsWithClustering(topology,
                                        level,
                                        branchPtIdx1,
                                        branchPtIdx2,
                                        clockRoot,
                                        topLevelSinksClustered_);
    return;
  }

  LevelTopology& parentTopology = topologyForEachLevel_[level - 2];
  parentTopology.forEachBranchingPoint(
      [&](unsigned idx, Point<double> clockRoot) {
        Point<double> low(clockRoot);
        Point<double> high(clockRoot);
        if (isHorizontal(level)) {
          low.setX(low.getX() - topology.getLength());
          high.setX(high.getX() + topology.getLength());
        } else {
          low.setY(low.getY() - topology.getLength());
          high.setY(high.getY() + topology.getLength());
        }
        const unsigned branchPtIdx1 = topology.addBranchingPoint(low, idx);
        const unsigned branchPtIdx2 = topology.addBranchingPoint(high, idx);

        std::vector<std::pair<float, float>> sinks;
        computeBranchSinks(parentTopology, idx, sinks);
        refineBranchingPointsWithClustering(
            topology, level, branchPtIdx1, branchPtIdx2, clockRoot, sinks);
      });
}

void HTreeBuilder::initTopLevelSinks(
    std::vector<std::pair<float, float>>& sinkLocations,
    std::vector<const ClockInst*>& sinkInsts)
{
  sinkLocations.clear();
  clock_.forEachSink([&](const ClockInst& sink) {
    sinkLocations.emplace_back((float) sink.getX() / wireSegmentUnit_,
                               (float) sink.getY() / wireSegmentUnit_);
    sinkInsts.emplace_back(&sink);
  });
}

void HTreeBuilder::initSecondLevelSinks(
    std::vector<std::pair<float, float>>& sinkLocations,
    std::vector<const ClockInst*>& sinkInsts)
{
  sinkLocations.clear();
  for (const auto& buf : topLevelSinksClustered_) {
    sinkLocations.emplace_back(buf.first, buf.second);
    const Point<double> bufPos(buf.first, buf.second);
    sinkInsts.emplace_back(mapLocationToSink_[bufPos]);
  }
}

void HTreeBuilder::computeBranchSinks(
    const LevelTopology& topology,
    const unsigned branchIdx,
    std::vector<std::pair<float, float>>& sinkLocations) const
{
  sinkLocations.clear();
  for (const Point<double>& point :
       topology.getBranchSinksLocations(branchIdx)) {
    sinkLocations.emplace_back(point.getX(), point.getY());
  }
}

void HTreeBuilder::refineBranchingPointsWithClustering(
    LevelTopology& topology,
    const unsigned level,
    const unsigned branchPtIdx1,
    const unsigned branchPtIdx2,
    const Point<double>& rootLocation,
    const std::vector<std::pair<float, float>>& sinks)
{
  CKMeans::Clustering clusteringEngine(
      sinks, rootLocation.getX(), rootLocation.getY(), logger_);

  Point<double>& branchPt1 = topology.getBranchingPoint(branchPtIdx1);
  Point<double>& branchPt2 = topology.getBranchingPoint(branchPtIdx2);
#ifndef NDEBUG
  const double targetDist = branchPt2.computeDist(rootLocation);
#endif

  std::vector<std::pair<float, float>> means;
  means.emplace_back(branchPt1.getX(), branchPt1.getY());
  means.emplace_back(branchPt2.getX(), branchPt2.getY());

  const unsigned cap
      = (unsigned) (sinks.size() * options_->getClusteringCapacity());
  clusteringEngine.iterKmeans(
      1, means.size(), cap, 5, options_->getClusteringPower(), means);

  if (((int) options_->getNumStaticLayers() - (int) level) < 0) {
    branchPt1 = Point<double>(means[0].first, means[0].second);
    branchPt2 = Point<double>(means[1].first, means[1].second);
  }

  std::vector<std::vector<unsigned>> clusters;
  clusteringEngine.getClusters(clusters);
  unsigned movedSinks = 0;
  const double errorFactor = 1.2;
  for (int clusterIdx = 0; clusterIdx < clusters.size(); ++clusterIdx) {
    for (int elementIdx = 0; elementIdx < clusters[clusterIdx].size();
         ++elementIdx) {
      const unsigned sinkIdx = clusters[clusterIdx][elementIdx];
      const Point<double> sinkLoc(sinks[sinkIdx].first, sinks[sinkIdx].second);
      const double dist = clusterIdx == 0 ? branchPt1.computeDist(sinkLoc)
                                          : branchPt2.computeDist(sinkLoc);
      const double distOther = clusterIdx == 0 ? branchPt2.computeDist(sinkLoc)
                                               : branchPt1.computeDist(sinkLoc);
      if (clusterIdx == 0) {
        topology.addSinkToBranch(branchPtIdx1, sinkLoc);
      } else {
        topology.addSinkToBranch(branchPtIdx2, sinkLoc);
      }
      // bool ok = (clusterIdx == 0) == ( dist < distOther ); // xxx y8
      // if ( ok ) { /// xxx y8
      // topology.addSinkToBranch(branchPtIdx1, sinkLoc); /// xxx y8
      // } else { /// xxx y8
      // topology.addSinkToBranch(branchPtIdx2, sinkLoc); /// xxx y8
      //}

      if (dist >= distOther * errorFactor) {
        movedSinks++;
      }
    }
  }

  if (movedSinks > 0) {
    logger_->report(" Out of {} sinks, {} sinks closer to other cluster.",
                    sinks.size(),
                    movedSinks);
  }

  assert(std::abs(branchPt1.computeDist(rootLocation) - targetDist) < 0.001
         && std::abs(branchPt2.computeDist(rootLocation) - targetDist) < 0.001);
}

void HTreeBuilder::createClockSubNets()
{
  Point<double> legalCenter = legalizeOneBuffer(sinkRegion_.computeCenter(),
                                                options_->getRootBuffer());
  const int centerX = legalCenter.getX() * wireSegmentUnit_;
  const int centerY = legalCenter.getY() * wireSegmentUnit_;

  ClockInst& rootBuffer = clock_.addClockBuffer(
      "clkbuf_0", options_->getRootBuffer(), centerX, centerY);
  addTreeLevelBuffer(&rootBuffer);
  Clock::SubNet& rootClockSubNet = clock_.addSubNet("clknet_0");
  rootClockSubNet.addInst(rootBuffer);
  treeBufLevels_++;

  // First level...
  LevelTopology& topLevelTopology = topologyForEachLevel_[0];
  bool isFirstPoint = true;
  topLevelTopology.forEachBranchingPoint([&](unsigned idx,
                                             Point<double> branchPoint) {
    Point<double> legalBranchPoint
        = legalizeOneBuffer(branchPoint, options_->getRootBuffer());
    SegmentBuilder builder("clkbuf_1_" + std::to_string(idx) + "_",
                           "clknet_1_" + std::to_string(idx) + "_",
                           legalCenter,  // center may have moved, don't use
                                         // sinkRegion_.computeCenter()
                           legalBranchPoint,
                           topLevelTopology.getWireSegments(),
                           clock_,
                           rootClockSubNet,
                           *techChar_,
                           wireSegmentUnit_,
                           this);
    if (!options_->getTreeBuffer().empty()) {
      builder.build(options_->getTreeBuffer());
    } else {
      builder.build();
    }
    if (topologyForEachLevel_.size() == 1) {
      builder.forceBufferInSegment(options_->getRootBuffer());
    }
    if (isFirstPoint) {
      treeBufLevels_ += builder.getNumBufferLevels();
      isFirstPoint = false;
    }
    topLevelTopology.setBranchDrivingSubNet(idx, *builder.getDrivingSubNet());
  });

  // Others...
  for (int levelIdx = 1; levelIdx < topologyForEachLevel_.size(); ++levelIdx) {
    LevelTopology& topology = topologyForEachLevel_[levelIdx];
    isFirstPoint = true;
    topology.forEachBranchingPoint([&](unsigned idx,
                                       Point<double> branchPoint) {
      unsigned parentIdx = topology.getBranchingPointParentIdx(idx);
      LevelTopology& parentTopology = topologyForEachLevel_[levelIdx - 1];
      Point<double> parentPoint = parentTopology.getBranchingPoint(parentIdx);

      Point<double> legalBranchPoint
          = legalizeOneBuffer(branchPoint, options_->getRootBuffer());
      SegmentBuilder builder("clkbuf_" + std::to_string(levelIdx + 1) + "_"
                                 + std::to_string(idx) + "_",
                             "clknet_" + std::to_string(levelIdx + 1) + "_"
                                 + std::to_string(idx) + "_",
                             parentPoint,
                             legalBranchPoint,
                             topology.getWireSegments(),
                             clock_,
                             *parentTopology.getBranchDrivingSubNet(parentIdx),
                             *techChar_,
                             wireSegmentUnit_,
                             this);
      if (!options_->getTreeBuffer().empty()) {
        builder.build(options_->getTreeBuffer());
      } else {
        builder.build();
      }
      if (levelIdx == topologyForEachLevel_.size() - 1) {
        builder.forceBufferInSegment(options_->getRootBuffer());
      }
      if (isFirstPoint) {
        treeBufLevels_ += builder.getNumBufferLevels();
        isFirstPoint = false;
      }
      topology.setBranchDrivingSubNet(idx, *builder.getDrivingSubNet());
    });
  }

  LevelTopology& leafTopology = topologyForEachLevel_.back();
  unsigned numSinks = 0;
  leafTopology.forEachBranchingPoint(
      [&](unsigned idx, Point<double> branchPoint) {
        Clock::SubNet* subNet = leafTopology.getBranchDrivingSubNet(idx);
        subNet->setLeafLevel(true);

        const std::vector<Point<double>>& sinkLocs
            = leafTopology.getBranchSinksLocations(idx);
        for (const Point<double>& loc : sinkLocs) {
          if (mapLocationToSink_.find(loc) == mapLocationToSink_.end()) {
            logger_->error(CTS, 80, "Sink not found.");
          }

          subNet->addInst(*mapLocationToSink_[loc]);
          ++numSinks;
        }
      });

  logger_->info(CTS, 35, " Number of sinks covered: {}.", numSinks);
}

void HTreeBuilder::createSingleBufferClockNet()
{
  logger_->report(" Building single-buffer clock net.");

  Point<double> legalCenter = legalizeOneBuffer(sinkRegion_.computeCenter(),
                                                options_->getRootBuffer());
  const int centerX = legalCenter.getX() * wireSegmentUnit_;
  const int centerY = legalCenter.getY() * wireSegmentUnit_;
  ClockInst& rootBuffer = clock_.addClockBuffer(
      "clkbuf_0", options_->getRootBuffer(), centerX, centerY);
  addTreeLevelBuffer(&rootBuffer);
  Clock::SubNet& clockSubNet = clock_.addSubNet("clknet_0");
  clockSubNet.addInst(rootBuffer);

  clock_.forEachSink([&](ClockInst& inst) { clockSubNet.addInst(inst); });
}

void HTreeBuilder::plotSolution()
{
  static int cnt = 0;
  auto name = std::string("plot") + std::to_string(cnt++) + ".py";
  std::ofstream file(name);
  file << "import numpy as np\n";
  file << "import matplotlib.pyplot as plt\n";
  file << "import matplotlib.path as mpath\n";
  file << "import matplotlib.lines as mlines\n";
  file << "import matplotlib.patches as mpatches\n";
  file << "from matplotlib.collections import PatchCollection\n\n";

  clock_.forEachSink([&](const ClockInst& sink) {
    file << "plt.scatter(" << (double) sink.getX() / wireSegmentUnit_ << ", "
         << (double) sink.getY() / wireSegmentUnit_ << ", s=1)\n";
  });

  LevelTopology& topLevelTopology = topologyForEachLevel_.front();
  Point<double> topLevelBufferLoc = sinkRegion_.computeCenter();
  topLevelTopology.forEachBranchingPoint(
      [&](unsigned idx, Point<double> branchPoint) {
        if (topLevelBufferLoc.getX() < branchPoint.getX()) {
          file << "plt.plot([" << topLevelBufferLoc.getX() << ", "
               << branchPoint.getX() << "], [" << topLevelBufferLoc.getY()
               << ", " << branchPoint.getY() << "], c = 'r')\n";
        } else {
          file << "plt.plot([" << branchPoint.getX() << ", "
               << topLevelBufferLoc.getX() << "], [" << branchPoint.getY()
               << ", " << topLevelBufferLoc.getY() << "], c = 'r')\n";
        }
      });

  for (int levelIdx = 1; levelIdx < topologyForEachLevel_.size(); ++levelIdx) {
    const LevelTopology& topology = topologyForEachLevel_[levelIdx];
    topology.forEachBranchingPoint([&](unsigned idx,
                                       Point<double> branchPoint) {
      unsigned parentIdx = topology.getBranchingPointParentIdx(idx);
      Point<double> parentPoint
          = topologyForEachLevel_[levelIdx - 1].getBranchingPoint(parentIdx);
      std::string color = "orange";
      if (levelIdx % 2 == 0) {
        color = "red";
      }

      if (parentPoint.getX() < branchPoint.getX()) {
        file << "plt.plot([" << parentPoint.getX() << ", " << branchPoint.getX()
             << "], [" << parentPoint.getY() << ", " << branchPoint.getY()
             << "], c = '" << color << "')\n";
      } else {
        file << "plt.plot([" << branchPoint.getX() << ", " << parentPoint.getX()
             << "], [" << branchPoint.getY() << ", " << parentPoint.getY()
             << "], c = '" << color << "')\n";
      }
    });
  }

  file << "plt.show()\n";
  file.close();
}

SegmentBuilder::SegmentBuilder(const std::string& instPrefix,
                               const std::string& netPrefix,
                               const Point<double>& root,
                               const Point<double>& target,
                               const std::vector<unsigned>& techCharWires,
                               Clock& clock,
                               Clock::SubNet& drivingSubNet,
                               const TechChar& techChar,
                               const unsigned techCharDistUnit,
                               TreeBuilder* tree)
    : instPrefix_(instPrefix),
      netPrefix_(netPrefix),
      root_(root),
      target_(target),
      techCharWires_(techCharWires),
      techChar_(&techChar),
      techCharDistUnit_(techCharDistUnit),
      clock_(&clock),
      drivingSubNet_(&drivingSubNet),
      tree_(tree)
{
}

void SegmentBuilder::build(const std::string& forceBuffer)
{
  const double lengthX = std::abs(root_.getX() - target_.getX());
  const bool isLowToHiX = root_.getX() < target_.getX();
  const bool isLowToHiY = root_.getY() < target_.getY();

  double connectionLength = 0.0;
  for (unsigned techCharWireIdx : techCharWires_) {
    const WireSegment& wireSegment = techChar_->getWireSegment(techCharWireIdx);
    const unsigned wireSegLen = wireSegment.getLength();
    for (int buffer = 0; buffer < wireSegment.getNumBuffers(); ++buffer) {
      const double location
          = wireSegment.getBufferLocation(buffer) * wireSegLen;
      connectionLength += location;

      double x = std::numeric_limits<double>::max();
      double y = std::numeric_limits<double>::max();
      if (connectionLength < lengthX) {
        y = root_.getY();
        x = (isLowToHiX) ? (root_.getX() + connectionLength)
                         : (root_.getX() - connectionLength);
      } else {
        x = target_.getX();
        y = (isLowToHiY) ? (root_.getY() + (connectionLength - lengthX))
                         : (root_.getY() - (connectionLength - lengthX));
      }

      const std::string buffMaster = !forceBuffer.empty()
                                         ? forceBuffer
                                         : wireSegment.getBufferMaster(buffer);
      ClockInst& newBuffer = clock_->addClockBuffer(
          instPrefix_ + std::to_string(numBufferLevels_),
          buffMaster,
          x * techCharDistUnit_,
          y * techCharDistUnit_);
      tree_->addTreeLevelBuffer(&newBuffer);

      drivingSubNet_->addInst(newBuffer);
      drivingSubNet_
          = &clock_->addSubNet(netPrefix_ + std::to_string(numBufferLevels_));
      drivingSubNet_->addInst(newBuffer);

      ++numBufferLevels_;
    }
    connectionLength += wireSegLen;
  }
}

void SegmentBuilder::forceBufferInSegment(const std::string& master)
{
  if (numBufferLevels_ != 0) {
    return;
  }

  ClockInst& newBuffer
      = clock_->addClockBuffer(instPrefix_ + "_f",
                               master,
                               target_.getX() * techCharDistUnit_,
                               target_.getY() * techCharDistUnit_);
  tree_->addTreeLevelBuffer(&newBuffer);

  drivingSubNet_->addInst(newBuffer);
  drivingSubNet_ = &clock_->addSubNet(netPrefix_ + "_leaf");
  drivingSubNet_->addInst(newBuffer);
  numBufferLevels_++;
}

}  // namespace cts
