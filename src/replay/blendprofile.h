#pragma once
#include "replay/settings.h"

namespace blendprofile {
// Only marker-local blend channels. Global Curve controls already apply to all
// markers; shake, focus, audio and scene settings must not follow this action.
inline void copy(const rsettings::MarkerSettings& source, rsettings::MarkerSettings& target) {
  target.path=source.path;
  target.orient=source.orient;
  target.v[rsettings::P_ALPHA]=source.v[rsettings::P_ALPHA];
  target.v[rsettings::P_EASE_IN]=source.v[rsettings::P_EASE_IN];
  target.v[rsettings::P_EASE_OUT]=source.v[rsettings::P_EASE_OUT];
}
}
