///////////////////////////////////////////////////////////////////////////////
// BSD 3-Clause License
//
// Copyright (c) 2021, The Regents of the University of California
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
// ARE
// DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
// FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
// DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
// SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
// CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
// OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
///////////////////////////////////////////////////////////////////////////////

#include <float.h>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <random>
#include <thread>
#include <unordered_map>
#include <vector>
#include <queue>
#include <stdexcept>
#include <cassert>

#include "block_placement.h"
#include "shape_engine.h"
#include "util.h"
#include "utl/Logger.h"

namespace block_placement {
using std::abs;
using std::cout;
using std::endl;
using std::exp;
using std::fstream;
using std::getline;
using std::ios;
using std::log;
using std::max;
using std::min;
using std::ofstream;
using std::pair;
using std::pow;
using std::sort;
using std::sqrt;
using std::stof;
using std::stoi;
using std::string;
using std::swap;
using std::thread;
using std::tanh;
using std::to_string;
using std::unordered_map;
using std::vector;
using std::queue;
using utl::Logger;
using utl::MPL;

Block::Block(const std::string& name,
             float area,
             int num_macro,
             const std::vector<std::pair<float, float>>& aspect_ratio)
{
  name_ = name;
  area_ = area;
  num_macro_ = num_macro;
  is_soft_ = (num_macro_ == 0);

  aspect_ratio_ = aspect_ratio;
  
  if(num_macro_ >= 1) {
    for (unsigned int i = 0; i < aspect_ratio_.size(); i++) {
      // height_limit_ is sorted in non-decreasing order
      height_limit_.push_back({aspect_ratio_[i].second, aspect_ratio_[i].second});
 
      // width_limit_ is sorted in non-increasing order
      width_limit_.push_back({aspect_ratio_[i].first, aspect_ratio_[i].first});
    }
  } else {
    // sort the aspect ratio according to the 1st element of the pair in
    // ascending order And we assume the aspect_ratio[i].first <=
    // aspect_ratio[i].second
    std::sort(aspect_ratio_.begin(), aspect_ratio_.end());
    for (unsigned int i = 0; i < aspect_ratio_.size(); i++) {
      const float height_low = std::sqrt(area_ * aspect_ratio_[i].first);
      const float width_high = area_ / height_low;
      const float height_high = std::sqrt(area_ * aspect_ratio_[i].second);
      const float width_low = area_ / height_high;
 
      // height_limit_ is sorted in non-decreasing order
      height_limit_.push_back({height_low, height_high});
 
      // width_limit_ is sorted in non-increasing order
      width_limit_.push_back({width_high, width_low});
    }
  }
}

void Block::ChangeWidth(float width)
{
  if (is_soft_ == false)
    return;

  if (width >= width_limit_[0].first) {
    width_ = width_limit_[0].first;
    height_ = area_ / width_;
  } else if (width <= width_limit_[width_limit_.size() - 1].second) {
    width_ = width_limit_[width_limit_.size() - 1].second;
    height_ = area_ / width_;
  } else {
    std::vector<std::pair<float, float>>::iterator vec_it
        = width_limit_.begin();
    while (vec_it->second > width)
      vec_it++;

    if (width <= vec_it->first) {
      width_ = width;
      height_ = area_ / width_;
    } else {
      float width_low = vec_it->first;
      vec_it--;
      float width_high = vec_it->second;
      if (width - width_low > width_high - width)
        width_ = width_high;
      else
        width_ = width_low;

      height_ = area_ / width_;
    }
  }
}

void Block::ChangeHeight(float height)
{
  if (is_soft_ == false)
    return;

  if (height <= height_limit_[0].first) {
    height_ = height_limit_[0].first;
    width_ = area_ / height_;
  } else if (height >= height_limit_[height_limit_.size() - 1].second) {
    height_ = height_limit_[height_limit_.size() - 1].second;
    width_ = area_ / height_;
  } else {
    std::vector<std::pair<float, float>>::iterator vec_it
        = height_limit_.begin();
    while (vec_it->second < height)
      vec_it++;

    if (height >= vec_it->first) {
      height_ = height;
      width_ = area_ / height_;
    } else {
      float height_high = vec_it->first;
      vec_it--;
      float height_low = vec_it->second;
      if (height - height_low > height_high - height)
        height_ = height_high;
      else
        height_ = height_low;

      width_ = area_ / height_;
    }
  }
}

void Block::ResizeHardBlock()
{
  if (num_macro_ == 0) 
    return;
  
  const int index1 = (int) (floor((*distribution_)(*generator_) * aspect_ratio_.size()));
  width_ = aspect_ratio_[index1].first;
  height_ = aspect_ratio_[index1].second;
}

void Block::ChooseAspectRatioRandom()
{
  float ar = 0.0;
  const int index1
      = (int) (floor((*distribution_)(*generator_) * aspect_ratio_.size()));

  const float ar_low = aspect_ratio_[index1].first;
  const float ar_high = aspect_ratio_[index1].second;

  if (ar_low == ar_high) {
    ar = ar_low;
  } else {
    const float num = (*distribution_)(*generator_);
    ar = ar_low + (ar_high - ar_low) * num;
  }

  height_ = std::sqrt(area_ * ar);
  width_ = area_ / height_;
}

void Block::SetAspectRatio(float aspect_ratio)
{
  height_ = std::sqrt(area_ * aspect_ratio);
  width_ = area_ / height_;
}

void Block::SetRandom(std::mt19937& generator,
                      std::uniform_real_distribution<float>& distribution)
{
  generator_ = &generator;
  distribution_ = &distribution;
  if(num_macro_ == 0)
    ChooseAspectRatioRandom();
  else
    ResizeHardBlock();
}

bool Block::IsResizeable() const
{
  return num_macro_ == 0 || aspect_ratio_.size() > 1;
}

void Block::RemoveSoftBlock()
{
  if (num_macro_ == 0) {
    width_ = 0.0;
    height_ = 0.0;
  }
}

void Block::ShrinkSoftBlock(float width_factor, float height_factor)
{
  width_ = width_ * width_factor;
  height_ = height_ * height_factor;
  area_ = width_ * height_;
}

SimulatedAnnealingCore::SimulatedAnnealingCore(
    float outline_width,
    float outline_height,
    const std::vector<Block>& blocks,
    const std::vector<Net*>& nets,
    const std::vector<Region*>& regions,
    const std::vector<Location*>& locations,
    const std::unordered_map<std::string, std::pair<float, float>>&
        terminal_position,
    float cooling_rate,
    float alpha,
    float beta,
    float gamma,
    float boundary_weight,
    float macro_blockage_weight,
    float location_weight,
    float notch_weight,
    float resize_prob,
    float pos_swap_prob,
    float neg_swap_prob,
    float double_swap_prob,
    float init_prob,
    float rej_ratio,
    int max_num_step,
    int k,
    float c,
    int perturb_per_step,
    float learning_rate,
    float shrink_factor,
    float shrink_freq,
    unsigned seed)
{
  outline_width_ = outline_width;
  outline_height_ = outline_height;

  cooling_rate_ = cooling_rate;

  learning_rate_ = learning_rate;
  shrink_factor_ = shrink_factor;
  shrink_freq_ = shrink_freq;

  alpha_ = alpha;
  beta_ = beta;
  gamma_ = gamma;
  boundary_weight_ = boundary_weight;
  macro_blockage_weight_ = macro_blockage_weight;
  location_weight_ = location_weight;
  notch_weight_ = notch_weight;


  alpha_base_ = alpha_;
  beta_base_ = beta_;
  gamma_base_ = gamma_;
  boundary_weight_base_ = boundary_weight_;
  macro_blockage_weight_base_ = macro_blockage_weight_;
  location_weight_base_ = location_weight_;
  notch_weight_base_ = notch_weight_;

  resize_prob_ = resize_prob;
  pos_swap_prob_ = resize_prob_ + pos_swap_prob;
  neg_swap_prob_ = pos_swap_prob_ + neg_swap_prob;
  double_swap_prob_ = neg_swap_prob_ + double_swap_prob;

  init_prob_ = init_prob;
  rej_ratio_ = rej_ratio;
  max_num_step_ = max_num_step;
  k_ = k;
  c_ = c;
  perturb_per_step_ = perturb_per_step;

  std::mt19937 randGen(seed);
  generator_ = randGen;

  std::uniform_real_distribution<float> distribution(0.0, 1.0);
  distribution_ = distribution;

  nets_ = nets;
  regions_ = regions;
  locations_ = locations;
  terminal_position_ = terminal_position;

  for (unsigned int i = 0; i < blocks.size(); i++) {
    pos_seq_.push_back(i);
    neg_seq_.push_back(i);

    pre_pos_seq_.push_back(i);
    pre_neg_seq_.push_back(i);
  }

  blocks_ = blocks;
  
  for (unsigned int i = 0; i < blocks_.size(); i++) {
    blocks_[i].SetRandom(generator_, distribution_);
    block_map_.insert(std::pair<std::string, int>(blocks_[i].GetName(), i));
  }


  for(size_t i = 0; i < locations_.size(); i++) {
    for(size_t j = 0; j < blocks_.size(); j++) {
      if(locations_[i]->name_ == blocks_[j].GetName()) {
        location_map_.insert(std::pair<int, int>(i, j));
      }
    }
  }

  pre_blocks_ = blocks_;
}

void SimulatedAnnealingCore::PackFloorplan()
{
  for (int i = 0; i < blocks_.size(); i++) {
    blocks_[i].SetX(0.0);
    blocks_[i].SetY(0.0);
  }

  // calculate X position
  vector<pair<int, int>> match(blocks_.size());
  for (int i = 0; i < pos_seq_.size(); i++) {
    match[pos_seq_[i]].first = i;
    match[neg_seq_[i]].second = i;
  }

  vector<float> length(blocks_.size());
  for (int i = 0; i < blocks_.size(); i++)
    length[i] = 0.0;

  for (int i = 0; i < pos_seq_.size(); i++) {
    int b = pos_seq_[i];
    int p = match[b].second;
    blocks_[b].SetX(length[p]);
    float t = blocks_[b].GetX() + blocks_[b].GetWidth();
    for (int j = p; j < neg_seq_.size(); j++)
      if (t > length[j])
        length[j] = t;
      else
        break;
  }

  width_ = length[blocks_.size() - 1];

  // calulate Y position
  vector<int> pos_seq(pos_seq_.size());
  int num_blocks = pos_seq_.size();
  for (int i = 0; i < num_blocks; i++)
    pos_seq[i] = pos_seq_[num_blocks - 1 - i];

  for (int i = 0; i < num_blocks; i++) {
    match[pos_seq[i]].first = i;
    match[neg_seq_[i]].second = i;
  }

  for (int i = 0; i < num_blocks; i++)
    length[i] = 0.0;

  for (int i = 0; i < num_blocks; i++) {
    int b = pos_seq[i];
    int p = match[b].second;
    blocks_[b].SetY(length[p]);
    float t = blocks_[b].GetY() + blocks_[b].GetHeight();
    for (int j = p; j < num_blocks; j++)
      if (t > length[j])
        length[j] = t;
      else
        break;
  }

  height_ = length[num_blocks - 1];
  area_ = width_ * height_;
}

void SimulatedAnnealingCore::SingleSwap(bool flag)
{
  int index1 = (int) (floor((distribution_) (generator_) *blocks_.size()));
  int index2 = (int) (floor((distribution_) (generator_) *blocks_.size()));
  while (index1 == index2) {
    index2 = (int) (floor((distribution_) (generator_) *blocks_.size()));
  }

  if (flag)
    swap(pos_seq_[index1], pos_seq_[index2]);
  else
    swap(neg_seq_[index1], neg_seq_[index2]);
}

void SimulatedAnnealingCore::DoubleSwap()
{
  unsigned int index1 = (unsigned) (floor((distribution_) (generator_) *blocks_.size()));
  unsigned int index2 = (unsigned) (floor((distribution_) (generator_) *blocks_.size()));
  while (index1 == index2) {
    index2 = (unsigned) (floor((distribution_) (generator_) *blocks_.size()));
  }

  swap(pos_seq_[index1], pos_seq_[index2]);
  unsigned int neg_index1 = 0;
  unsigned int neg_index2 = 0;
  for(int i = 0; i < blocks_.size(); i++) {
    if(pos_seq_[index1] == neg_seq_[i])
      neg_index1 = i;
 
    if(pos_seq_[index2] == neg_seq_[i])
      neg_index2 = i;
  }
 
  swap(neg_seq_[neg_index1], neg_seq_[neg_index2]);
}

void SimulatedAnnealingCore::Resize()
{
  unsigned int index1 = (unsigned)(floor((distribution_)(generator_) * blocks_.size()));
  while (blocks_[index1].IsResizeable() == false) {
    index1 = (unsigned) (floor((distribution_) (generator_) *blocks_.size()));
  }

  block_id_ = index1;
  if (blocks_[index1].GetNumMacro() > 0) {
    blocks_[index1].ResizeHardBlock();
    return;
  }

  float option = (distribution_) (generator_);
  if (option <= 0.2) {
    // Change the aspect ratio of the soft block to a random value in the
    // range of the given soft aspect-ratio constraint
    blocks_[index1].ChooseAspectRatioRandom();
  } else if (option <= 0.4) {
    // Change the width of soft block to Rb = e.x2 - b.x1
    float b_x1 = blocks_[index1].GetX();
    float b_x2 = b_x1 + blocks_[index1].GetWidth();
    float e_x2 = outline_width_;

    if (b_x1 >= e_x2)
      return;

    for (int i = 0; i < blocks_.size(); i++) {
      float cur_x2 = blocks_[i].GetX() + blocks_[i].GetWidth();
      if (cur_x2 > b_x2 && cur_x2 < e_x2)
        e_x2 = cur_x2;
    }

    float width = e_x2 - b_x1;
    blocks_[index1].ChangeWidth(width);
  } else if (option <= 0.6) {
    // change the width of soft block to Lb = d.x2 - b.x1
    float b_x1 = blocks_[index1].GetX();
    float b_x2 = b_x1 + blocks_[index1].GetWidth();
    float d_x2 = b_x1;
    for (int i = 0; i < blocks_.size(); i++) {
      float cur_x2 = blocks_[i].GetX() + blocks_[i].GetWidth();
      if (cur_x2 < b_x2 && cur_x2 > d_x2)
        d_x2 = cur_x2;
    }

    if (d_x2 <= b_x1) {
      return;
    } else {
      float width = d_x2 - b_x1;
      blocks_[index1].ChangeWidth(width);
    }
  } else if (option <= 0.8) {
    // change the height of soft block to Tb = a.y2 - b.y1
    float b_y1 = blocks_[index1].GetY();
    float b_y2 = b_y1 + blocks_[index1].GetHeight();
    float a_y2 = outline_height_;

    if (b_y1 >= a_y2)
      return;

    for (int i = 0; i < blocks_.size(); i++) {
      float cur_y2 = blocks_[i].GetY() + blocks_[i].GetHeight();
      if (cur_y2 > b_y2 && cur_y2 < a_y2)
        a_y2 = cur_y2;
    }

    float height = a_y2 - b_y1;
    blocks_[index1].ChangeHeight(height);
  } else {
    // Change the height of soft block to Bb = c.y2 - b.y1
    float b_y1 = blocks_[index1].GetY();
    float b_y2 = b_y1 + blocks_[index1].GetHeight();
    float c_y2 = b_y1;
    for (int i = 0; i < blocks_.size(); i++) {
      float cur_y2 = blocks_[i].GetY() + blocks_[i].GetHeight();
      if (cur_y2 < b_y2 && cur_y2 > c_y2)
        c_y2 = cur_y2;
    }

    if (c_y2 <= b_y1) {
      return;
    } else {
      float height = c_y2 - b_y1;
      blocks_[index1].ChangeHeight(height);
    }
  }
}

void SimulatedAnnealingCore::Perturb()
{
  if (blocks_.size() == 1)
    return;

  pre_pos_seq_ = pos_seq_;
  pre_neg_seq_ = neg_seq_;
  pre_width_ = width_;
  pre_height_ = height_;
  pre_area_ = area_;
  pre_wirelength_ = wirelength_;
  pre_outline_penalty_ = outline_penalty_;
  pre_boundary_penalty_ = boundary_penalty_;
  pre_macro_blockage_penalty_ = macro_blockage_penalty_;
  pre_location_penalty_ = location_penalty_;
  pre_notch_penalty_ = notch_penalty_;


  float op = (distribution_) (generator_);
  if (op <= resize_prob_) {
    action_id_ = 0;
    pre_blocks_ = blocks_;
    Resize();
  } else if (op <= pos_swap_prob_) {
    action_id_ = 1;
    SingleSwap(true);
  } else if (op <= neg_swap_prob_) {
    action_id_ = 2;
    SingleSwap(false);
  } else {
    action_id_ = 3;
    DoubleSwap();
  }

  PackFloorplan();
}

void SimulatedAnnealingCore::Restore()
{
  // To reduce the running time, I didn't call PackFloorplan again
  // So when we write the final floorplan out, we need to PackFloor again
  // at the end of SA process
  if (action_id_ == 0)
    blocks_[block_id_] = pre_blocks_[block_id_];
  else if (action_id_ == 1)
    pos_seq_ = pre_pos_seq_;
  else if (action_id_ == 2)
    neg_seq_ = pre_neg_seq_;
  else {
    pos_seq_ = pre_pos_seq_;
    neg_seq_ = pre_neg_seq_;
  }

  width_ = pre_width_;
  height_ = pre_height_;
  area_ = pre_area_;
  wirelength_ = pre_wirelength_;
  outline_penalty_ = pre_outline_penalty_;
  boundary_penalty_ = pre_boundary_penalty_;
  macro_blockage_penalty_ = pre_macro_blockage_penalty_;
  location_penalty_ = pre_location_penalty_;
  notch_penalty_ = pre_notch_penalty_;
}

//
// Calculate the penalty for fixed outline constraint
//
void SimulatedAnnealingCore::CalculateOutlinePenalty()
{
  outline_penalty_ = 0.0;

  const float max_width = max(outline_width_, width_);
  const float max_height = max(outline_height_, height_);
  outline_penalty_
      = max(outline_penalty_,
            max_width * max_height - outline_width_ * outline_height_);
}

//
// Calculate the penalty for macro blockage
//
void SimulatedAnnealingCore::CalculateMacroBlockagePenalty()
{
  macro_blockage_penalty_ = 0.0;
  if (regions_.size() == 0)
    return;

  for (Region* region : regions_) 
    for (int i = 0; i < blocks_.size(); i++) 
      if (blocks_[i].GetNumMacro() > 0) {
        const float lx = blocks_[i].GetX();
        const float ly = blocks_[i].GetY();
        const float ux = lx + blocks_[i].GetWidth();
        const float uy = ly + blocks_[i].GetHeight();

        const float region_lx = region->lx_;
        const float region_ly = region->ly_;
        const float region_ux = region->ux_;
        const float region_uy = region->uy_;

        if (ux <= region_lx || lx >= region_ux || uy <= region_ly
            || ly >= region_uy)
          ;
        else {
          const float width = min(ux, region_ux) - max(lx, region_lx);
          const float height = min(uy, region_uy) - max(ly, region_ly);
          macro_blockage_penalty_ += width * height;
        }
      }
}

//
// Calculate the penalty for macro guidance
//
void SimulatedAnnealingCore::CalculateLocationPenalty()
{
  location_penalty_ = 0.0;
  if(location_map_.size() == 0)
    return;
  
  for(auto map_iter = location_map_.begin(); 
    map_iter != location_map_.end(); map_iter++) {
    const float location_x = (locations_[map_iter->first]->lx_
                        + locations_[map_iter->first]->ux_) / 2.0;
    const float location_y = (locations_[map_iter->first]->ly_
                        + locations_[map_iter->first]->uy_) / 2.0;
    const float location_width = locations_[map_iter->first]->ux_ 
                        - locations_[map_iter->first]->lx_;
    const float location_height = locations_[map_iter->first]->uy_ 
                        - locations_[map_iter->first]->ly_;
    const float block_width = blocks_[map_iter->second].GetWidth();
    const float block_height = blocks_[map_iter->second].GetHeight();
    const float block_x = blocks_[map_iter->second].GetX() + block_width / 2.0;
    const float block_y = blocks_[map_iter->second].GetY() + block_height / 2.0;
 
    float x_dist = abs(block_x - location_x);
    float y_dist = abs(block_y - location_y);
 
    const float width = (block_width + location_width) / 2.0;
    const float height = (block_height + location_height) / 2.0;
    x_dist = x_dist - width > 0.0 ?  x_dist-width : 0.0;
    y_dist = y_dist - height > 0.0 ? y_dist - height : 0.0;
    if(x_dist >= 0.0 && y_dist >= 0.0)
      location_penalty_ += min(x_dist, y_dist);
  }
}

// 
// Align macros to boundaires
//
void SimulatedAnnealingCore::AlignMacro()
{
  // horizontal threshold, we use 10% as the threshold value
  float threshold_H = outline_width_ / 10.0; 
  // vertical threshold, we use 10% as the threshold value
  float threshold_V = outline_height_ / 10.0; 
  for (int i = 0; i < blocks_.size(); i++) {
    const int weight = blocks_[i].GetNumMacro();
    if (weight > 0) {
      const float width = blocks_[i].GetWidth();
      const float height = blocks_[i].GetHeight();
      threshold_H = min(threshold_H, width);
      threshold_V = min(threshold_V, height);
    }
  }
  
  // Alignment macros to boundaries
  for (int i = 0; i < blocks_.size(); i++) {
    const int weight = blocks_[i].GetNumMacro();
    if (weight > 0) {
      const float lx = blocks_[i].GetX();
      const float ly = blocks_[i].GetY();
      const float ux = lx + blocks_[i].GetWidth();
      const float uy = ly + blocks_[i].GetHeight();

      if(lx < threshold_H) 
        blocks_[i].SetX(0.0);
      else if(ux < outline_width_ && outline_width_  - ux < threshold_H) 
        blocks_[i].SetX(outline_width_ - blocks_[i].GetWidth());
      
  
      if(ly < threshold_V) 
        blocks_[i].SetY(0.0);
      else if(uy < outline_height_ && outline_height_ - uy < threshold_V) 
        blocks_[i].SetY(outline_height_ - blocks_[i].GetHeight());
    }
  }

  vector<int> macro_id_list;
  queue<int> macro_queue; // seeds for alignment
  
  // Align macros according to X
  // left alignment
  for (int i = 0; i < blocks_.size(); i++) {
    const int weight = blocks_[i].GetNumMacro();
    if (weight > 0) {
      macro_id_list.push_back(i);
      if(blocks_[i].GetX() == 0.0) {
        macro_queue.push(i);
        blocks_[i].SetAlignFlag(true);
      } else if(blocks_[i].GetX() + blocks_[i].GetWidth() >= outline_width_) 
        blocks_[i].SetAlignFlag(true);  
    }
  }
 
  while(!macro_queue.empty()) {
    const int src = macro_queue.front();
    const float lx = blocks_[src].GetX();
    const float ux = blocks_[src].GetWidth() + lx;
    const float ly = blocks_[src].GetY();
    const float uy = blocks_[src].GetHeight() + ly;
    macro_queue.pop();
    for(auto macro_id : macro_id_list) 
      if (blocks_[macro_id].GetAlignFlag() == false) {
        const float lx_b = blocks_[macro_id].GetX();
        const float ly_b = blocks_[macro_id].GetY();
        const float ux_b = lx_b + blocks_[macro_id].GetWidth();
        const float uy_b = ly_b + blocks_[macro_id].GetHeight();
        const bool y_flag = abs(ly - ly_b) <= threshold_V ||
                            abs(uy - uy_b) <= threshold_V ||
                            abs(ly - uy_b) <= threshold_V ||
                            abs(uy - ly_b) <= threshold_V ;
        if(y_flag == false)
          continue;
        bool x_flag = false;
        if (lx_b >= lx && lx_b <= lx + threshold_H) {
          blocks_[macro_id].SetX(lx);  
          x_flag = true;
        } else if(lx_b >= ux && lx_b <= ux + threshold_H) {
          blocks_[macro_id].SetX(ux);
          x_flag = true;
        }

        if(x_flag == true) 
          if(CalculateOverlap() == true) 
            blocks_[macro_id].SetX(lx_b);
          else {
            macro_queue.push(macro_id);
            blocks_[macro_id].SetAlignFlag(true);
          }
      } 
  }
  

  // right alignment
  for(auto macro_id : macro_id_list) {
    blocks_[macro_id].SetAlignFlag(false);
    if(blocks_[macro_id].GetX() + blocks_[macro_id].GetWidth() >= outline_width_) {
      blocks_[macro_id].SetAlignFlag(true);  
      macro_queue.push(macro_id);
    } else if(blocks_[macro_id].GetX() == 0.0) 
      blocks_[macro_id].SetAlignFlag(true);  
  }

  while(!macro_queue.empty()) {
    const int src = macro_queue.front();
    const float lx = blocks_[src].GetX();
    const float ux = blocks_[src].GetWidth() + lx;
    const float ly = blocks_[src].GetY();
    const float uy = blocks_[src].GetHeight() + ly;
    macro_queue.pop();
    for(auto macro_id : macro_id_list) 
      if (blocks_[macro_id].GetAlignFlag() == false) {
        const float lx_b = blocks_[macro_id].GetX();
        const float ly_b = blocks_[macro_id].GetY();
        const float ux_b = lx_b + blocks_[macro_id].GetWidth();
        const float uy_b = ly_b + blocks_[macro_id].GetHeight();
        const bool y_flag = abs(ly - ly_b) <= threshold_V ||
                            abs(uy - uy_b) <= threshold_V ||
                            abs(ly - uy_b) <= threshold_V ||
                            abs(uy - ly_b) <= threshold_V ;
        if(y_flag == false)
          continue;
        bool x_flag = false;
        if(ux_b <= ux && ux_b >= ux - threshold_H) {
          blocks_[macro_id].SetX(ux - (ux_b - lx_b));  
          x_flag = true;
        } else if(ux_b <= lx && ux_b >= lx - threshold_H) {
          blocks_[macro_id].SetX(lx - (ux_b - lx_b));
          x_flag = true;
        }
        if(x_flag == true) 
          if(CalculateOverlap() == true) 
            blocks_[macro_id].SetX(lx_b);
          else {
            macro_queue.push(macro_id);
            blocks_[macro_id].SetAlignFlag(true);
          }
      }
  }
 
  // bottom alignment
  for(auto macro_id : macro_id_list) {
    blocks_[macro_id].SetAlignFlag(false);
    if(blocks_[macro_id].GetY() == 0.0) {
      blocks_[macro_id].SetAlignFlag(true);  
      macro_queue.push(macro_id);
    } else if(blocks_[macro_id].GetY() + blocks_[macro_id].GetHeight() >= outline_height_) 
      blocks_[macro_id].SetAlignFlag(true);  
  }

  while(!macro_queue.empty()) {
    const int src = macro_queue.front();
    const float lx = blocks_[src].GetX();
    const float ux = blocks_[src].GetWidth() + lx;
    const float ly = blocks_[src].GetY();
    const float uy = blocks_[src].GetHeight() + ly;
    macro_queue.pop();
    for(auto macro_id : macro_id_list) 
      if (blocks_[macro_id].GetAlignFlag() == false) {
        const float lx_b = blocks_[macro_id].GetX();
        const float ly_b = blocks_[macro_id].GetY();
        const float ux_b = lx_b + blocks_[macro_id].GetWidth();
        const float uy_b = ly_b + blocks_[macro_id].GetHeight();
        const bool x_flag = abs(lx - lx_b) <= threshold_H ||
                            abs(ux - ux_b) <= threshold_H ||
                            abs(lx - ux_b) <= threshold_H ||
                            abs(ux - lx_b) <= threshold_H ;
        if(x_flag == false)
          continue;
        bool y_flag = false;
        if(ly_b >= ly && ly_b <= ly + threshold_V) {
          blocks_[macro_id].SetY(ly);
          y_flag = true;
        } else if(ly_b >= uy && ly_b <= uy + threshold_V) {
          blocks_[macro_id].SetY(uy);
          y_flag = true;
        }
        if(y_flag == true) 
          if(CalculateOverlap() == true) 
            blocks_[macro_id].SetY(ly_b);
          else {
            macro_queue.push(macro_id);
            blocks_[macro_id].SetAlignFlag(true);
          }
      }
  }
 
  // top alignment
  for(auto macro_id : macro_id_list) {
    blocks_[macro_id].SetAlignFlag(false);
    if(blocks_[macro_id].GetY() + blocks_[macro_id].GetHeight() >= outline_height_) {
      blocks_[macro_id].SetAlignFlag(true);  
      macro_queue.push(macro_id);
    } else if(blocks_[macro_id].GetY() == 0.0) 
      blocks_[macro_id].SetAlignFlag(true);  
  }

  while(!macro_queue.empty()) {
    const int src = macro_queue.front();
    const float lx = blocks_[src].GetX();
    const float ux = blocks_[src].GetWidth() + lx;
    const float ly = blocks_[src].GetY();
    const float uy = blocks_[src].GetHeight() + ly;
    macro_queue.pop();
    for(auto macro_id : macro_id_list) 
      if (blocks_[macro_id].GetAlignFlag() == false) {
        const float lx_b = blocks_[macro_id].GetX();
        const float ly_b = blocks_[macro_id].GetY();
        const float ux_b = lx_b + blocks_[macro_id].GetWidth();
        const float uy_b = ly_b + blocks_[macro_id].GetHeight();
        const bool x_flag = abs(lx - lx_b) <= threshold_H ||
                            abs(ux - ux_b) <= threshold_H ||
                            abs(lx - ux_b) <= threshold_H ||
                            abs(ux - lx_b) <= threshold_H ;
        if(x_flag == false)
          continue;
        bool y_flag = false;
        if(uy_b <= uy && uy_b >= uy - threshold_V) {
          blocks_[macro_id].SetY(uy - (uy_b - ly_b));
          y_flag = true;
        } else if(uy_b <= ly && uy_b >= ly - threshold_V) {
          blocks_[macro_id].SetY(ly - (uy_b - ly_b));
          y_flag = true;
        }
        if(y_flag == true) 
          if(CalculateOverlap() == true) 
            blocks_[macro_id].SetY(ly_b);
          else {
            macro_queue.push(macro_id);
            blocks_[macro_id].SetAlignFlag(true);
          }
      }
  }
}



// 
// Calculate the penalty for dead space (or notch)
//
void SimulatedAnnealingCore::CalculateNotchPenalty()
{ 
  notch_penalty_ = 0.0;
  if(width_ > outline_width_ || height_ > outline_height_) {
      const float area = max(width_, outline_width_) * max(height_, outline_height_);
      notch_penalty_ = sqrt(area / (outline_width_ * outline_height_));
      return;
  }
  AlignMacro();
  vector<float> x_vec;
  vector<float> y_vec;
  for(int i = 0; i < blocks_.size(); i++) 
    if(blocks_[i].GetNumMacro() > 0) {
      const float lx = blocks_[i].GetX();
      const float ly = blocks_[i].GetY();
      const float ux = lx + blocks_[i].GetWidth();
      const float uy = ly + blocks_[i].GetHeight();
      x_vec.push_back(lx);
      x_vec.push_back(ux);
      y_vec.push_back(ly);
      y_vec.push_back(uy);
    }  

  x_vec.push_back(0.0);
  y_vec.push_back(0.0);

  x_vec.push_back(outline_width_);
  y_vec.push_back(outline_height_);

  sort(x_vec.begin(), x_vec.end());
  sort(y_vec.begin(), y_vec.end());

  vector<float> x_grid;
  vector<float> y_grid;
 
  float temp_x = 0.0;
  x_grid.push_back(x_vec[0]);
  temp_x = x_vec[0];
  for(int i = 1; i < x_vec.size(); i++)
    if(x_vec[i] - temp_x > 0.0) {
      temp_x = x_vec[i];
      x_grid.push_back(x_vec[i]);
    }
 
  float temp_y = 0.0;
  y_grid.push_back(y_vec[0]);
  temp_y = y_vec[0];
  for(int i = 1; i < y_vec.size(); i++)
    if(y_vec[i] - temp_y > 0.0) {
      temp_y = y_vec[i];
      y_grid.push_back(y_vec[i]);
    }

  const int num_x = x_grid.size() - 1;
  const int num_y = y_grid.size() - 1;
  vector<vector<bool> > grid(num_x);
  for(int i = 0; i < num_x; i++)
    grid[i].resize(num_y);

  for(int i = 0; i < num_x; i++)
    for(int j = 0; j < num_y; j++)
        grid[i][j] = false;

  for(int i = 0; i < blocks_.size(); i++) 
    if(blocks_[i].GetNumMacro() > 0) {
      const float lx = blocks_[i].GetX();
      const float ly = blocks_[i].GetY();
      const float ux = lx + blocks_[i].GetWidth();
      const float uy = ly + blocks_[i].GetHeight();

      int x_start = 0;
      int x_end = 0;
      int y_start = 0;
      int y_end = 0;
      for(int j = 0; j < num_x; j++ ) {
        if((x_grid[j] <= lx) && (lx < x_grid[j+1]))
           x_start = j;

        if((x_grid[j] < ux) && (ux <= x_grid[j+1]))
           x_end = j;
      }

      for(int j = 0; j < num_y; j++) {
        if((y_grid[j]  <= ly) && (ly < y_grid[j+1]))
          y_start = j;

        if((y_grid[j] < uy) && (uy <= y_grid[j+1]))
          y_end = j;
      }

      for(int k = x_start; k <= x_end; k++)
        for(int l = y_start; l <= y_end; l++)
            grid[k][l] = true;
    }
  // we define the notch threshold
  const float threshold_H = min(50.0, outline_width_ / 10.0);
  const float threshold_V = min(50.0, outline_height_ / 10.0);
  int num_notch = 0;
  
  
  for (int i = 0; i < num_x; i++) {
    for (int j = 0; j < num_y; j++) {
      bool is_notch = false;
      if (grid[i][j] == true)
        continue;
      if (i == 0 && j==0) {
        if (grid[i+1][j] == true || grid[i][j+1] == true)
          is_notch = true;
      } else if (i == num_x - 1 && j == 0) {
        if (grid[i-1][j] == true || grid[i][j+1] == true)
          is_notch = true;
      } else if (i == 0 && j == num_y - 1) {
        if(grid[i+1][j] == true || grid[i][j-1] == true)
          is_notch = true;
      } else if (i == num_x - 1 && j == num_y - 1) {
        if (grid[i-1][j] == true || grid[i][j-1] == true)
          is_notch = true;
      } else if (j == 0) {
        int result = 0 + grid[i-1][j] + grid[i+1][j]  + grid[i][j+1];
        if (result >= 2)
          is_notch = true;
      } else if (j == num_y - 1) {
        int result = 0 + grid[i-1][j] + grid[i+1][j] + grid[i][j-1];
        if (result >= 2)
          is_notch = true;
      } else if (i == 0) {
        int result = 0 + grid[i][j-1] + grid[i][j+1] + grid[i+1][j];
        if (result >= 2)
          is_notch = true;
      } else if (i == num_x - 1) {
        int result = 0 + grid[i][j-1] + grid[i][j+1] + grid[i-1][j];
        if (result >= 2)
          is_notch = true;
      } else {
        int result = grid[i][j+1] + grid[i][j-1] + grid[i-1][j] + grid[i+1][j];
        if (result >= 3 )
          is_notch = true;
      }
 
      if (is_notch == true) {
        const float width = x_grid[i + 1] - x_grid[i];
        const float height = y_grid[j+1] - y_grid[j];
        if (width <= threshold_H || height <= threshold_V) {     
          num_notch += 1;
          notch_penalty_ += sqrt(width * height / (outline_width_ * outline_height_));
        }
      } 
    }
  }
}

//
// Calculate the penalty for pushing hard macros to boundaries
//
void SimulatedAnnealingCore::CalculateBoundaryPenalty()
{
  boundary_penalty_ = 0.0;
  for (int i = 0; i < blocks_.size(); i++) {
    const int weight = blocks_[i].GetNumMacro();
    if (weight > 0) {
      float lx = blocks_[i].GetX();
      float ly = blocks_[i].GetY();
      float ux = lx + blocks_[i].GetWidth();
      float uy = ly + blocks_[i].GetHeight();

      lx = min(lx, abs(outline_width_ - ux));
      ly = min(ly, abs(outline_height_ - uy));
      lx = min(lx, ly);
      boundary_penalty_ += lx * lx * weight * weight;
    }
  }
}

void SimulatedAnnealingCore::CalculateWirelength()
{
  wirelength_ = 0.0;
  for (Net* net : nets_) {
    vector<string> blocks = net->blocks_;
    vector<string> terminals = net->terminals_;
    const int weight = net->weight_;
    float lx = FLT_MAX;
    float ly = FLT_MAX;
    float ux = 0.0;
    float uy = 0.0;

    for (int i = 0; i < blocks.size(); i++) {
      const float temp_lx = blocks_[block_map_[blocks[i]]].GetX();
      const float temp_ux = temp_lx + blocks_[block_map_[blocks[i]]].GetWidth();
      const float temp_ly = blocks_[block_map_[blocks[i]]].GetY();
      const float temp_uy = temp_ly + blocks_[block_map_[blocks[i]]].GetHeight();
      lx = min(lx, temp_lx);
      ly = min(ly, temp_ly);
      ux = max(ux, temp_ux);
      uy = max(uy, temp_uy);
    }

    for (int i = 0; i < terminals.size(); i++) {
      const float x = terminal_position_[terminals[i]].first;
      const float y = terminal_position_[terminals[i]].second;
      lx = min(lx, x);
      ly = min(ly, y);
      ux = max(ux, x);
      uy = max(uy, y);
    }
    wirelength_ += (abs(ux - lx) + abs(uy - ly)) * weight;
  }
}

float SimulatedAnnealingCore::NormCost(float area,
                                       float wirelength,
                                       float outline_penalty,
                                       float boundary_penalty,
                                       float macro_blockage_penalty,
                                       float location_penalty,
                                       float notch_penalty) const
{
  float cost = 0.0;
  cost += alpha_ * area / norm_area_;
  if (norm_wirelength_ > 0.0) 
    cost += beta_ * wirelength / norm_wirelength_;
  if (norm_outline_penalty_ > 0.0) 
    cost += gamma_ * outline_penalty / norm_outline_penalty_;
  if (norm_boundary_penalty_ > 0.0) 
    cost += boundary_weight_ * boundary_penalty / norm_boundary_penalty_;
  if (norm_macro_blockage_penalty_ > 0.0) 
    cost += macro_blockage_weight_ * macro_blockage_penalty
            / norm_macro_blockage_penalty_;
  if (norm_location_penalty_ > 0.0) 
    cost += location_weight_ * location_penalty_ / norm_location_penalty_;
  if (norm_notch_penalty_ > 0.0) 
    cost += notch_weight_ * notch_penalty_ / norm_notch_penalty_;  
  return cost;
}

void SimulatedAnnealingCore::Initialize()
{
  vector<float> area_list;
  vector<float> wirelength_list;
  vector<float> outline_penalty_list;
  vector<float> boundary_penalty_list;
  vector<float> macro_blockage_penalty_list;
  vector<float> location_penalty_list;
  vector<float> notch_penalty_list;
  norm_area_ = 0.0;
  norm_wirelength_ = 0.0;
  norm_outline_penalty_ = 0.0;
  norm_boundary_penalty_ = 0.0;
  norm_macro_blockage_penalty_ = 0.0;
  norm_location_penalty_ = 0.0;
  norm_notch_penalty_ = 0.0;
  for (int i = 0; i < perturb_per_step_ ; i++) {
    Perturb();
    CalculateWirelength();
    CalculateOutlinePenalty();
    CalculateBoundaryPenalty();
    CalculateMacroBlockagePenalty();
    CalculateLocationPenalty();
    CalculateNotchPenalty();
    area_list.push_back(area_);
    wirelength_list.push_back(wirelength_);
    outline_penalty_list.push_back(outline_penalty_);
    boundary_penalty_list.push_back(boundary_penalty_);
    macro_blockage_penalty_list.push_back(macro_blockage_penalty_);
    location_penalty_list.push_back(location_penalty_);
    notch_penalty_list.push_back(notch_penalty_);
    norm_area_ += area_;
    norm_wirelength_ += wirelength_;
    norm_outline_penalty_ += outline_penalty_;
    norm_boundary_penalty_ += boundary_penalty_;
    norm_macro_blockage_penalty_ += macro_blockage_penalty_;
    norm_location_penalty_ += location_penalty_;
    norm_notch_penalty_ += notch_penalty_;
  }

  norm_area_ = norm_area_ / perturb_per_step_;
  norm_wirelength_ = norm_wirelength_ / perturb_per_step_;
  norm_outline_penalty_ = norm_outline_penalty_ / perturb_per_step_;
  norm_boundary_penalty_ = norm_boundary_penalty_ / perturb_per_step_;
  norm_macro_blockage_penalty_
      = norm_macro_blockage_penalty_ / perturb_per_step_;
  norm_location_penalty_ = norm_location_penalty_ / perturb_per_step_;
  norm_notch_penalty_ = norm_notch_penalty_ / perturb_per_step_;

  vector<float> cost_list;
  for (int i = 0; i < area_list.size(); i++)
    cost_list.push_back(NormCost(area_list[i],
                                 wirelength_list[i],
                                 outline_penalty_list[i],
                                 boundary_penalty_list[i],
                                 macro_blockage_penalty_list[i],
                                 location_penalty_list[i],
                                 notch_penalty_list[i]));

  float delta_cost = 0.0;
  for (int i = 1; i < cost_list.size(); i++)
    delta_cost += abs(cost_list[i] - cost_list[i - 1]);

  init_T_ = (-1.0) * (delta_cost / (perturb_per_step_ - 1)) / log(init_prob_);
}

void SimulatedAnnealingCore::Initialize(float init_T,
                                        float norm_area,
                                        float norm_wirelength,
                                        float norm_outline_penalty,
                                        float norm_boundary_penalty,
                                        float norm_macro_blockage_penalty,
                                        float norm_location_penalty,
                                        float norm_notch_penalty)
{
  init_T_ = init_T;
  norm_area_ = norm_area;
  norm_wirelength_ = norm_wirelength;
  norm_outline_penalty_ = norm_outline_penalty;
  norm_boundary_penalty_ = norm_boundary_penalty;
  norm_macro_blockage_penalty_ = norm_macro_blockage_penalty;
  norm_location_penalty_ = norm_location_penalty;
  norm_notch_penalty_ = norm_notch_penalty;
}

void SimulatedAnnealingCore::SetSeq(const std::vector<int>& pos_seq,
                                    const std::vector<int>& neg_seq)
{
  pos_seq_ = pos_seq;
  neg_seq_ = neg_seq;
  pre_pos_seq_ = pos_seq;
  pre_neg_seq_ = neg_seq;
  PackFloorplan();
  CalculateWirelength();
  CalculateOutlinePenalty();
  CalculateBoundaryPenalty();
  CalculateMacroBlockagePenalty();
}

bool SimulatedAnnealingCore::IsFeasible() const
{
  float tolerance = 0.001;
  if (width_ <= outline_width_ * (1 + tolerance)
      && height_ <= outline_height_ * (1 + tolerance))
    return true;
  else
    return false;
}

void SimulatedAnnealingCore::ShrinkBlocks()
{
  for (int i = 0; i < blocks_.size(); i++) 
    if (blocks_[i].GetNumMacro() == 0) 
      blocks_[i].ShrinkSoftBlock(shrink_factor_, shrink_factor_);   
}


bool SimulatedAnnealingCore::CalculateOverlap()
{
  vector<pair<float, float>> macro_block_x_list;
  vector<pair<float, float>> macro_block_y_list;

  for (int i = 0; i < blocks_.size(); i++) 
    if (blocks_[i].GetNumMacro() > 0) {
      const float lx = blocks_[i].GetX();
      const float ux = lx + blocks_[i].GetWidth();
      const float ly = blocks_[i].GetY();
      const float uy = ly + blocks_[i].GetHeight();
      macro_block_x_list.push_back(pair<float, float>(lx, ux));
      macro_block_y_list.push_back(pair<float, float>(ly, uy));
    }
  

  float overlap = 0.0;
  for (int i = 0; i < macro_block_x_list.size(); i++) 
    for (int j = i + 1; j < macro_block_x_list.size(); j++) {
      const float x1 = 
          max(macro_block_x_list[i].first, macro_block_x_list[j].first);
      const float x2 = 
          min(macro_block_x_list[i].second, macro_block_x_list[j].second);
      const float y1 
          = max(macro_block_y_list[i].first, macro_block_y_list[j].first);
      const float y2
          = min(macro_block_y_list[i].second, macro_block_y_list[j].second);
      const float x = 0.0;
      const float y = 0.0;
      overlap += max(x2 - x1, x) * max(y2 - y1, y);
    }
  
    
  return overlap > 0.0;
}

void SimulatedAnnealingCore::FastSA()
{
  float pre_cost = NormCost(area_,
                            wirelength_,
                            outline_penalty_,
                            boundary_penalty_,
                            macro_blockage_penalty_,
                            location_penalty_,
                            notch_penalty_);
  float cost = pre_cost;
  float delta_cost = 0.0;
  float best_cost = cost;
  int step = 1;
  float rej_num = 0.0;
  float T = init_T_;

  const int max_num_restart = 2;
  int num_restart = 0;
  const int max_num_shrink = int(1.0 / shrink_freq_);
  int num_shrink = 0;
  const int modulo_base = int(max_num_step_ * shrink_freq_);

  while (step < max_num_step_) {
    rej_num = 0.0;
    float accept_rate = 0.0;
    float avg_delta_cost = 0.0;
    for (int i = 0; i < perturb_per_step_; i++) {
      Perturb();
      CalculateWirelength();
      CalculateOutlinePenalty();
      CalculateBoundaryPenalty();
      CalculateMacroBlockagePenalty();
      CalculateLocationPenalty();
      CalculateNotchPenalty();
      cost = NormCost(area_,
                      wirelength_,
                      outline_penalty_,
                      boundary_penalty_,
                      macro_blockage_penalty_,
                      location_penalty_,
                      notch_penalty_);

      delta_cost = cost - pre_cost;
      float num = distribution_(generator_);
      float prob = (delta_cost > 0.0) ? exp((-1) * delta_cost / T) : 1;
      avg_delta_cost += abs(delta_cost);
      if (delta_cost < 0 || num < prob) {
        pre_cost = cost;
        accept_rate += 1.0;
        if (cost < best_cost) {
          best_cost = cost;
          if ((num_shrink <= max_num_shrink) && (step % modulo_base == 0)
              && (IsFeasible() == false)) {
            num_shrink += 1;
            ShrinkBlocks();
            PackFloorplan();
            CalculateWirelength();
            CalculateOutlinePenalty();
            CalculateBoundaryPenalty();
            CalculateMacroBlockagePenalty();
            CalculateLocationPenalty();
            CalculateNotchPenalty();
            pre_cost = NormCost(area_,
                                wirelength_,
                                outline_penalty_,
                                boundary_penalty_,
                                macro_blockage_penalty_,
                                location_penalty_,
                                notch_penalty_);
            best_cost = pre_cost;
          }
        }
      } else {
        rej_num += 1.0;
        Restore();
      }
    }

    step++;
    T = T * cooling_rate_;

    if (step == max_num_step_) {
      PackFloorplan();
      CalculateWirelength();
      CalculateOutlinePenalty();
      CalculateBoundaryPenalty();
      CalculateMacroBlockagePenalty();
      CalculateLocationPenalty();
      CalculateNotchPenalty();
      if (IsFeasible() == false) {
        if (num_restart < max_num_restart) {
          step = 1;
          T = init_T_;
          num_restart += 1;
        }
      }
    }
  }

  CalculateWirelength();
  CalculateOutlinePenalty();
  CalculateBoundaryPenalty();
  CalculateMacroBlockagePenalty();
  CalculateLocationPenalty();
  CalculateNotchPenalty();
}

void Run(SimulatedAnnealingCore* sa)
{
  sa->FastSA();
}

void ParseNetFile(vector<Net*>& nets,
                  unordered_map<string, pair<float, float>>& terminal_position,
                  const string& net_file)
{
  fstream f;
  string line;
  vector<string> content;
  f.open(net_file, ios::in);
  while (getline(f, line))
    content.push_back(line);
  f.close();
  unordered_map<string, pair<float, float>>::iterator terminal_iter;
  int i = 0;
  while (i < content.size()) {
    vector<string> words = Split(content[i]);
    if (words.size() > 2 && words[0] == string("source:")) {
      const string source = words[1];
      terminal_iter = terminal_position.find(source);
      bool terminal_flag = true;
      if (terminal_iter == terminal_position.end())
        terminal_flag = false;

      int j = 2;
      while (j < words.size()) {
        vector<string> blocks;
        vector<string> terminals;
        if (terminal_flag == true)
          terminals.push_back(source);
        else
          blocks.push_back(source);

        const string sink = words[j++];
        terminal_iter = terminal_position.find(sink);
        if (terminal_iter == terminal_position.end())
          blocks.push_back(sink);
        else
          terminals.push_back(sink);

        const int weight = stoi(words[j++]);
        Net* net = new Net(weight, blocks, terminals);
        nets.push_back(net);
      }

      i++;
    } else {
      i++;
    }
  }
}

void ParseRegionFile(vector<Region*>& regions, const string& region_file)
{
  fstream f;
  string line;
  f.open(region_file, ios::in);
  // Check wether the file exists
  if (!(f.good()))
    return;

  while (getline(f, line)) {
    vector<string> words = Split(line);
    const float lx = stof(words[1]);
    const float ly = stof(words[2]);
    const float ux = stof(words[3]);
    const float uy = stof(words[4]);
    Region* region = new Region(lx, ly, ux, uy);
    regions.push_back(region);
  }
  f.close();
}

void ParseLocationFile(vector<Location*>& locations, const string& location_file)
{
  fstream f;
  string line;
  f.open(location_file, ios::in);
  // Check wether the file exists
  if (!(f.good()))
    return;
 
  while (getline(f, line)) {
    vector<string> words = Split(line);
    const string name = words[0];
    const float lx = stof(words[1]);
    const float ly = stof(words[2]);
    const float ux = stof(words[3]);
    const float uy = stof(words[4]);
    Location* location = new Location(name, lx, ly, ux, uy);
    locations.push_back(location);
  }
  f.close();
}


vector<Block> Floorplan(const vector<shape_engine::Cluster*>& clusters,
                        Logger* logger,
                        float outline_width,
                        float outline_height,
                        const std::string& net_file,
                        const std::string& region_file,
                        const std::string& location_file,
                        int num_level,
                        int num_worker,
                        float heat_rate,
                        float alpha,
                        float beta,
                        float gamma,
                        float boundary_weight,
                        float macro_blockage_weight,
                        float location_weight,
                        float notch_weight,
                        float resize_prob,
                        float pos_swap_prob,
                        float neg_swap_prob,
                        float double_swap_prob,
                        float init_prob,
                        float rej_ratio,
                        int max_num_step,
                        int k,
                        float c,
                        int perturb_per_step,
                        float learning_rate,
                        float shrink_factor,
                        float shrink_freq,
                        unsigned seed)
{
  logger->info(MPL, 2001, "Block placement starts.");

  vector<Block> blocks;
  for (int i = 0; i < clusters.size(); i++) {
    const string name = clusters[i]->GetName();
    const float area = clusters[i]->GetArea();
    const int num_macro = clusters[i]->GetNumMacro();
    vector<pair<float, float>> aspect_ratio = clusters[i]->GetAspectRatio();
    blocks.push_back(Block(name, area, num_macro, aspect_ratio));
  }

  unordered_map<string, pair<float, float>> terminal_position;
  string word = string("LL");
  terminal_position[word] = pair<float, float>(0.0, outline_height / 6.0);
  word = string("RL");
  terminal_position[word]
      = pair<float, float>(outline_width, outline_height / 6.0);
  word = string("BL");
  terminal_position[word] = pair<float, float>(outline_width / 6.0, 0.0);
  word = string("TL");
  terminal_position[word]
      = pair<float, float>(outline_width / 6.0, outline_height);
  word = string("LU");
  terminal_position[word] = pair<float, float>(0.0, outline_height * 5.0 / 6.0);
  word = string("RU");
  terminal_position[word]
      = pair<float, float>(outline_width, outline_height * 5.0 / 6.0);
  word = string("BU");
  terminal_position[word] = pair<float, float>(outline_width * 5.0 / 6.0, 0.0);
  word = string("TU");
  terminal_position[word]
      = pair<float, float>(outline_width * 5.0 / 6.0, outline_height);
  word = string("LM");
  terminal_position[word] = pair<float, float>(0.0, outline_height / 2.0);
  word = string("RM");
  terminal_position[word]
      = pair<float, float>(outline_width, outline_height / 2.0);
  word = string("BM");
  terminal_position[word] = pair<float, float>(outline_width / 2.0, 0.0);
  word = string("TM");
  terminal_position[word]
      = pair<float, float>(outline_width / 2.0, outline_height);
  
  vector<Net*> nets;
  ParseNetFile(nets, terminal_position, net_file);

  vector<Region*> regions;
  ParseRegionFile(regions, region_file);
  
  vector<Location*> locations;
  ParseLocationFile(locations, location_file);

  const int num_seed = num_level * num_worker + 10;  // 10 is for guardband
  int seed_id = 0;
  vector<unsigned> seed_list(num_seed);
  std::mt19937 rand_generator(seed);
  for (int i = 0; i < num_seed; i++)
    seed_list[i] = (unsigned) rand_generator();

  SimulatedAnnealingCore* sa = new SimulatedAnnealingCore(outline_width,
                                                          outline_height,
                                                          blocks,
                                                          nets,
                                                          regions,
                                                          locations,
                                                          terminal_position,
                                                          0.99,
                                                          alpha,
                                                          beta,
                                                          gamma,
                                                          boundary_weight,
                                                          macro_blockage_weight,
                                                          location_weight,
                                                          notch_weight,
                                                          resize_prob,
                                                          pos_swap_prob,
                                                          neg_swap_prob,
                                                          double_swap_prob,
                                                          init_prob,
                                                          rej_ratio,
                                                          max_num_step,
                                                          k,
                                                          c,
                                                          perturb_per_step,
                                                          learning_rate,
                                                          shrink_factor,
                                                          shrink_freq,
                                                          seed_list[seed_id++]);

  sa->Initialize();
  logger->info(MPL, 2002, "Block placement finish initialization.");

  SimulatedAnnealingCore* best_sa = nullptr;
  float best_cost = FLT_MAX;
  const float norm_area = sa->GetNormArea();
  const float norm_wirelength = sa->GetNormWirelength();
  const float norm_outline_penalty = sa->GetNormOutlinePenalty();
  const float norm_boundary_penalty = sa->GetNormBoundaryPenalty();
  const float norm_macro_blockage_penalty = sa->GetNormMacroBlockagePenalty();
  const float norm_location_penalty = sa->GetNormLocationPenalty();
  const float norm_notch_penalty = sa->GetNormNotchPenalty();
  float init_T = sa->GetInitT();

  logger->info(MPL, 2003, "Block placement Init_T: {}.", init_T);

  blocks = sa->GetBlocks();
  vector<int> pos_seq = sa->GetPosSeq();
  vector<int> neg_seq = sa->GetNegSeq();
  float heat_count = 1.0;
  for (int i = 0; i < num_level; i++) {
    init_T = init_T * heat_count;
    heat_count = heat_count * heat_rate;
    vector<SimulatedAnnealingCore*> sa_vec;
    vector<thread> threads;
    for (int j = 0; j < num_worker; j++) {
      float cooling_rate = 0.995;
      if (num_worker >= 2) {
        cooling_rate = 0.995 - j * (0.995 - 0.985) / (num_worker - 1);
      }

      SimulatedAnnealingCore* sa
          = new SimulatedAnnealingCore(outline_width,
                                       outline_height,
                                       blocks,
                                       nets,
                                       regions,
                                       locations,
                                       terminal_position,
                                       cooling_rate,
                                       alpha,
                                       beta,
                                       gamma,
                                       boundary_weight,
                                       macro_blockage_weight,
                                       location_weight,
                                       notch_weight,
                                       resize_prob,
                                       pos_swap_prob,
                                       neg_swap_prob,
                                       double_swap_prob,
                                       init_prob,
                                       rej_ratio,
                                       max_num_step,
                                       k,
                                       c,
                                       perturb_per_step,
                                       learning_rate,
                                       shrink_factor,
                                       shrink_freq,
                                       seed_list[seed_id++]);

      sa->Initialize(init_T,
                     norm_area,
                     norm_wirelength,
                     norm_outline_penalty,
                     norm_boundary_penalty,
                     norm_macro_blockage_penalty,
                     norm_location_penalty,
                     norm_notch_penalty);

      sa->SetSeq(pos_seq, neg_seq);
      sa_vec.push_back(sa);
      threads.push_back(thread(Run, sa));
    }

    for (auto& th : threads)
      th.join();

    for (int j = 0; j < num_worker; j++) {
      if (best_cost > sa_vec[j]->GetCost()) {
        best_cost = sa_vec[j]->GetCost();
        best_sa = sa_vec[j];
      }
    }

    blocks = best_sa->GetBlocks();
    pos_seq = best_sa->GetPosSeq();
    neg_seq = best_sa->GetNegSeq();

    // verify the result
    string output_info = "level:  ";
    output_info += to_string(i) + "   ";
    output_info += "cost:  ";
    output_info += to_string(best_sa->GetCost()) + "   ";
    output_info += "area:   ";
    output_info += to_string(best_sa->GetArea()) + "/";
    output_info += to_string(best_sa->GetArea() / norm_area) + "   ";
    output_info += "wirelength:  ";
    output_info += to_string(best_sa->GetWirelength()) + "/";
    output_info += to_string(best_sa->GetWirelength() / norm_wirelength) + "   ";
    output_info += "outline_penalty:  ";
    output_info += to_string(best_sa->GetOutlinePenalty()) + "/";
    output_info += to_string(best_sa->GetOutlinePenalty() / norm_outline_penalty) + "   ";
    output_info += "boundary_penalty:  ";
    output_info += to_string(best_sa->GetBoundaryPenalty()) + "/";
    output_info += to_string(best_sa->GetBoundaryPenalty() / norm_boundary_penalty) + "   ";
    output_info += "macro_blockage_penalty:  ";
    output_info += to_string(best_sa->GetMacroBlockagePenalty()) + "/";
    output_info += to_string(best_sa->GetMacroBlockagePenalty() / norm_macro_blockage_penalty) + "  ";
    output_info += "location_penalty:   ";
    output_info += to_string(best_sa->GetLocationPenalty()) + "/";
    output_info += to_string(best_sa->GetLocationPenalty() / norm_location_penalty) + "  ";
    output_info += "notch_penalty:   ";
    output_info += to_string(best_sa->GetNotchPenalty()) + "/";
    output_info += to_string(best_sa->GetNotchPenalty() / norm_notch_penalty) + "  ";


    logger->info(MPL, 2004 + i, "Block placement {}.", output_info);

    for (int j = 0; j < num_worker; j++) {
      if (best_cost < sa_vec[j]->GetCost()) {
        delete sa_vec[j];
      }
    }
  }

  best_sa->AlignMacro();
  blocks = best_sa->GetBlocks();
  logger->info(MPL,
               2004 + num_level,
               "Block placement floorplan width: {}.",
               best_sa->GetWidth());
  logger->info(MPL,
               2005 + num_level,
               "Block placement floorplan height: {}.",
               best_sa->GetHeight());
  logger->info(MPL,
               2006 + num_level,
               "Block placement outline width: {}.",
               outline_width);
  logger->info(MPL,
               2007 + num_level,
               "Block placement outline height: {}.",
               outline_height);

  if (!(best_sa->IsFeasible()))
    logger->info(
        MPL, 2008 + num_level, "Block placement no feasible floorplan.");

  // free the memory of best_sa
  delete best_sa;
  
  return blocks;
}

}  // namespace block_placement
