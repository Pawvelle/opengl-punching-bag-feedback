# 基于 C++ 与 OpenGL 的 3D 拳击受力反馈沙袋交互系统

> Course Project — C++17 + OpenGL 3.3 Core Profile + GLFW + GLEW + GLM

---

## 项目简介

本项目实现了一个实时 3D 拳击沙袋模拟系统：

- 玩家通过**快速滑动鼠标**模拟出拳
- 系统检测鼠标速度与命中位置，向沙袋施加物理冲量
- 沙袋产生真实的摆动、倾斜、自旋和阻尼回弹效果
- 命中瞬间触发粒子爆炸特效

---

## 目录结构

```
opengl-punching-bag-feedback/
├── CMakeLists.txt       # CMake 构建配置
├── src/
│   └── main.cpp         # 全部源代码（含内嵌 GLSL 着色器）
└── README.md
```

---

## 依赖项

| 库      | 说明                        | Windows (vcpkg)        | macOS (Homebrew)     | Linux (apt)            |
|---------|-----------------------------|------------------------|----------------------|------------------------|
| OpenGL  | 图形 API（系统自带）          | 系统预装                | 系统预装              | 系统预装               |
| GLFW 3  | 窗口与输入管理                | `vcpkg install glfw3`  | `brew install glfw`  | `libglfw3-dev`         |
| GLEW    | OpenGL 扩展加载器（macOS 免） | `vcpkg install glew`   | （无需，用系统框架）   | `libglew-dev`          |
| GLM     | 向量/矩阵数学库（纯头文件）    | `vcpkg install glm`    | `brew install glm`   | `libglm-dev`           |
| CMake   | 构建系统（≥ 3.16）            | 官网安装               | `brew install cmake` | `cmake`                |

**一键安装依赖：**

```bash
# macOS（注意：macOS 用系统 OpenGL 框架，无需 glew）
brew install cmake glfw glm

# Linux (Debian/Ubuntu)
sudo apt install cmake libglfw3-dev libglew-dev libglm-dev

# Windows (vcpkg)
vcpkg install glfw3 glm glew --triplet x64-windows
```

> macOS 通过系统 `<OpenGL/gl3.h>` 直接获取 OpenGL 函数，因此**不需要 GLEW**；
> Windows / Linux 则需要 GLEW 加载 OpenGL 扩展函数指针。`main.cpp` 已用 `#ifdef __APPLE__` 自动区分。

---

## 编译与运行

### macOS / Linux

```bash
# 进入项目目录
cd opengl-punching-bag-feedback

# 创建构建目录（-p：目录已存在也不会报错）
mkdir -p build && cd build

# 配置（CMake 会自动检测 Homebrew 路径）
cmake ..

# 编译（--parallel 让 CMake 自动并行，跨平台通用）
cmake --build . --parallel

# 运行
./punching_bag
```

---

### Windows（推荐使用 vcpkg + Visual Studio / CMake）

> **重要提示：项目路径请使用纯英文目录**（例如 `D:\code\opengl-punching-bag-feedback`）。
> 中文路径或带空格的路径常导致 CMake 配置失败或 vcpkg 依赖识别异常。

#### 1. 安装 vcpkg（若尚未安装）

```bat
git clone https://github.com/microsoft/vcpkg.git C:\dev\vcpkg
cd C:\dev\vcpkg
bootstrap-vcpkg.bat
```

建议把 vcpkg 根目录设为环境变量 `VCPKG_ROOT`（本项目的 CMake 会自动检测它）：

```bat
setx VCPKG_ROOT C:\dev\vcpkg
```

> 设置后请重新打开终端使环境变量生效。

#### 2. 使用 vcpkg 安装依赖（glfw3 / glm / glew）

```bat
vcpkg install glfw3 glm glew --triplet x64-windows
```

#### 3. 使用 CMake 配置项目

```bat
cd D:\code\opengl-punching-bag-feedback
mkdir build
cd build
```

**方式 A：已设置 `VCPKG_ROOT` 环境变量**（CMake 会自动启用 vcpkg 工具链）

```bat
cmake .. -A x64
```

**方式 B：手动指定 vcpkg 工具链文件**

```bat
cmake .. -A x64 -DCMAKE_TOOLCHAIN_FILE=C:\dev\vcpkg\scripts\buildsystems\vcpkg.cmake
```

#### 4. 编译 Release 版本

```bat
cmake --build . --config Release
```

#### 5. 运行可执行文件

```bat
.\Release\punching_bag.exe
```

> 若使用 Ninja 等单配置生成器，可执行文件直接位于 `build\punching_bag.exe`。

#### Windows 说明

- 程序已在 `main()` 中调用 `SetConsoleOutputCP(CP_UTF8)`，控制台中文输出不会乱码。
- CMake 已为 MSVC 自动添加 `/utf-8` 编译选项，保证含中文注释的源码正确编译。
- GLEW 在 Windows 下用于加载 OpenGL 扩展函数，已在创建 GLFW 窗口并设置上下文后再调用 `glewInit()`，顺序正确。

> **运行时 DLL 提醒**：vcpkg 的 `x64-windows` 三联生成的是动态库，程序运行时需要
> `glfw3.dll`、`glew32.dll`。使用 vcpkg 工具链 + Visual Studio 生成器时，CMake 通常会
> 自动把这些 DLL 拷贝到 exe 同目录。若双击运行报“缺少 xxx.dll”，请手动将这两个 DLL
> （位于 `<vcpkg>\installed\x64-windows\bin\`）复制到 `punching_bag.exe` 所在目录即可。
> 也可改用静态三联 `--triplet x64-windows-static` 安装依赖，从而生成不依赖 DLL 的可执行文件。

---

## 操作方式

| 操作                          | 效果                                 |
|-------------------------------|--------------------------------------|
| 在沙袋屏幕区域**快速滑动鼠标** | 出拳击打沙袋（速度越快力度越大）      |
| 向左/右滑动                    | 沙袋左右摆动                         |
| 向上/下滑动                    | 沙袋前后摆动                         |
| 命中沙袋**左/右侧**             | 产生顺/逆时针自旋                    |
| `R` 键                        | 重置沙袋到初始静止位置               |
| `ESC` 键                      | 退出程序                             |

> 窗口标题栏实时显示当前击打力度（%）和总命中次数。

---

## 核心技术实现

### 1. 渲染管线

- OpenGL 3.3 Core Profile（使用 VAO/VBO，无立即模式）
- **Blinn-Phong 光照**：环境光 + 漫反射 + 镜面反射
- 4× MSAA 抗锯齿
- 粒子系统：`GL_POINTS` + 点精灵圆形剪裁

### 2. 几何体（手写三角面片，无外部模型）

| 物体     | 实现方式                |
|----------|-------------------------|
| 沙袋     | 32 段圆柱（侧面 + 顶底面）|
| 地面     | 单个矩形平面              |
| 链条     | 8 段细圆柱（Y 轴动态缩放）|
| 顶部金属环 | 24 段扁平圆柱           |
| 横梁     | 8 段粗圆柱（90° 旋转横置）|

### 3. 物理模拟（自行实现）

```
沙袋 = 摆锤模型
  角加速度 = -(g/L) × θ  （小角度近似，摆钟公式）
  阻尼     = pow(factor, dt×60)  （帧率无关衰减）
  角度限制 = [-43°, +43°]  （防止飞走）
  自旋     = 偏心冲量 × 独立阻尼
```

- **位移**：Euler 积分（每帧更新角速度和角度）
- **倾斜**：沙袋本体沿摆臂方向旋转（叉积求旋转轴）
- **阻尼**：指数衰减，约 3–4 秒恢复静止
- **回弹**：重力恢复力自然产生弹性效果

### 4. 命中检测

1. 将沙袋 3D 中心点通过 MVP 矩阵投影到屏幕像素坐标
2. 判断鼠标是否在投影圆形区域内（半径约 95px）
3. 鼠标单次移动 ≥ 22px 触发出拳判定
4. 力度 = 移动距离 / 220px（clamp 到 [0, 1]）

### 5. 粒子爆炸效果

- 命中时生成 20–80 个粒子
- 初速：沿命中方向 + 随机散射
- 物理：重力 + 空气阻力（线性衰减）
- 颜色：暗红 → 橙黄（渐变，随寿命透明度递减）
- 批量绘制为 `GL_POINTS`，无额外 draw call 开销

---

## 可调参数（`src/main.cpp` 顶部常量）

| 参数                    | 默认值 | 说明                     |
|-------------------------|--------|--------------------------|
| `g_bag.pendLen`         | 1.4 m  | 摆长（影响摆动周期）      |
| `g_bag.damping`         | 0.996  | 摆动阻尼（越小衰减越快）  |
| `g_bag.spinDamp`        | 0.990  | 自旋阻尼                 |
| `SWIPE_MIN`（命中检测） | 22 px  | 触发出拳的最小滑动距离    |
| `SWIPE_MAX`（命中检测） | 220 px | 满力度对应的滑动距离      |
| `BAG_R`                 | 0.28 m | 沙袋半径                 |
| `BAG_H`                 | 1.20 m | 沙袋高度                 |

---

## 运行截图说明

- 场景背景为深蓝黑色（模拟室内健身房）
- 沙袋默认深红色，命中瞬间闪金黄色
- 窗口标题实时显示力度与命中次数
- 控制台输出每次命中的详细信息（力度、偏移位置）

---

## 已知限制（第一版）

- 文字 HUD 使用窗口标题栏显示（未实现 in-window bitmap 字体）
- 命中检测为屏幕空间 2D 圆形判定，未做精确 3D 碰撞
- 粒子数量上限 1200（防止 VBO 溢出）
- 单一光源，无阴影

---

## 开发环境

- macOS 14+ / Apple Silicon
- Xcode Command Line Tools
- CMake 3.16+
- C++17

---

*课程项目 · OpenGL 交互图形学实践*
