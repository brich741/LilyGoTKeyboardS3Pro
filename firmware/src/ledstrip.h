#pragma once
#include <stdint.h>

#include "config.h"
#include "visual.h"

// Background task that renders the page effect + per-key colors onto the 14 LEDs.
namespace ledstrip {

void begin();

// Replace what the strip should show. Cheap; call whenever something changes.
void set(const core::LedCfg& page, const uint16_t zones[core::kKeyCount],
         const core::KeyLook looks[core::kKeyCount], uint8_t brightnessPct);

void setDimmed(bool dimmed);

// Light only these LEDs (white) for calibration; 0 restores normal output.
void probe(uint16_t mask);

// Stop the background task from touching the LEDs (bring-up experiments).
void setPaused(bool paused);

}  // namespace ledstrip
