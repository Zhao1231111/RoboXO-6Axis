# 井字棋机器人 - 前端设计文档

本文档供前端开发使用。目标是让开发者能够根据此文档直接编写代码，不需要再回头查阅开题报告。

## 1. 技术栈

| 技术 | 版本 | 用途 |
|------|------|------|
| Next.js | 14 | 框架，使用静态导出（`next export`），文件路由 |
| React | 18 | UI 组件和状态管理（Context + Hooks） |
| Fluent UI React | v9 (`@fluentui/react-components`) | 组件库（Button, Dropdown, Tab, Card 等） |
| Three.js | latest (`three`) + `@react-three/fiber` + `@react-three/drei` | 3D 机械臂渲染 |
| WebSocket | 浏览器原生 API | 实时状态接收 |
| TypeScript | 5.x | 类型安全 |

## 2. 部署方式

前端编译为纯静态文件（HTML/JS/CSS），由后端 FastAPI 的 `StaticFiles` 中间件托管。示教器 Windows 平板上通过浏览器访问 `http://<工控机IP>:8000/`。不需要在平板上安装 Node.js。

## 3. 页面结构

顶部 Tab 导航，三个页面：

```
┌─────────────────────────────────────────────┐
│  [对弈]    [手操]    [设置]                   │  ← Tab 栏
├─────────────────────────────────────────────┤
│                                             │
│            页面内容区                         │
│                                             │
├─────────────────────────────────────────────┤
│        常驻安全状态区域 (1/4~1/5 屏幕高度)     │  ← 所有页面共享
└─────────────────────────────────────────────┘
```

Tab 使用 Fluent UI 的 `<TabList>` + `<Tab>` 组件。安全状态区域在所有页面底部始终显示，放在 Tab 内容区外面（Layout 级别）。

## 4. 对弈页面 (`/game`)

### 布局

```
┌──────────────────────┬──────────────────┐
│                      │  难度: [简单 ▼]   │
│                      │  先手: [人类 ▼]   │
│    3x3 棋盘网格       │  [开始]  [重来]   │
│    (60% 宽度)        │                  │
│                      │  胜:2 负:1 平:0   │
│                      ├──────────────────┤
│                      │  Three.js 小窗口  │
│                      │  (~200x150px)    │
│    当前轮次 / 结果     │  机械臂实时姿态    │
├──────────────────────┴──────────────────┤
│  安全状态区域                              │
│  绿色/黄色/红色背景 + 文字 + 图标           │
└─────────────────────────────────────────┘
```

### 组件树

```
<GamePage>
  <div className="game-layout">  // CSS Grid: 左右分栏
    <BoardSection>
      <TurnIndicator turn="human|robot" />
      <Board cells={3x3 array} />  // 九宫格
      <GameResult result="win|lose|draw|null" />  // 对局结束时叠加显示
    </BoardSection>
    <ControlSection>
      <DifficultySelector value={difficulty} onChange={...} />
      <FirstMoveSelector value={firstMove} onChange={...} />
      <Button onClick={startGame}>开始</Button>
      <Button onClick={resetGame}>重来</Button>
      <ScoreDisplay wins={n} losses={n} draws={n} />
      <RobotMiniView />  // Three.js 小窗口
    </ControlSection>
  </div>
  {/* 安全区域在 Layout 级别渲染，不在这里 */}
</GamePage>
```

### Board 组件

- 3x3 CSS Grid
- 每个格子根据 `cells[row][col]` 的值渲染：
  - `0` 或 `null`: 空白
  - `1` 或 `"O"`: 显示圈（SVG 圆）
  - `2` 或 `"X"`: 显示叉（SVG 两条对角线）
- 格子不可点击（用户在物理白板上画棋，不在屏幕上操作）

### DifficultySelector

Fluent UI `<Dropdown>`，选项: "简单", "中等", "困难"。值映射: `easy`, `medium`, `hard`。

### FirstMoveSelector

Fluent UI `<Dropdown>`，选项: "人类先手", "机器人先手"。值映射: `human`, `robot`。

## 5. 手操页面 (`/jog`)

### 布局

```
┌─────────────────────────────────────────────┐
│                                             │
│           Three.js 3D 视口                   │
│         (全宽, ~45% 屏幕高度)                 │
│       可触摸旋转/缩放, 显示六轴机械臂          │
│                                             │
├──────────────────────┬──────────────────────┤
│  速度倍率: [1%][5%][10%][50%][100%]          │
├──────────────────────┬──────────────────────┤
│   关节坐标            │   直角坐标            │
│   J1: 45.2°  [+][-]  │   X: 320.1mm [+][-] │
│   J2: -30.0° [+][-]  │   Y: 150.3mm [+][-] │
│   J3: 60.5°  [+][-]  │   Z: 400.2mm [+][-] │
│   J4: 0.0°   [+][-]  │   Rx: 0.0°   [+][-] │
│   J5: -45.3° [+][-]  │   Ry: 180.0° [+][-] │
│   J6: 12.1°  [+][-]  │   Rz: 0.0°   [+][-] │
├──────────────────────┴──────────────────────┤
│  IO 控制: [夹爪开][夹爪合][报警] ● 夹爪:合 ● 报警:关 │
├─────────────────────────────────────────────┤
│  安全状态区域                                 │
└─────────────────────────────────────────────┘
```

### 组件树

```
<JogPage>
  <RobotView3D height="45vh" />  // Three.js 大视口
  <SpeedSelector value={speedRatio} onChange={...} />
  <div className="jog-columns">
    <JointJogPanel joints={jointAngles} onJog={handleJointJog} />
    <CartesianJogPanel pose={cartesianPose} onJog={handleCartesianJog} />
  </div>
  <IOControlPanel ioState={ioState} onSetIO={handleSetIO} />
</JogPage>
```

### Jog 按钮交互逻辑

- `onPointerDown`: 发送 `POST /api/robot/jog` 请求，参数 `{ axis, direction: "+"|"-", speed_ratio, mode: "joint"|"cartesian" }`
- `onPointerUp` / `onPointerLeave`: 发送 `POST /api/robot/jog/stop`
- 按钮使用 Fluent UI `<Button>` 且设置 `appearance="primary"` for "+"，`appearance="secondary"` for "-"
- 最小触摸目标: 48x48dp

### SpeedSelector

一行互斥按钮组，用 Fluent UI `<ToggleButton>` 实现。选中项高亮。

### IOControlPanel

- 夹爪开: `POST /api/robot/io` `{ pin: "gripper", state: "open" }`
- 夹爪合: `POST /api/robot/io` `{ pin: "gripper", state: "close" }`
- 报警手动触发: `POST /api/robot/io` `{ pin: "alarm", state: "on"|"off" }`
- 状态指示: 彩色圆点（绿=开/合, 灰=未激活）

## 6. 设置页面 (`/settings`)

### 内容

- **标定操作**
  - 按钮: "白板平面标定" → `POST /api/system/calibrate` `{ type: "whiteboard" }`
  - 按钮: "手眼标定" → `POST /api/system/calibrate` `{ type: "handeye" }`
  - 标定进行中显示进度提示

- **连接状态**
  - WebSocket 连接: 绿色 "已连接" / 红色 "断开"
  - IPC 连接 (从 WebSocket 状态中读取): 绿色 "正常" / 红色 "异常"

- **版本信息**
  - 前端版本号、后端版本号

## 7. 安全状态区域组件 (`<SafetyStatusBar>`)

这是全局组件，渲染在 Layout 层，所有页面底部都可见。

### 规格

- 高度: 屏幕高度的 20%~25%（约 150~200px on 10" tablet）
- 宽度: 100%
- 字体大小: 不小于 24px（安全信息必须在远处可读）
- 图标尺寸: 不小于 64x64px

### 状态与样式

| 安全状态 | 背景色 | 文字 | 图标 |
|---------|--------|------|------|
| `braked` | `#00A651` (ISO 3864 绿) | "已抱闸 - 安全" | ISO 7010 安全符号 |
| `countdown` | `#FFD100` (ISO 3864 黄) | "警告 - X 秒后机器人开始运动" | ISO 7010 W001 一般警告 |
| `moving` | `#ED1C24` (ISO 3864 红) | "机器人运动中，请远离" | ISO 7010 W012 移动部件警告 |
| `disconnected` | `#333333` (深灰) | "连接中断 - 请检查网络" | 断连图标 |

### 实现

```tsx
function SafetyStatusBar({ safetyState, countdown }: Props) {
  const config = SAFETY_STYLES[safetyState]; // 从上表查
  return (
    <div style={{
      backgroundColor: config.bgColor,
      height: '20vh',
      display: 'flex',
      alignItems: 'center',
      justifyContent: 'center',
      gap: '24px',
    }}>
      <img src={config.icon} style={{ width: 64, height: 64 }} />
      <span style={{ fontSize: 28, fontWeight: 'bold', color: config.textColor }}>
        {safetyState === 'countdown'
          ? `警告 - ${countdown} 秒后机器人开始运动`
          : config.text}
      </span>
    </div>
  );
}
```

## 8. Three.js 机械臂可视化 (`<RobotView3D>`)

### DH 参数

使用 MOKA MR07S 的 DH 参数表（具体数值从 C++ 控制程序或文档获取）。DH 参数定义为一个数组:

```ts
interface DHRow {
  a: number;      // 连杆长度 (mm)
  alpha: number;  // 连杆扭角 (rad)
  d: number;      // 关节偏距 (mm)
  theta_offset: number; // 关节角偏移 (rad)
}

const DH_PARAMS: DHRow[] = [
  // { a, alpha, d, theta_offset } for J1..J6
  // 待填入实际数值
];
```

### 正运动学计算

在浏览器端用 JS 计算。每个关节的齐次变换矩阵:

```
T_i = Rz(theta_i + offset_i) * Tz(d_i) * Tx(a_i) * Rx(alpha_i)
```

从基座到每个关节的累积变换: `T_0^i = T_1 * T_2 * ... * T_i`

将 `T_0^i` 的旋转和平移应用到对应 Three.js Object3D 的 `matrix` 属性上。

### 连杆可视化

每个连杆用简化几何体表示:
- 基座: 圆柱体 (灰色)
- 连杆 1-6: 圆柱体或长方体 (蓝色/灰色交替)
- 末端法兰: 小圆盘 (橙色)
- 夹爪: 两个小方块 (绿色)，根据夹爪状态调整间距

### 使用 react-three-fiber

```tsx
<Canvas camera={{ position: [800, 600, 800], fov: 50 }}>
  <ambientLight intensity={0.5} />
  <directionalLight position={[500, 500, 500]} />
  <OrbitControls enableDamping />
  <RobotArm jointAngles={jointAngles} dhParams={DH_PARAMS} />
  <gridHelper args={[2000, 20]} />
</Canvas>
```

### 两种使用场景

| 场景 | 容器尺寸 | 说明 |
|------|---------|------|
| 对弈页面 | ~200x150px | 右侧小窗口，只读预览 |
| 手操页面 | 全宽 x 45vh | 大视口，支持触摸旋转缩放 |

共用同一个 `<RobotView3D>` 组件，通过 props 传入不同尺寸。

## 9. 全局状态管理

### WebSocket Provider

```tsx
// contexts/RobotStateContext.tsx

interface RobotState {
  joints: number[];          // [J1, J2, J3, J4, J5, J6] 单位: 度
  cartesian: {
    x: number; y: number; z: number;     // mm
    rx: number; ry: number; rz: number;   // 度
  };
  gripper: 'open' | 'closed';
  board: (null | 'O' | 'X')[][];         // 3x3
  gamePhase: 'idle' | 'drawing_board' | 'wait_human' | 'recognizing'
           | 'robot_thinking' | 'countdown' | 'moving' | 'braked'
           | 'game_over' | 'erasing';
  safetyState: 'braked' | 'countdown' | 'moving';
  countdown: number;                      // 倒计时剩余秒数
  score: { wins: number; losses: number; draws: number };
  ipcConnected: boolean;
}
```

### Provider 实现要点

- 组件挂载时连接 `ws://<host>:8000/ws/state`
- 收到 JSON 消息后 `setState(parse(message.data))`
- 每 5 秒发送心跳: `ws.send(JSON.stringify({ type: "heartbeat" }))`
- 断连超过 2 秒: 设置 `safetyState = 'disconnected'`，显示断连警告
- 自动重连: 断连后每 2 秒尝试重连

### 后端推送的 JSON 格式

```json
{
  "joints": [45.2, -30.0, 60.5, 0.0, -45.3, 12.1],
  "cartesian": {"x": 320.1, "y": 150.3, "z": 400.2, "rx": 0.0, "ry": 180.0, "rz": 0.0},
  "gripper": "closed",
  "board": [[null, "O", null], [null, "X", null], ["O", null, null]],
  "game_phase": "wait_human",
  "safety_state": "braked",
  "countdown": 0,
  "score": {"wins": 2, "losses": 1, "draws": 0},
  "ipc_connected": true
}
```

## 10. REST API 端点汇总

| 方法 | 路径 | 用途 | 请求体 |
|------|------|------|--------|
| POST | `/api/game/start` | 开始新局 | `{ difficulty: "easy"\|"medium"\|"hard", first_move: "human"\|"robot" }` |
| POST | `/api/game/reset` | 重置对局 | - |
| GET | `/api/game/state` | 查询游戏状态 | - |
| POST | `/api/robot/jog` | 点动 | `{ mode: "joint"\|"cartesian", axis: 0-5, direction: "+"\|"-", speed_ratio: 0.01-1.0 }` |
| POST | `/api/robot/jog/stop` | 停止点动 | - |
| POST | `/api/robot/io` | IO 控制 | `{ pin: "gripper"\|"alarm", state: "open"\|"close"\|"on"\|"off" }` |
| POST | `/api/robot/estop` | 软件急停 | - |
| GET | `/api/robot/status` | 查询机器人状态 | - |
| POST | `/api/system/calibrate` | 触发标定 | `{ type: "whiteboard"\|"handeye" }` |
| GET | `/api/system/health` | 系统健康 | - |

## 11. 文件结构建议

```
src/
  app/
    layout.tsx          # 全局 Layout: Tab 导航 + SafetyStatusBar
    game/
      page.tsx          # 对弈页面
    jog/
      page.tsx          # 手操页面
    settings/
      page.tsx          # 设置页面
  components/
    Board.tsx           # 3x3 棋盘网格
    RobotView3D.tsx     # Three.js 机械臂
    SafetyStatusBar.tsx # 安全状态区域
    JointJogPanel.tsx   # 关节点动面板
    CartesianJogPanel.tsx # 直角坐标点动面板
    SpeedSelector.tsx   # 速度倍率选择器
    IOControlPanel.tsx  # IO 控制面板
  contexts/
    RobotStateContext.tsx  # WebSocket + 全局状态
  lib/
    api.ts              # REST API 调用封装
    dh.ts               # DH 参数和正运动学计算
    safety-styles.ts    # 安全状态颜色/图标/文字映射
  types/
    index.ts            # TypeScript 类型定义
```

## 12. 安全相关的 UI 规范

1. 安全状态区域（`SafetyStatusBar`）在 `layout.tsx` 中渲染，在 Tab 内容区下方，始终可见，不可被遮挡。
2. 颜色编码遵循 ISO 3864: 红色 `#ED1C24`（危险），黄色 `#FFD100`（警告），绿色 `#00A651`（安全）。
3. "开始" 按钮放在右侧控制区，远离棋盘区域，防止误触。
4. 所有按钮的触摸目标不小于 48x48dp。
5. WebSocket 断连超过 2 秒时:
   - 安全状态区域变为深灰色 + "连接中断"
   - 屏幕上方弹出红色警告横幅
   - 禁用所有控制类按钮（Jog、IO、开始等）

## 13. Mockup 占位

以下两处需要制作 mockup 图片，放入报告 `images/` 目录:

- `images/mockup_game.png` - 对弈页面的完整截图或线框图
- `images/mockup_jog.png` - 手操页面的完整截图或线框图

可以用 Figma、Excalidraw 或浏览器截图制作。
