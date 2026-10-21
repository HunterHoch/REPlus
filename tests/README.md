# Native gyro checks

Use an x64 MSVC developer prompt, C++20, `/EHsc /MT /I src`, and put compiler/object/executable outputs in the ignored `work/` directory. The math, track, motion and portrait tests are standalone. The bake test also compiles `src/replay/gyro_bake.cpp`. The export test includes production `gyro.cpp` with stubbed game/capture boundaries and links `ws2_32.lib`.

The track test optionally accepts the preserved 599-sample Project 15 portrait CSV. The export test requires that CSV path as its first argument. Actual recordings remain local and are not included here. These harnesses check interpolation, fresh-packet recording, marker conversion/undo, portrait rotation, render-time camera application and the DOF basis; they do not replace validation inside FiveM.

For the browser/bridge suite, run `npm ci` then `npm test` in `gyro_poc`.