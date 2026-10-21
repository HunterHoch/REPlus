# Phone gyro proof of concept

Run `npm install`, then `npm start` in this folder. Requires Node.js and the `cf` CLI.

Open http://localhost:3210 on the PC. Scan the QR code with your phone, open it in Safari or Chrome, and tap **Enable gyro**. Allow motion access. Hold the phone comfortably, ideally landscape, then press **Align heading** and **Record pass**. The camera follows an 18-second arc around a stationary box. Rotate the phone to keep the subject in the crosshair. The path never changes with aiming. **Stop & save** pauses and saves; **Resume recording** continues that pass. **Restart** returns to the beginning; the next Record pass creates a new take. **Align heading** points left/right toward the subject but preserves gravity-referenced tilt and roll. Align again after changing screen orientation. Keep the phone page visible. Stop with Ctrl+C.

**Watch take** replays the recorded camera orientation along the same path, ignoring live phone input for rendering. **Download take** exports JSON. Takes also save automatically on the PC under `recordings/<take-id>.json` when stopped, finished, or disconnected. Existing files survive new passes and server restarts; the UI holds only the current session's latest take. Files contain seconds on the path timeline, camera positions, world quaternions in [x,y,z,w] order, FOV, and coordinate-system metadata (Three.js Y-up, not GTA coordinates). Sample rate depends on the phone and connection; gaps are interpolated on replay. Download before closing if a disk-save error appears. There is no physical position tracking: positions come from the authored path.

The horizon comes from the phone's fused device orientation (gravity reference), not its starting pose. Only heading is aligned to the scene; compass north is not used. Align toward the horizon rather than pointing straight up/down, where horizontal heading is undefined. Sensor tilt/roll and landscape behavior still need physical-phone confirmation.

The launcher uses `cf tunnels quick-start` for temporary HTTPS (required for phone sensors), forwarding only this app on loopback port 3210. This is an Internet relay, not a LAN latency benchmark. Pairing uses random session and role tokens; sessions are memory-only and expire after an hour without traffic. Restarting changes the tunnel URL; scan the new QR. The box uses relative orientation, not compass north. Sensor access and physical axis behavior still require testing on a real phone.

Both browsers render the same Three.js scene at 16:9. Phone orientation is applied locally immediately and sent over WebSocket to the desktop. Play/pause state uses a shared server clock with round-trip clock synchronization. This tests aiming with a phone preview; it does **not** test game video streaming or its latency. Phone backgrounding or disconnect pauses playback. No game integration, saved recordings, autofocus, or smoothing. The small map shows the fixed path, subject, and camera position. Actual phone WebGL performance and gyro mapping need real-device testing.

Sensor API reference: https://developer.mozilla.org/en-US/docs/Web/API/DeviceOrientationEvent/requestPermission_static


FiveM mode: Stream, choose Horizontal/Vertical, then Record pass. The saved gyro track loads directly during REPlus export, before shake and DOF, at each shutter sample's replay time. Marker conversion is optional. Vertical renders sideways at full game resolution and rotates upright in the final encode (2560x1440 becomes 1440x2560). See ../GYRO_PASS.md for details and validation status.

Browser exercise **Watch take** now uses the same short-hold repair and time-aware cubic quaternion interpolation as the native gyro export. Refresh the PC and phone pages, choose Browser exercise, Record pass, then Stop & save and Watch take. Live aiming stays immediate; the recorded playback is where the interpolation is evaluated. Raw saved JSON samples are unchanged. The bridge does not need restarting for this update.
