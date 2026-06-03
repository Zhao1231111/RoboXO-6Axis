"use client";

import { useMemo } from "react";
import { Canvas } from "@react-three/fiber";
import { OrbitControls } from "@react-three/drei";
import { Vector3, Quaternion, Matrix4 } from "three";
import { computeFrames } from "@/lib/dh";

const JOINT_RADIUS = 40;
const JOINT_HEIGHT = 60;
const LINK_WIDTH = 36;
const BASE_RADIUS = 80;
const BASE_HEIGHT = 30;
const GIZMO_SIZE = 120;

const COLORS = {
  base: "#555555",
  jointEven: "#3A7BDE",
  jointOdd: "#888888",
  linkEven: "#5A9BEE",
  linkOdd: "#AAAAAA",
  endEffector: "#E87730",
};

function extractPosQuat(m: Matrix4): { pos: Vector3; quat: Quaternion } {
  const pos = new Vector3();
  const quat = new Quaternion();
  const scale = new Vector3();
  m.decompose(pos, quat, scale);
  return { pos, quat };
}

function Link({
  from,
  to,
  color,
}: {
  from: Vector3;
  to: Vector3;
  color: string;
}) {
  const mid = useMemo(
    () => new Vector3().addVectors(from, to).multiplyScalar(0.5),
    [from, to]
  );
  const length = useMemo(() => from.distanceTo(to), [from, to]);

  const quat = useMemo(() => {
    if (length < 0.1) return new Quaternion();
    const dir = new Vector3().subVectors(to, from).normalize();
    const up = new Vector3(0, 1, 0);
    const q = new Quaternion();
    q.setFromUnitVectors(up, dir);
    return q;
  }, [from, to, length]);

  if (length < 0.1) return null;

  return (
    <mesh position={mid} quaternion={quat}>
      <boxGeometry args={[LINK_WIDTH, length, LINK_WIDTH]} />
      <meshStandardMaterial color={color} />
    </mesh>
  );
}

function Joint({
  matrix,
  color,
}: {
  matrix: Matrix4;
  color: string;
}) {
  const { pos, quat } = useMemo(() => extractPosQuat(matrix), [matrix]);

  return (
    <mesh position={pos} quaternion={quat}>
      <cylinderGeometry args={[JOINT_RADIUS, JOINT_RADIUS, JOINT_HEIGHT, 24]} />
      <meshStandardMaterial color={color} />
    </mesh>
  );
}

function Gizmo({ matrix, size }: { matrix: Matrix4; size: number }) {
  const { pos, quat } = useMemo(() => extractPosQuat(matrix), [matrix]);

  return (
    <group position={pos} quaternion={quat}>
      {/* X axis — red */}
      <mesh position={[size / 2, 0, 0]}>
        <boxGeometry args={[size, 4, 4]} />
        <meshStandardMaterial color="#EE3333" />
      </mesh>
      {/* Y axis — green */}
      <mesh position={[0, size / 2, 0]}>
        <boxGeometry args={[4, size, 4]} />
        <meshStandardMaterial color="#33BB33" />
      </mesh>
      {/* Z axis — blue */}
      <mesh position={[0, 0, size / 2]}>
        <boxGeometry args={[4, 4, size]} />
        <meshStandardMaterial color="#3366EE" />
      </mesh>
    </group>
  );
}

function RobotArm({ joints }: { joints: number[] }) {
  const frames = useMemo(() => computeFrames(joints), [joints]);
  const positions = useMemo(
    () => frames.map((m) => extractPosQuat(m).pos),
    [frames]
  );

  return (
    // Rotate Z-up (DH convention) → Y-up (Three.js convention)
    <group rotation={[-Math.PI / 2, 0, 0]}>
      {/* Base pedestal: sits below frame_0 along Z (DH up axis) */}
      <mesh position={[0, 0, -BASE_HEIGHT / 2]} rotation={[Math.PI / 2, 0, 0]}>
        <cylinderGeometry
          args={[BASE_RADIUS, BASE_RADIUS * 1.2, BASE_HEIGHT, 32]}
        />
        <meshStandardMaterial color={COLORS.base} />
      </mesh>

      {/* Base gizmo */}
      <Gizmo matrix={frames[0]} size={GIZMO_SIZE} />

      {/* Joints and links */}
      {frames.slice(1).map((frame, i) => (
        <group key={i}>
          <Joint
            matrix={frame}
            color={i % 2 === 0 ? COLORS.jointEven : COLORS.jointOdd}
          />
          <Link
            from={positions[i]}
            to={positions[i + 1]}
            color={i % 2 === 0 ? COLORS.linkEven : COLORS.linkOdd}
          />
        </group>
      ))}

      {/* End-effector marker */}
      <mesh
        position={positions[6]}
        quaternion={extractPosQuat(frames[6]).quat}
      >
        <cylinderGeometry args={[20, 20, 40, 16]} />
        <meshStandardMaterial color={COLORS.endEffector} />
      </mesh>

      {/* TCP gizmo */}
      <Gizmo matrix={frames[6]} size={GIZMO_SIZE} />
    </group>
  );
}

export default function RobotView3D({
  joints,
  className = "",
}: {
  joints: number[];
  className?: string;
}) {
  return (
    <div className={`rounded-lg overflow-hidden bg-[#E8E6E1] ${className}`}>
      <Canvas
        camera={{ position: [1200, 800, 1200], fov: 45, near: 1, far: 10000 }}
      >
        <ambientLight intensity={0.6} />
        <directionalLight position={[500, 800, 500]} intensity={0.8} />
        <directionalLight position={[-300, 400, -300]} intensity={0.3} />
        <OrbitControls enableDamping dampingFactor={0.1} />
        <gridHelper args={[2000, 20, "#BBBBBB", "#DDDDDD"]} />
        <RobotArm joints={joints} />
      </Canvas>
    </div>
  );
}
