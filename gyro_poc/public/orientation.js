export function multiply([ax, ay, az, aw], [bx, by, bz, bw]) {
  return [ax*bw+aw*bx+ay*bz-az*by, ay*bw+aw*by+az*bx-ax*bz, az*bw+aw*bz+ax*by-ay*bx, aw*bw-ax*bx-ay*by-az*bz];
}
export const inverse = ([x,y,z,w]) => [-x,-y,-z,w];
// Earth Z-up -> scene Y-up is a WORLD rotation (left multiplication).
// The phone's screen basis is fixed when heading is aligned.
export function groundCamera(device, screenAngle = 0) {
  return multiply(multiply([-Math.SQRT1_2,0,0,Math.SQRT1_2],device),fromDevice(-screenAngle,0,0));
}
export function heading([x,y,z,w]) {
  const forwardX = -2*(x*z+y*w), forwardZ = -(1-2*(x*x+y*y));
  return Math.hypot(forwardX,forwardZ)<1e-5 ? null : Math.atan2(-forwardX,-forwardZ);
}
export function yaw(angle) { return [0,Math.sin(angle/2),0,Math.cos(angle/2)]; }
export function fromDevice(alpha, beta, gamma) {
  const half = Math.PI / 360;
  const a = alpha*half, b = beta*half, g = gamma*half;
  // DeviceOrientation uses intrinsic Z-X-Y, not yaw/pitch/roll addition.
  // Track the physical phone, whose axes stay fixed when its UI turns landscape.
  // Camera-view corrections rotate these axes and turn phone roll into yaw.
  return multiply(multiply([0,0,Math.sin(a),Math.cos(a)], [Math.sin(b),0,0,Math.cos(b)]), [0,Math.sin(g),0,Math.cos(g)]);
}
export function cssMatrix([x,y,z,w]) {
  // Reflect the Y coordinate into CSS's downwards screen space: S*R*S.
  return `matrix3d(${[1-2*(y*y+z*z), -2*(x*y+z*w), 2*(x*z-y*w), 0, -2*(x*y-z*w), 1-2*(x*x+z*z), -2*(y*z+x*w), 0, 2*(x*z+y*w), -2*(y*z-x*w), 1-2*(x*x+y*y), 0, 0,0,0,1].join(',')})`;
}
