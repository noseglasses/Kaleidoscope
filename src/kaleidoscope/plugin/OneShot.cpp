/* -*- mode: c++ -*-
 * Kaleidoscope-OneShot -- One-shot modifiers and layers
 * Copyright (C) 2016-2018  Keyboard.io, Inc.
 *
 * This program is free software: you can redistribute it and/or modify it under
 * the terms of the GNU General Public License as published by the Free Software
 * Foundation, version 3.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE. See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include <Kaleidoscope-OneShot.h>

namespace kaleidoscope {
namespace plugin {

// ---- state ---------

uint32_t OneShot::start_time_ = 0;
uint16_t OneShot::time_out = 2500;
uint16_t OneShot::hold_time_out = 250;
int16_t OneShot::double_tap_time_out = -1;
OneShot::state_t OneShot::state_ = {0, 0};
OneShot::state_t OneShot::sticky_state_ = {0, 0};
OneShot::state_t OneShot::stickable_state_ = {0xFF, 0xFF};
OneShot::state_t OneShot::pressed_state_ = {0, 0};
bool OneShot::double_tap_sticky = true;
bool OneShot::double_tap_sticky_layers = true;
Key OneShot::prev_key_;
bool OneShot::should_cancel_ = false;
bool OneShot::should_cancel_stickies_ = false;
uint8_t OneShot::positions_[16];
bool OneShot::use_new_stickies_ = false;

// --- helper macros ------

#define isOneShotKey(key) (key.raw >= ranges::OS_FIRST && key.raw <= ranges::OS_LAST)
#define isModifier(key) (key.raw >= Key_LeftControl.raw && key.raw <= Key_RightGui.raw)
#define isLayerKey(key) (key.flags == (KEY_FLAGS | SYNTHETIC | SWITCH_TO_KEYMAP))

#define isOneShot(idx) (bitRead (state_.all, (idx)))
#define setOneShot(idx) (bitWrite (state_.all, idx, 1))
#define clearOneShot(idx) (bitWrite (state_.all, idx, 0))

#define isSticky_(idx) (bitRead (sticky_state_.all, idx))
#define setSticky(idx) (bitWrite (sticky_state_.all, idx, 1))
#define clearSticky(idx) bitWrite (sticky_state_.all, idx, 0)

#define setPressed(idx) bitWrite(pressed_state_.all, idx, 1)
#define clearPressed(idx) bitWrite(pressed_state_.all, idx, 0)
#define isPressed(idx) bitRead (pressed_state_.all, idx)

#define isSameAsPrevious(key) (key.raw == prev_key_.raw)
#define saveAsPrevious(key) prev_key_.raw = key.raw

#define hasTimedOut() (millis () - start_time_ >= time_out)

bool OneShot::isStickable(Key key) {
  if (use_new_stickies_)
    return bitRead(stickable_state_.all, key.raw - ranges::OS_FIRST);

  if (key >= ranges::OSM_FIRST && key <= ranges::OSM_LAST)
    return true;
  else if (key >= ranges::OSL_FIRST && key <= ranges::OSL_LAST)
    return double_tap_sticky_layers;

  return false;
}

void OneShot::positionToCoords(uint8_t pos, byte *row, byte *col) {
  *col = pos % COLS;
  *row = (pos - *col) / COLS;
}

// ---- OneShot stuff ----
void OneShot::injectNormalKey(uint8_t idx, uint8_t key_state) {
  Key key;
  byte row, col;

  if (idx < 8) {
    key.flags = Key_LeftControl.flags;
    key.keyCode = Key_LeftControl.keyCode + idx;
  } else {
    key.flags = KEY_FLAGS | SYNTHETIC | SWITCH_TO_KEYMAP;
    key.keyCode = LAYER_SHIFT_OFFSET + idx - 8;
  }

  positionToCoords(positions_[idx], &row, &col);
  handleKeyswitchEvent(key, UNKNOWN_KEYSWITCH_LOCATION, key_state | INJECTED);
}

void OneShot::activateOneShot(uint8_t idx) {
  injectNormalKey(idx, IS_PRESSED);
}

void OneShot::cancelOneShot(uint8_t idx) {
  clearOneShot(idx);
  injectNormalKey(idx, WAS_PRESSED);
}

EventHandlerResult OneShot::onKeyswitchEvent(Key &mapped_key, byte row, byte col, uint8_t keyState) {
  uint8_t idx = mapped_key.raw - ranges::OS_FIRST;

  if (keyState & INJECTED)
    return EventHandlerResult::OK;

  if (!state_.all) {
    if (!isOneShotKey(mapped_key)) {
      return EventHandlerResult::OK;
    }

    if (keyToggledOff(keyState)) {
      clearPressed(idx);
    } else if (keyToggledOn(keyState)) {
      start_time_ = millis();
      positions_[idx] = row * COLS + col;
      setPressed(idx);
      setOneShot(idx);
      saveAsPrevious(mapped_key);

      activateOneShot(idx);
    }

    return EventHandlerResult::EVENT_CONSUMED;
  }

  if (isOneShotKey(mapped_key)) {
    if (isSticky_(idx)) {
      if (keyToggledOn(keyState)) {  // maybe on _off instead?
        saveAsPrevious(mapped_key);
        clearSticky(idx);
        cancelOneShot(idx);
        should_cancel_ = false;
      }
    } else {
      if (keyToggledOff(keyState)) {
        clearPressed(idx);
        if ((millis() - start_time_) >= hold_time_out) {
          cancelOneShot(idx);
          should_cancel_ = false;
        }
      }

      if (keyToggledOn(keyState)) {
        setPressed(idx);

        if (isSameAsPrevious(mapped_key) && isStickable(mapped_key)) {
          if ((millis() - start_time_) <= ((double_tap_time_out == -1) ? time_out : double_tap_time_out)) {
            setSticky(idx);

            saveAsPrevious(mapped_key);
          }
        } else {
          start_time_ = millis();

          positions_[idx] = row * COLS + col;
          setOneShot(idx);
          saveAsPrevious(mapped_key);

          activateOneShot(idx);
        }
      }
    }

    return EventHandlerResult::EVENT_CONSUMED;
  }

  // ordinary key here, with some event

  if (keyIsPressed(keyState)) {
    saveAsPrevious(mapped_key);
    if (!isModifier(mapped_key) && !isLayerKey(mapped_key)) {
      should_cancel_ = true;
    }
  }

  return EventHandlerResult::OK;
}

EventHandlerResult OneShot::beforeReportingState() {
  if (!state_.all)
    return EventHandlerResult::OK;

  for (uint8_t i = 0; i < 8; i++) {
    if (isOneShot(i)) {
      activateOneShot(i);
    }
  }

  return EventHandlerResult::OK;
}

EventHandlerResult OneShot::afterEachCycle() {
  if (!state_.all)
    return EventHandlerResult::OK;

  if (hasTimedOut())
    cancel();

  bool is_cancelled = false;

  for (uint8_t i = 0; i < 32; i++) {
    if (should_cancel_) {
      if (isSticky_(i)) {
        if (should_cancel_stickies_) {
          is_cancelled = true;
          clearSticky(i);
          cancelOneShot(i);
          clearPressed(i);
        }
      } else if (isOneShot(i) && !isPressed(i)) {
        is_cancelled = true;
        cancelOneShot(i);
      }
    }
  }

  if (is_cancelled) {
    should_cancel_ = false;
    should_cancel_stickies_ = false;
  }

  return EventHandlerResult::OK;
}

void OneShot::inject(Key mapped_key, uint8_t key_state) {
  onKeyswitchEvent(mapped_key, UNKNOWN_KEYSWITCH_LOCATION, key_state);
}

// --- glue code ---

bool OneShot::isActive(void) {
  return (state_.all && !hasTimedOut()) || (pressed_state_.all) || (sticky_state_.all);
}

bool OneShot::isActive(Key key) {
  uint8_t idx = key.raw - ranges::OS_FIRST;

  return (bitRead(state_.all, idx) && !hasTimedOut()) || (isPressed(idx)) || (isSticky_(idx));
}

bool OneShot::isSticky(Key key) {
  uint8_t idx = key.raw - ranges::OS_FIRST;

  return (isSticky_(idx));
}

bool OneShot::isModifierActive(Key key) {
  if (key.raw < Key_LeftControl.raw || key.raw > Key_RightGui.raw)
    return false;

  return isOneShot(key.keyCode - Key_LeftControl.keyCode);
}

void OneShot::cancel(bool with_stickies) {
  should_cancel_ = true;
  should_cancel_stickies_ = with_stickies;
}

void OneShot::enableStickability(Key key) {
  use_new_stickies_ = true;

  if (key >= ranges::OS_FIRST && key <= ranges::OS_LAST)
    bitSet(stickable_state_.all, (key.raw - ranges::OS_FIRST));
}

void OneShot::disableStickability(Key key) {
  use_new_stickies_ = true;

  if (key >= ranges::OS_FIRST && key <= ranges::OS_LAST)
    bitClear(stickable_state_.all, (key.raw - ranges::OS_FIRST));
}

void OneShot::enableStickabilityForModifiers() {
  use_new_stickies_ = true;
  stickable_state_.mods = 0xFF;
}

void OneShot::enableStickabilityForLayers() {
  use_new_stickies_ = true;
  stickable_state_.layers = 0xFF;
}

void OneShot::disableStickabilityForModifiers() {
  use_new_stickies_ = true;
  stickable_state_.mods = 0;
}

void OneShot::disableStickabilityForLayers() {
  use_new_stickies_ = true;
  stickable_state_.layers = 0;
}

}
}

kaleidoscope::plugin::OneShot OneShot;
