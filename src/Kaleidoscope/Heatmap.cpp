/* -*- mode: c++ -*-
 * Kaleidoscope-Heatmap -- Heatmap LED effect for Kaleidoscope.
 * Copyright (C) 2016, 2017  Gergely Nagy
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <Kaleidoscope.h>
#include <Kaleidoscope-Heatmap.h>

namespace kaleidoscope {

// store the number of times each key has been strock
uint16_t Heatmap::heatmap_[ROWS][COLS];
// max of heatmap_ (we divide by it so we start at 1)
uint16_t Heatmap::highest_ = 1;
// next heatmap computation time
uint32_t Heatmap::next_heatmap_comp_time_ = 0;
// in the cRGB struct the order is blue, green, red (It should be called cBGR…)
// default heat_colors                                black        green         yellow           red
static const cRGB heat_colors_default_[] PROGMEM = {{0, 0, 0}, {25, 255, 25}, {25, 255, 255}, {25, 25, 255}};

// colors from cold to hot
const cRGB *Heatmap::heat_colors = heat_colors_default_;
uint8_t Heatmap::heat_colors_length = 4;
// number of millisecond to wait between each heatmap computation
uint16_t Heatmap::update_delay = 1000;

cRGB Heatmap::computeColor(float v) {
  // compute the color corresponding to a value between 0 and 1

  /*
   * for exemple, if:
   *   v=0.8
   *   heat_colors_lenth=4 (hcl)
   *   the red components of heat_colors are: 0, 25, 25, 255 (rhc)
   * the red component returned by computeColor will be: 117
   *
   * 255 |                 /
   *     |                /
   *     |               /
   * 117 | - - - - - - -/
   *     |             /
   *  25 |      ______/ |
   *     |   __/
   *     | _/           |
   *     |/_________________
   *     0     1     2  ↑  3
   *                 ↑ 2.4 ↑
   *               idx1 |  idx2
   *                 <—–>
   *                  fb
   *
   * in this exemple, I call red heat_colors: rhc
   * idx1 = floor(v×(hcl-1)) = floor(0.8×3) = floor(2.4) = 2
   * idx2 = idx1 + 1 = 3
   * fb = v×(hcl-1)-idx1 = 0.8×3 - 2 = 0.4
   * red = (rhc[idx2]-rhc[idx1])×fb + rhc[idx1] = (255-25)×(2.4-2) + 25 = 117
   */

  float fb = 0;
  uint8_t idx1, idx2;

  if (v <= 0) {
    // if v = 0, don't bother computing fb and use heat_colors[0]
    idx1 = idx2 = 0;
  } else if (v >= 1) {
    // if v = 1,
    // don't bother computing fb and use heat_colors[heat_colors_length-1]
    idx1 = idx2 = heat_colors_length - 1;
  } else {
    float val = v * (heat_colors_length - 1);
    // static_cast from float to int just drop the decimal part of the number
    // static_cast<int>(5.9) → 5
    idx1 = static_cast<int>(val);
    idx2 = idx1 + 1;
    fb = val - static_cast<float>(idx1);
  }

  uint8_t r = static_cast<uint8_t>((pgm_read_byte(&(heat_colors[idx2].r)) - pgm_read_byte(&(heat_colors[idx1].r))) * fb + pgm_read_byte(&(heat_colors[idx1].r)));
  uint8_t g = static_cast<uint8_t>((pgm_read_byte(&(heat_colors[idx2].g)) - pgm_read_byte(&(heat_colors[idx1].g))) * fb + pgm_read_byte(&(heat_colors[idx1].g)));
  uint8_t b = static_cast<uint8_t>((pgm_read_byte(&(heat_colors[idx2].b)) - pgm_read_byte(&(heat_colors[idx1].b))) * fb + pgm_read_byte(&(heat_colors[idx1].b)));

  return {b, g, r};
}

void Heatmap::shiftStats(void) {
  // this method is called when:
  // 1. a value in heatmap_ reach INT8_MAX
  // 2. highest_ reach heat_colors_length*512 (see Heatmap::loopHook)

  // we divide every heatmap element by 2
  for (uint8_t r = 0; r < ROWS; r++) {
    for (uint8_t c = 0; c < COLS; c++) {
      heatmap_[r][c] = heatmap_[r][c] >> 1;
    }
  }

  // and also divide highest_ accordingly
  highest_ = highest_ >> 1;
}

EventHandlerResult Heatmap::onKeyswitchEvent(Key &mapped_key, byte row, byte col, uint8_t key_state) {
  // this methode is called frequently by Kaleidoscope
  // even if the module isn't activated

  // if it is a synthetic key, skip it
  if (key_state & INJECTED)
    return EventHandlerResult::OK;

  // if the key is not toggled on, skip it
  if (!keyToggledOn(key_state))
    return EventHandlerResult::OK;

  // increment the heatmap_ value related to the key
  heatmap_[row][col]++;

  // check highest_
  if (highest_ < heatmap_[row][col]) {
    highest_ = heatmap_[row][col];

    // if highest_ (and so heatmap_ value related to the key)
    // is close to overflow: call shiftStats
    // NOTE: this is barely impossible since shiftStats should be
    //       called much sooner by Heatmap::loopHook
    if (highest_ == INT16_MAX)
      shiftStats();
  }

  return EventHandlerResult::OK;
}

EventHandlerResult Heatmap::beforeEachCycle() {
  // this methode is called frequently by Kaleidoscope
  // even if the module isn't activated

  // call shiftStats (divide every heatmap value by 2)
  // if highest_ reach heat_colors_length*512.
  // So after the shift, highest_ will be heat_colors_length*256. We
  // didn't lose any precision in our heatmap since between each color we have a
  // maximum precision of 256 ; said differently, there is 256 state (at max)
  // between heat_colors[x] and heat_colors[x+1].
  if (highest_ > (static_cast<uint16_t>(heat_colors_length) << 9))
    shiftStats();

  return EventHandlerResult::OK;
}

void Heatmap::update(void) {
  // this methode is called frequently by the LEDControl::loopHook

  // do nothing if we didn't reach next_heatmap_comp_time_ yet
  if (next_heatmap_comp_time_ && (millis() < next_heatmap_comp_time_))
    return;
  // do the heatmap computing
  // (we reach next_heatmap_comp_time_ or next_heatmap_comp_time_ was never scheduled)

  // schedule the next heatmap computing
  next_heatmap_comp_time_ = millis() + update_delay;

  // for each key
  for (uint8_t r = 0; r < ROWS; r++) {
    for (uint8_t c = 0; c < COLS; c++) {
      // how much the key was pressed compared to the others (between 0 and 1)
      // (total_keys_ can't be equal to 0)
      float v = static_cast<float>(heatmap_[r][c]) / highest_;
      // we could have used an interger instead of a float, but then we would
      // have had to change some multiplication in division.
      // / on uint is slower than * on float, so I stay with the float
      // https://forum.arduino.cc/index.php?topic=92684.msg2733723#msg2733723

      // set the LED color accordingly
      ::LEDControl.setCrgbAt(r, c, computeColor(v));
    }
  }
}

}

kaleidoscope::Heatmap HeatmapEffect;
