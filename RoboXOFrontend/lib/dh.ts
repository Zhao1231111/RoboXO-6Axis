import { Matrix4 } from "three";

const DEG2RAD = Math.PI / 180;

interface DHRow {
  a: number;
  alpha: number;
  d: number;
  thetaOffset: number;
}

const L = 160;

const DH_PARAMS: DHRow[] = [
  { a: 0.0408,  alpha: Math.PI / 2,  d: 390,     thetaOffset: 0 },
  { a: 450.342, alpha: 0,            d: 0.4997,   thetaOffset: Math.PI / 2 },
  { a: 99.107,  alpha: Math.PI / 2,  d: 0,        thetaOffset: 0 },
  { a: 0,       alpha: Math.PI / 2,  d: 470.557,  thetaOffset: 0 },
  { a: 0,       alpha: -Math.PI / 2, d: 0,        thetaOffset: Math.PI / 2 },
  { a: 0,       alpha: 0,            d: 123 + L,  thetaOffset: 0 },
];

/**
 * Compute the DH homogeneous transform for one joint.
 * T = Rz(theta) * Tz(d) * Tx(a) * Rx(alpha)
 */
function dhMatrix(a: number, alpha: number, d: number, theta: number): Matrix4 {
  const ct = Math.cos(theta);
  const st = Math.sin(theta);
  const ca = Math.cos(alpha);
  const sa = Math.sin(alpha);

  // Standard DH matrix (row-major), then set via Matrix4.set which takes row-major args
  return new Matrix4().set(
    ct, -st * ca,  st * sa, a * ct,
    st,  ct * ca, -ct * sa, a * st,
    0,   sa,       ca,      d,
    0,   0,        0,       1,
  );
}

/**
 * Compute forward kinematics frames for the 6-axis robot.
 * @param jointsDeg Joint angles in degrees [J1..J6]
 * @returns Array of 7 Matrix4: frame 0 (base) through frame 6 (TCP)
 */
export function computeFrames(jointsDeg: number[]): Matrix4[] {
  const frames: Matrix4[] = [new Matrix4()]; // frame 0 = identity (base)

  let cumulative = new Matrix4();

  for (let i = 0; i < 6; i++) {
    const row = DH_PARAMS[i];
    const theta = (jointsDeg[i] ?? 0) * DEG2RAD + row.thetaOffset;
    const T = dhMatrix(row.a, row.alpha, row.d, theta);
    cumulative = cumulative.clone().multiply(T);
    frames.push(cumulative.clone());
  }

  return frames;
}
