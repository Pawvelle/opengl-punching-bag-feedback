/*
 * ============================================================
 * 基于 C++ 与 OpenGL 的 3D 拳击受力反馈沙袋交互系统
 * Course Project — C++17 + OpenGL 3.3 Core + GLFW + GLM
 *
 * 操作说明:
 *   - 在沙袋屏幕区域快速滑动鼠标 → 出拳
 *   - 滑动越快，击打力度越大
 *   - 命中左/右侧会产生不同的自旋方向
 *   - R 键重置沙袋位置
 *   - ESC 退出
 * ============================================================
 */

// ---- Windows 平台宏：必须早于任何系统/库头文件（尤其 glew.h 会内部包含 windows.h） ----
// NOMINMAX：避免 windows.h 定义 min/max 宏，破坏代码中的 std::min/std::max
// WIN32_LEAN_AND_MEAN：精简 windows.h，加快编译
#ifdef _WIN32
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#endif

// macOS 直接使用系统 OpenGL 3.x 头文件，无需 GLEW
// Windows/Linux 使用 GLEW 加载扩展函数指针
#ifdef __APPLE__
#  ifndef GL_SILENCE_DEPRECATION   // 若 CMake 已通过 -D 定义则不重复定义
#    define GL_SILENCE_DEPRECATION
#  endif
#  include <OpenGL/gl3.h>   // macOS 原生 OpenGL 3.3+ 头（含所有函数声明）
#else
#  include <GL/glew.h>      // Windows/Linux 需要 GLEW 加载扩展
#endif

// 阻止 GLFW 自行包含 <GL/gl.h>，避免与 GLEW 的 GL 声明潜在重复
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

// Windows：引入 windows.h 用于设置控制台 UTF-8 编码（NOMINMAX 等宏已在文件顶部定义）
#ifdef _WIN32
#  include <windows.h>
#endif

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <random>
#include <sstream>
#include <vector>

// ============================================================
// 数学常量
// ============================================================
static constexpr float PI  = 3.14159265358979323846f;
static constexpr float PI2 = PI * 2.f;

// ============================================================
// 窗口初始尺寸
// ============================================================
static int g_width  = 1280;
static int g_height = 720;

// ============================================================
// GLSL 着色器源码（内嵌字符串，避免外部文件依赖）
// ============================================================

// ---- 主场景顶点着色器：Phong 光照 ----
static const char* kMainVert = R"GLSL(
#version 330 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;

uniform mat4 uModel;
uniform mat4 uView;
uniform mat4 uProj;

out vec3 vFragPos;  // 世界空间顶点位置
out vec3 vNormal;   // 世界空间法线

void main() {
    vFragPos = vec3(uModel * vec4(aPos, 1.0));
    // 法线矩阵 = 模型矩阵的逆转置，正确处理非均匀缩放
    vNormal  = mat3(transpose(inverse(uModel))) * aNormal;
    gl_Position = uProj * uView * vec4(vFragPos, 1.0);
}
)GLSL";

// ---- 主场景片段着色器：Blinn-Phong ----
static const char* kMainFrag = R"GLSL(
#version 330 core
in vec3 vFragPos;
in vec3 vNormal;

uniform vec3  uLightPos;    // 光源位置（世界空间）
uniform vec3  uViewPos;     // 相机位置（世界空间）
uniform vec3  uColor;       // 物体颜色
uniform float uAmbient;     // 环境光强度
uniform float uAlpha;       // 透明度

out vec4 FragColor;

void main() {
    vec3 lightColor = vec3(1.0, 0.95, 0.88);  // 暖白光

    // 1. 环境光（模拟间接照明）
    vec3 ambient = uAmbient * lightColor;

    // 2. 漫反射（Lambert 模型）
    vec3 norm     = normalize(vNormal);
    vec3 lightDir = normalize(uLightPos - vFragPos);
    float diff    = max(dot(norm, lightDir), 0.0);
    vec3 diffuse  = diff * lightColor;

    // 3. 镜面反射（Blinn-Phong，减少计算量）
    vec3 viewDir    = normalize(uViewPos - vFragPos);
    vec3 halfwayDir = normalize(lightDir + viewDir);
    float spec      = pow(max(dot(norm, halfwayDir), 0.0), 64.0);
    vec3 specular   = 0.35 * spec * lightColor;

    vec3 result = (ambient + diffuse + specular) * uColor;
    FragColor   = vec4(result, uAlpha);
}
)GLSL";

// ---- 粒子顶点着色器：GL_POINTS 点精灵 ----
static const char* kPartVert = R"GLSL(
#version 330 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec4 aColor;

uniform mat4  uMVP;
uniform float uScale;  // 粒子大小基数

out vec4 vColor;

void main() {
    gl_Position  = uMVP * vec4(aPos, 1.0);
    // 根据深度（w 分量）缩放粒子像素大小，模拟透视
    gl_PointSize = max(2.0, uScale * 8.0 / gl_Position.w);
    vColor       = aColor;
}
)GLSL";

// ---- 粒子片段着色器：圆形软边点 ----
static const char* kPartFrag = R"GLSL(
#version 330 core
in vec4 vColor;
out vec4 FragColor;

void main() {
    // gl_PointCoord 范围 [0,1]×[0,1]，中心为 (0.5, 0.5)
    vec2  d = gl_PointCoord - vec2(0.5);
    float r = dot(d, d);       // 到中心距离的平方
    if (r > 0.25) discard;    // 丢弃圆形外的像素
    float alpha = (1.0 - r * 4.0) * vColor.a;  // 边缘软化
    FragColor = vec4(vColor.rgb, alpha);
}
)GLSL";

// ============================================================
// 着色器编译与链接
// ============================================================
static GLuint compileShader(GLenum type, const char* src) {
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, nullptr);
    glCompileShader(s);
    GLint ok;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[1024];
        glGetShaderInfoLog(s, sizeof(log), nullptr, log);
        std::cerr << "[ShaderError] " << log << "\n";
    }
    return s;
}

static GLuint makeProgram(const char* vert, const char* frag) {
    GLuint vs  = compileShader(GL_VERTEX_SHADER,   vert);
    GLuint fs  = compileShader(GL_FRAGMENT_SHADER, frag);
    GLuint prg = glCreateProgram();
    glAttachShader(prg, vs);
    glAttachShader(prg, fs);
    glLinkProgram(prg);
    GLint ok;
    glGetProgramiv(prg, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[1024];
        glGetProgramInfoLog(prg, sizeof(log), nullptr, log);
        std::cerr << "[LinkError] " << log << "\n";
    }
    glDeleteShader(vs);
    glDeleteShader(fs);
    return prg;
}

// ============================================================
// 几何体生成（CPU 端手写三角面片）
// ============================================================

// 生成圆柱体（含顶面/底面），每顶点格式：x y z nx ny nz（6 float）
static void buildCylinder(std::vector<float>& v, int seg, float r, float h) {
    float hh = h * 0.5f;
    for (int i = 0; i < seg; ++i) {
        float a0 = (float)i       / seg * PI2;
        float a1 = (float)(i + 1) / seg * PI2;
        float x0 = cosf(a0)*r, z0 = sinf(a0)*r;
        float x1 = cosf(a1)*r, z1 = sinf(a1)*r;
        // 侧面外法线（y 分量为 0）
        float nx0 = cosf(a0), nz0 = sinf(a0);
        float nx1 = cosf(a1), nz1 = sinf(a1);

        // 侧面：两个三角形拼成矩形
        v.insert(v.end(), {
            x0,-hh,z0, nx0,0,nz0,
            x1,-hh,z1, nx1,0,nz1,
            x1, hh,z1, nx1,0,nz1,
            x0,-hh,z0, nx0,0,nz0,
            x1, hh,z1, nx1,0,nz1,
            x0, hh,z0, nx0,0,nz0,
        });
        // 顶面（法线朝上）
        v.insert(v.end(), {
            0, hh,0, 0,1,0,
            x0,hh,z0, 0,1,0,
            x1,hh,z1, 0,1,0,
        });
        // 底面（法线朝下，顶点顺序翻转）
        v.insert(v.end(), {
            0,-hh,0, 0,-1,0,
            x1,-hh,z1, 0,-1,0,
            x0,-hh,z0, 0,-1,0,
        });
    }
}

// 生成地面平面（size×size 的矩形，y=0）
static void buildGround(std::vector<float>& v, float size) {
    v = {
        -size,0,-size, 0,1,0,
         size,0,-size, 0,1,0,
         size,0, size, 0,1,0,
        -size,0,-size, 0,1,0,
         size,0, size, 0,1,0,
        -size,0, size, 0,1,0,
    };
}

// 生成单位四边形（XY 平面，原点在左下角，法线 +Z）—— 用于 HUD 力度条
static void buildQuad(std::vector<float>& v) {
    v = {
        0,0,0, 0,0,1,
        1,0,0, 0,0,1,
        1,1,0, 0,0,1,
        0,0,0, 0,0,1,
        1,1,0, 0,0,1,
        0,1,0, 0,0,1,
    };
}

// 生成扁平圆环（XY 平面，法线 +Z）—— 用于命中冲击环
static void buildRingFlat(std::vector<float>& v, int seg, float rIn, float rOut) {
    for (int i = 0; i < seg; ++i) {
        float a0 = (float)i       / seg * PI2;
        float a1 = (float)(i + 1) / seg * PI2;
        float c0 = cosf(a0), s0 = sinf(a0);
        float c1 = cosf(a1), s1 = sinf(a1);
        // 内外两圈构成一段四边形（两个三角形）
        v.insert(v.end(), {
            c0*rIn,  s0*rIn,  0, 0,0,1,
            c0*rOut, s0*rOut, 0, 0,0,1,
            c1*rOut, s1*rOut, 0, 0,0,1,
            c0*rIn,  s0*rIn,  0, 0,0,1,
            c1*rOut, s1*rOut, 0, 0,0,1,
            c1*rIn,  s1*rIn,  0, 0,0,1,
        });
    }
}

// 上传顶点数据到 GPU，返回 VAO
static GLuint uploadMesh(const std::vector<float>& v, GLuint& outVBO) {
    GLuint vao, vbo;
    glGenVertexArrays(1, &vao);
    glGenBuffers(1, &vbo);
    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER,
                 (GLsizeiptr)(v.size() * sizeof(float)),
                 v.data(), GL_STATIC_DRAW);
    // location=0: position (xyz)
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE,
                          6*sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    // location=1: normal (xyz)
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE,
                          6*sizeof(float), (void*)(3*sizeof(float)));
    glEnableVertexAttribArray(1);
    glBindVertexArray(0);
    outVBO = vbo;
    return vao;
}

// ============================================================
// 物理系统：沙袋摆锤模型
// ============================================================
struct BagPhysics {
    // 悬挂固定点（天花板钩子位置）
    glm::vec3 pivot{ 0.f, 3.3f, 0.f };

    // 摆动角度（弧度），以竖直方向为零点
    float angleX = 0.f;   // 左右摆动角（绕 Z 轴等效）
    float angleZ = 0.f;   // 前后摆动角（绕 X 轴等效）
    float velX   = 0.f;   // X 方向角速度
    float velZ   = 0.f;   // Z 方向角速度

    // 自旋（绕沙袋纵轴旋转）
    float spinAngle = 0.f;
    float spinVel   = 0.f;

    // 物理参数
    float pendLen  = 1.4f;    // 摆长（悬挂点到沙袋重心距离）
    float gravity  = 9.8f;    // 重力加速度 (m/s²)
    float damping  = 0.996f;  // 摆动阻尼系数（per-frame @60fps基准）
    float spinDamp = 0.990f;  // 自旋阻尼

    // 计算沙袋当前重心位置
    glm::vec3 center() const {
        // 总摆角（两个方向的合成）
        float total = sqrtf(angleX*angleX + angleZ*angleZ);
        float cosA  = (total > 1e-5f) ? cosf(total) : 1.f;
        return {
            pivot.x + pendLen * sinf(angleX),
            pivot.y - pendLen * cosA,
            pivot.z + pendLen * sinf(angleZ)
        };
    }

    // 每帧物理积分（Euler 法 + 小角度近似）
    void step(float dt) {
        // 摆钟恢复力：角加速度 = -(g/L)·θ
        float omega2 = gravity / pendLen;
        velX += -omega2 * angleX * dt;
        velZ += -omega2 * angleZ * dt;

        // 阻尼：将 60fps 基准系数转换为任意帧率
        float d = powf(damping, dt * 60.f);
        velX *= d;
        velZ *= d;

        // 积分角度
        angleX += velX * dt;
        angleZ += velZ * dt;

        // 限制最大摆角（约 43°），防止沙袋"飞走"
        const float MAX_A = 0.75f;
        angleX = std::clamp(angleX, -MAX_A, MAX_A);
        angleZ = std::clamp(angleZ, -MAX_A, MAX_A);

        // 自旋阻尼 + 积分
        spinVel   *= powf(spinDamp, dt * 60.f);
        spinAngle += spinVel * dt;
    }

    // 施加击打冲量（外部调用）
    void impulse(float ix, float iz, float iSpin) {
        velX     += ix;
        velZ     += iz;
        spinVel  += iSpin;
    }
};

// ============================================================
// 粒子系统
// ============================================================
struct Particle {
    glm::vec3 pos;     // 位置
    glm::vec3 vel;     // 速度
    glm::vec3 color;   // RGB 颜色
    float life;        // 剩余寿命（秒）
    float maxLife;     // 初始寿命
};

static std::vector<Particle> g_particles;
static std::mt19937 g_rng{ std::random_device{}() };

// 在命中点爆发粒子
static void emitParticles(glm::vec3 pos, glm::vec3 dir, float force) {
    std::uniform_real_distribution<float> rndS(-1.f, 1.f);
    std::uniform_real_distribution<float> rndP( 0.f, 1.f);

    // 力度越大，粒子越多（增强：基数与上限均提高）
    int count = (int)(30.f + force * 110.f);
    count = std::min(count, 160);

    for (int i = 0; i < count && (int)g_particles.size() < 1200; ++i) {
        Particle p;
        // 初始位置：命中点附近小范围随机扰动
        p.pos = pos + glm::vec3(rndS(g_rng), rndS(g_rng), rndS(g_rng)) * 0.06f;

        // 速度：主体沿命中方向飞出，附加随机散射（扩散随力度增大更明显）
        float speed = (1.f + rndP(g_rng) * 3.5f) * (0.5f + force * 1.4f);
        p.vel = dir * speed
              + glm::vec3(rndS(g_rng), rndP(g_rng) * 0.5f + 0.3f, rndS(g_rng)) * (1.8f + force * 2.4f);

        // 颜色渐变：暗红 → 亮橙黄（模拟冲击火星）
        float t = rndP(g_rng);
        p.color = glm::mix(glm::vec3(1.f, 0.12f, 0.f),
                           glm::vec3(1.f, 0.85f, 0.1f), t);

        p.maxLife = 0.25f + rndP(g_rng) * 0.55f;
        p.life    = p.maxLife;
        g_particles.push_back(p);
    }
}

// 粒子物理更新：重力 + 空气阻力 + 消亡
static void stepParticles(float dt) {
    for (auto& p : g_particles) {
        p.pos  += p.vel * dt;
        p.vel.y -= 7.f * dt;          // 重力下落
        p.vel   *= (1.f - 1.8f * dt); // 空气阻力衰减
        p.life  -= dt;
    }
    // 移除寿命耗尽的粒子
    g_particles.erase(
        std::remove_if(g_particles.begin(), g_particles.end(),
            [](const Particle& p){ return p.life <= 0.f; }),
        g_particles.end());
}

// ============================================================
// 命中冲击环系统：命中点出现短暂放大并消失的半透明圆环
// ============================================================
struct ImpactRing {
    glm::vec3 pos;     // 世界坐标（命中点）
    float age;         // 已存在时间（秒）
    float maxAge;      // 总寿命（秒）
    float force;       // 命中力度（决定圆环大小）
};
static std::vector<ImpactRing> g_rings;

// 在命中点生成一个冲击环
static void emitRing(glm::vec3 pos, float force) {
    g_rings.push_back({ pos, 0.f, 0.35f, force });
}

// 冲击环更新：累加寿命并移除过期者
static void stepRings(float dt) {
    for (auto& r : g_rings) r.age += dt;
    g_rings.erase(
        std::remove_if(g_rings.begin(), g_rings.end(),
            [](const ImpactRing& r){ return r.age >= r.maxAge; }),
        g_rings.end());
}

// ============================================================
// 全局状态
// ============================================================
static GLFWwindow* g_win = nullptr;

// 物理
static BagPhysics g_bag;

// 游戏统计
static int   g_hitCount  = 0;
static float g_lastForce = 0.f;
static float g_flashTime = 0.f;   // 命中后沙袋闪光计时

// 反馈增强相关
static double g_lastHitTime = -1.0;           // 上次命中时间戳（glfwGetTime），用于冷却判断
static constexpr double HIT_COOLDOWN = 0.12;  // 击打冷却时间（秒），防止一次滑动连发
static float  g_shakeTime    = 0.f;           // 相机震动剩余时间（秒）
static float  g_shakeMag     = 0.f;           // 相机震动强度（米）
static constexpr float SHAKE_DUR = 0.16f;     // 相机震动总时长（秒）
static float  g_forceDisplay = 0.f;           // HUD 力度条显示值（带缓慢衰减）

// 鼠标输入
static double g_mouseX   = 0.0, g_mouseY   = 0.0;
static double g_prevX    = 0.0, g_prevY    = 0.0;
static bool   g_firstMov = true;

// 相机（固定视角）
static glm::vec3 g_camPos{ 0.f, 2.4f, 5.0f };
static glm::vec3 g_camTgt{ 0.f, 1.7f, 0.f  };

// 光源（位于沙袋右上方）
static glm::vec3 g_lightPos{ 2.5f, 5.5f, 3.f };

// 沙袋尺寸常量
static constexpr float BAG_R = 0.28f;  // 半径（m）
static constexpr float BAG_H = 1.20f;  // 高度（m）

// GPU 对象
static GLuint g_progMain = 0, g_progPart = 0;

struct MeshObj { GLuint vao=0, vbo=0; int count=0; };
static MeshObj g_mBag, g_mGnd, g_mChain, g_mRing, g_mBeam;
static MeshObj g_mQuad, g_mRingFlat;   // HUD 力度条 / 命中冲击环
static GLuint  g_partVAO = 0, g_partVBO = 0;

// ============================================================
// Uniform 工具函数
// ============================================================
static void uMat4(GLuint p, const char* n, const glm::mat4& m) {
    glUniformMatrix4fv(glGetUniformLocation(p, n), 1, GL_FALSE, glm::value_ptr(m));
}
static void uVec3(GLuint p, const char* n, const glm::vec3& v) {
    glUniform3fv(glGetUniformLocation(p, n), 1, glm::value_ptr(v));
}
static void uFloat(GLuint p, const char* n, float v) {
    glUniform1f(glGetUniformLocation(p, n), v);
}

// 绘制一个网格（设置好 uniform 后调用）
static void drawMesh(const MeshObj& m) {
    glBindVertexArray(m.vao);
    glDrawArrays(GL_TRIANGLES, 0, m.count);
}

// ============================================================
// 命中检测
// ============================================================
static void tryHit(double dx, double dy) {
    // 击打冷却：用 glfwGetTime() 记录上次命中时间，距上次不足 HIT_COOLDOWN 秒则忽略。
    // 这样一次快速滑动跨越的多帧鼠标回调只判定一次命中，但不影响正常的连续快速出拳。
    double nowT = glfwGetTime();
    if (g_lastHitTime >= 0.0 && (nowT - g_lastHitTime) < HIT_COOLDOWN) return;

    // 鼠标单次移动距离（像素）
    float dist = sqrtf((float)(dx*dx + dy*dy));

    // 最小滑动距离阈值（低于此值视为普通移动，不触发出拳）
    static constexpr float SWIPE_MIN = 22.f;
    static constexpr float SWIPE_MAX = 220.f;
    if (dist < SWIPE_MIN) return;

    // ---- 将沙袋中心投影到屏幕坐标 ----
    glm::vec3 bagPos = g_bag.center();
    glm::mat4 view   = glm::lookAt(g_camPos, g_camTgt, {0.f, 1.f, 0.f});
    glm::mat4 proj   = glm::perspective(glm::radians(45.f),
                                        (float)g_width / g_height, 0.1f, 100.f);
    glm::vec4 clip = proj * view * glm::vec4(bagPos, 1.f);
    // 归一化设备坐标 → 屏幕像素坐标
    glm::vec3 ndc = glm::vec3(clip) / clip.w;
    float sx = (ndc.x + 1.f) * 0.5f * g_width;
    float sy = (1.f - ndc.y) * 0.5f * g_height;  // Y 轴翻转

    // 沙袋在屏幕上的近似半径（像素），保持宽松以增加游戏可玩性
    float screenR = 95.f;

    // 检测：鼠标是否在沙袋屏幕范围内
    float mdx = (float)g_mouseX - sx;
    float mdy = (float)g_mouseY - sy;
    if (sqrtf(mdx*mdx + mdy*mdy) > screenR * 1.5f) return;  // 未命中

    // ===== 命中成功 =====
    ++g_hitCount;
    g_flashTime   = 0.5f;
    g_lastHitTime = nowT;     // 记录命中时间，进入冷却

    // 力度映射：先把滑动距离归一化到 [0,1]，再用 smoothstep 风格 S 曲线。
    // 小幅滑动被压低（手感更柔和），中高幅快速滑动迅速上升（更有爆发感）。
    // 显式展开 smoothstep：3t² - 2t³，结果始终落在 [0,1]，不依赖 glm::smoothstep。
    float rawForce = std::clamp((dist - SWIPE_MIN) / (SWIPE_MAX - SWIPE_MIN), 0.f, 1.f);
    float force    = rawForce * rawForce * (3.f - 2.f * rawForce);
    g_lastForce    = force;   // 保留用于窗口标题显示
    g_forceDisplay = force;   // 同步更新 HUD 力度条

    // 触发相机震动：力度越大震动越明显
    g_shakeTime = SHAKE_DUR;
    g_shakeMag  = 0.025f + force * 0.05f;

    // 鼠标滑动方向（单位向量，屏幕坐标系）
    float dirX = (float)dx / dist;  // 正 = 向右
    float dirY = (float)dy / dist;  // 正 = 向下

    // 命中位置偏移（相对于沙袋屏幕中心，-1 到 1）
    float relX = std::clamp(mdx / screenR, -1.f, 1.f);  // 负 = 命中左侧
    float relY = std::clamp(mdy / screenR, -1.f, 1.f);  // 负 = 命中上部

    // ---- 冲量分解（增强方向感与命中位置区分） ----
    // 水平滑动 → 沙袋左右摆动（angleX 方向，dirX>0 向右、dirX<0 向左）
    // 任意强力出拳 → 额外把沙袋推向后方（远离相机, -Z），更符合"被击中"直觉
    // 命中位置：下部(relY>0)力臂长→摆幅更大，上部(relY<0)力臂短→摆幅更小
    // 偏侧命中(relX) → 绕纵轴自旋，命中左/右侧自旋方向相反
    float scale = force * 4.6f;                     // 摆动冲量随力度增强

    // 纵向命中位置修正系数：下部放大、上部缩小摆幅
    float vert  = 1.f + relY * 0.35f;

    // 左右摆动：滑动横向分量 + 命中偏移横推，再乘命中位置修正
    float impX  = (dirX * scale + relX * force) * vert;
    // 前后摆动：鼠标垂直分量带来的前后推力，叠加"横扫也会把沙袋推向后方"的分量
    float impZ  = (dirY * scale * 0.45f
                   - fabsf(dirX) * scale * 0.30f) * vert;
    // 自旋：偏心命中越靠边，自旋越强（左右方向相反）
    float impSpin = relX * force * 3.6f;

    // 注意：BagPhysics::step() 内仍有最大摆角钳制(±0.75rad)与阻尼，沙袋不会飞走
    g_bag.impulse(impX, impZ, impSpin);

    // 命中点（沙袋朝向相机面的表面）
    glm::vec3 hitPt  = bagPos + glm::vec3(relX * BAG_R,
                                           -relY * BAG_H * 0.38f,
                                            BAG_R * 0.9f);
    glm::vec3 hitDir = glm::normalize(glm::vec3(dirX, -dirY * 0.4f, 0.8f));
    emitParticles(hitPt, hitDir, force);
    emitRing(hitPt, force);   // 命中点生成冲击环

    // 控制台输出（方便调试与课程报告截图）
    std::cout << "[击中] 力度=" << std::fixed << std::setprecision(0)
              << (force * 100.f) << "% | 总命中=" << g_hitCount
              << " | 位置偏移=(L/R " << relX << ", U/D " << relY << ")\n";
}

// ============================================================
// GLFW 回调
// ============================================================
static void cbCursorPos(GLFWwindow*, double x, double y) {
    if (g_firstMov) { g_prevX = x; g_prevY = y; g_firstMov = false; }
    g_mouseX = x; g_mouseY = y;
    tryHit(x - g_prevX, y - g_prevY);
    g_prevX = x; g_prevY = y;
}

static void cbFBSize(GLFWwindow*, int w, int h) {
    g_width = w; g_height = h;
    glViewport(0, 0, w, h);
}

static void cbKey(GLFWwindow* win, int key, int, int action, int) {
    if (action != GLFW_PRESS) return;
    if (key == GLFW_KEY_ESCAPE) glfwSetWindowShouldClose(win, GLFW_TRUE);
    if (key == GLFW_KEY_R) {
        // 重置沙袋角度 / 速度 / 自旋（原有功能，保持不变）
        g_bag.angleX = g_bag.angleZ = 0.f;
        g_bag.velX   = g_bag.velZ   = 0.f;
        g_bag.spinAngle = g_bag.spinVel = 0.f;
        g_particles.clear();
        g_flashTime = 0.f;
        // 同步归零新增的反馈状态：冷却 / 震动 / 力度条 / 冲击环 / 标题力度
        g_lastHitTime  = -1.0;   // 允许重置后立即出拳
        g_shakeTime    = 0.f;
        g_shakeMag     = 0.f;
        g_forceDisplay = 0.f;    // HUD 力度条归零
        g_lastForce    = 0.f;    // 窗口标题力度显示归零
        g_rings.clear();
        std::cout << "[重置] 沙袋已归位\n";
    }
}

// ============================================================
// 绘制场景辅助
// ============================================================

// 设置主着色器公共 uniform（光照、相机）
// eyePos 传入"实际"相机位置（含震动偏移），保证镜面高光计算与 view 矩阵一致
static void setSceneUniforms(const glm::mat4& view, const glm::mat4& proj,
                             const glm::vec3& eyePos) {
    glUseProgram(g_progMain);
    uMat4(g_progMain, "uView",     view);
    uMat4(g_progMain, "uProj",     proj);
    uVec3(g_progMain, "uLightPos", g_lightPos);
    uVec3(g_progMain, "uViewPos",  eyePos);
}

// 绘制一个主场景网格（设置 model + 材质后调用）
static void drawSceneMesh(const MeshObj& m,
                          const glm::mat4& model,
                          const glm::vec3& color,
                          float ambient = 0.25f,
                          float alpha   = 1.f) {
    uMat4 (g_progMain, "uModel",   model);
    uVec3 (g_progMain, "uColor",   color);
    uFloat(g_progMain, "uAmbient", ambient);
    uFloat(g_progMain, "uAlpha",   alpha);
    drawMesh(m);
}

// ============================================================
// 主函数
// ============================================================
int main() {
    // ---- Windows 控制台 UTF-8 支持（避免中文输出乱码） ----
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);  // 控制台输出使用 UTF-8
    SetConsoleCP(CP_UTF8);        // 控制台输入使用 UTF-8
#endif

    // ---- 初始化 GLFW ----
    if (!glfwInit()) {
        std::cerr << "GLFW 初始化失败\n";
        return -1;
    }
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#ifdef __APPLE__
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
#endif
    glfwWindowHint(GLFW_SAMPLES, 4);  // 4× MSAA 抗锯齿

    g_win = glfwCreateWindow(g_width, g_height,
        "3D Punching Bag  |  Force: 0%  |  Hits: 0  |  Swipe mouse to punch | R=Reset",
        nullptr, nullptr);
    if (!g_win) {
        std::cerr << "窗口创建失败\n";
        glfwTerminate();
        return -1;
    }
    glfwMakeContextCurrent(g_win);
    glfwSetCursorPosCallback      (g_win, cbCursorPos);
    glfwSetFramebufferSizeCallback(g_win, cbFBSize);
    glfwSetKeyCallback            (g_win, cbKey);

    // ---- 初始化 GLEW（仅 Windows/Linux 需要） ----
    // macOS 上 OpenGL 函数由系统库直接提供，无需加载器
#ifndef __APPLE__
    glewExperimental = GL_TRUE;
    if (glewInit() != GLEW_OK) {
        std::cerr << "GLEW 初始化失败\n";
        return -1;
    }
#endif

    // ---- OpenGL 全局配置 ----
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_MULTISAMPLE);         // 启用 MSAA
    glEnable(GL_PROGRAM_POINT_SIZE);  // 允许顶点着色器控制点大小
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glClearColor(0.09f, 0.11f, 0.17f, 1.f);  // 深蓝黑背景（模拟健身房暗光）

    // ---- 编译着色器 ----
    g_progMain = makeProgram(kMainVert, kMainFrag);
    g_progPart = makeProgram(kPartVert, kPartFrag);

    // ---- 生成几何体并上传 GPU ----

    // 沙袋：32 段圆柱（段数越多越圆滑）
    { std::vector<float> v; buildCylinder(v, 32, BAG_R, BAG_H);
      g_mBag.vao = uploadMesh(v, g_mBag.vbo);
      g_mBag.count = (int)v.size() / 6; }

    // 地面：大平面
    { std::vector<float> v; buildGround(v, 7.f);
      g_mGnd.vao = uploadMesh(v, g_mGnd.vbo);
      g_mGnd.count = (int)v.size() / 6; }

    // 链条：细圆柱（高度 1 方便 Y 方向缩放）
    { std::vector<float> v; buildCylinder(v, 8, 0.022f, 1.f);
      g_mChain.vao = uploadMesh(v, g_mChain.vbo);
      g_mChain.count = (int)v.size() / 6; }

    // 顶环：扁平圆柱（金属环）
    { std::vector<float> v; buildCylinder(v, 24, BAG_R * 0.8f, 0.07f);
      g_mRing.vao = uploadMesh(v, g_mRing.vbo);
      g_mRing.count = (int)v.size() / 6; }

    // 横梁：细长圆柱（天花板支撑横梁）
    { std::vector<float> v; buildCylinder(v, 8, 0.05f, 1.2f);
      g_mBeam.vao = uploadMesh(v, g_mBeam.vbo);
      g_mBeam.count = (int)v.size() / 6; }

    // HUD 力度条：单位四边形
    { std::vector<float> v; buildQuad(v);
      g_mQuad.vao = uploadMesh(v, g_mQuad.vbo);
      g_mQuad.count = (int)v.size() / 6; }

    // 命中冲击环：扁平圆环
    { std::vector<float> v; buildRingFlat(v, 40, 0.72f, 1.0f);
      g_mRingFlat.vao = uploadMesh(v, g_mRingFlat.vbo);
      g_mRingFlat.count = (int)v.size() / 6; }

    // 粒子系统：动态 VBO（每粒子 7 个 float: xyz + rgba）
    glGenVertexArrays(1, &g_partVAO);
    glGenBuffers(1, &g_partVBO);
    glBindVertexArray(g_partVAO);
    glBindBuffer(GL_ARRAY_BUFFER, g_partVBO);
    glBufferData(GL_ARRAY_BUFFER, 1200 * 7 * sizeof(float), nullptr, GL_DYNAMIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 7*sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, 7*sizeof(float), (void*)(3*sizeof(float)));
    glEnableVertexAttribArray(1);
    glBindVertexArray(0);

    double lastTime = glfwGetTime();

    // ========== 主渲染循环 ==========
    while (!glfwWindowShouldClose(g_win)) {
        double now = glfwGetTime();
        float  dt  = (float)(now - lastTime);
        lastTime   = now;
        dt = std::min(dt, 0.05f);  // 时间步上限（防止卡顿时物体飞出）

        glfwPollEvents();

        // ---- 更新物理与粒子 ----
        g_bag.step(dt);
        stepParticles(dt);
        stepRings(dt);
        if (g_flashTime > 0.f) g_flashTime = std::max(0.f, g_flashTime - dt * 1.8f);

        // 相机震动 / 力度条 计时更新（冷却已改为基于 glfwGetTime，无需逐帧递减）
        if (g_shakeTime > 0.f) g_shakeTime = std::max(0.f, g_shakeTime - dt);
        g_forceDisplay = std::max(0.f, g_forceDisplay - dt * 0.35f);  // 缓慢回落

        // ---- 更新窗口标题（显示实时力度、命中次数、操作说明） ----
        {
            std::ostringstream oss;
            oss << std::fixed << std::setprecision(0);
            oss << "3D Punching Bag"
                << "  |  Force: " << (g_lastForce * 100.f) << "%"
                << "  |  Hits: "  << g_hitCount
                << "  |  Swipe mouse fast to punch  "
                << "  [R=Reset]  [ESC=Quit]";
            glfwSetWindowTitle(g_win, oss.str().c_str());
        }

        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        // ---- 构建相机矩阵 ----
        // 命中后给相机一个轻微随机抖动（随时间衰减），幅度小不影响观察
        glm::vec3 camPos = g_camPos;
        if (g_shakeTime > 0.f) {
            std::uniform_real_distribution<float> j(-1.f, 1.f);
            float k = (g_shakeTime / SHAKE_DUR) * g_shakeMag;  // 越接近结束抖动越弱
            camPos += glm::vec3(j(g_rng), j(g_rng), j(g_rng)) * k;
        }
        glm::mat4 view = glm::lookAt(camPos, g_camTgt, {0.f, 1.f, 0.f});
        glm::mat4 proj = glm::perspective(glm::radians(45.f),
                                          (float)g_width / g_height, 0.1f, 100.f);

        // 把含震动的实际相机位置 camPos 传入，保证镜面光照(uViewPos)正确
        setSceneUniforms(view, proj, camPos);

        // ===== [1] 绘制地面 =====
        drawSceneMesh(g_mGnd, glm::mat4(1.f),
                      {0.28f, 0.24f, 0.20f}, 0.40f);

        // ===== [2] 绘制横梁（悬挂支架） =====
        {
            // 横梁水平放置在悬挂点正上方
            glm::mat4 m = glm::translate(glm::mat4(1.f),
                                          g_bag.pivot + glm::vec3(0.f, 0.1f, 0.f));
            // 将圆柱旋转 90° 使其横置
            m = glm::rotate(m, glm::radians(90.f), glm::vec3(0.f, 0.f, 1.f));
            drawSceneMesh(g_mBeam, m, {0.45f, 0.42f, 0.40f}, 0.35f);
        }

        // ===== 计算沙袋当前状态 =====
        glm::vec3 bagPos = g_bag.center();

        // 摆臂方向（从悬挂点指向沙袋重心，向下）
        glm::vec3 armDir   = glm::normalize(bagPos - g_bag.pivot);
        glm::vec3 worldUp  = {0.f, 1.f, 0.f};
        // 沙袋倾斜矩阵：将局部 Y 轴旋转到沿摆臂方向
        glm::mat4 tiltMat = glm::mat4(1.f);
        {
            // -armDir 指向"上"（沙袋顶部朝向悬挂点）
            glm::vec3 rotAx = glm::cross(worldUp, -armDir);
            float     rotAg = acosf(std::clamp(glm::dot(worldUp, -armDir), -1.f, 1.f));
            if (glm::length(rotAx) > 1e-4f)
                tiltMat = glm::rotate(glm::mat4(1.f), rotAg, glm::normalize(rotAx));
        }

        // ===== [3] 绘制沙袋 =====
        {
            // 命中闪光：深红 → 金黄，持续约 0.3 秒
            float flash = std::min(g_flashTime * 2.f, 1.f);
            glm::vec3 color = glm::mix(
                glm::vec3(0.70f, 0.20f, 0.08f),  // 深红（正常色）
                glm::vec3(1.00f, 0.82f, 0.22f),  // 金黄（命中闪光）
                flash);

            glm::mat4 m = glm::translate(glm::mat4(1.f), bagPos);
            m = m * tiltMat;  // 沿摆臂方向倾斜
            m = glm::rotate(m, g_bag.spinAngle, glm::vec3(0.f, 1.f, 0.f));  // 自旋
            drawSceneMesh(g_mBag, m, color, 0.18f);
        }

        // ===== [4] 绘制顶部金属环 =====
        {
            // 顶环位置 = 沙袋顶面中心
            glm::vec3 ringPos = bagPos + (-armDir) * (BAG_H * 0.5f);
            glm::mat4 m = glm::translate(glm::mat4(1.f), ringPos);
            m = m * tiltMat;
            drawSceneMesh(g_mRing, m, {0.65f, 0.52f, 0.22f}, 0.35f);
        }

        // ===== [5] 绘制链条 =====
        {
            // 链条从悬挂点到沙袋顶部
            glm::vec3 bagTop  = bagPos + (-armDir) * (BAG_H * 0.5f);
            glm::vec3 chainV  = bagTop - g_bag.pivot;
            float chainLen    = glm::length(chainV);

            // 链条中点位置
            glm::mat4 m = glm::translate(glm::mat4(1.f),
                                          (g_bag.pivot + bagTop) * 0.5f);
            // 将圆柱 Y 轴对齐链条方向
            glm::vec3 cDir = glm::normalize(chainV);  // 从上到下
            glm::vec3 rAx  = glm::cross(worldUp, cDir);
            float     rAg  = acosf(std::clamp(glm::dot(worldUp, cDir), -1.f, 1.f));
            if (glm::length(rAx) > 1e-4f)
                m = glm::rotate(m, rAg, glm::normalize(rAx));
            // Y 方向缩放到实际链条长度
            m = glm::scale(m, {1.f, chainLen, 1.f});
            drawSceneMesh(g_mChain, m, {0.55f, 0.45f, 0.28f}, 0.40f);
        }

        // ===== [6] 绘制粒子系统 =====
        if (!g_particles.empty()) {
            // 收集粒子顶点数据（位置 xyz + 颜色 rgba）
            std::vector<float> pd;
            pd.reserve(g_particles.size() * 7);
            for (const auto& p : g_particles) {
                float alpha = p.life / p.maxLife;  // 透明度随寿命线性衰减
                pd.insert(pd.end(), {
                    p.pos.x, p.pos.y, p.pos.z,
                    p.color.r, p.color.g, p.color.b, alpha
                });
            }

            // 更新 VBO（动态写入，避免重新分配）
            glBindBuffer(GL_ARRAY_BUFFER, g_partVBO);
            glBufferSubData(GL_ARRAY_BUFFER, 0,
                            (GLsizeiptr)(pd.size() * sizeof(float)), pd.data());

            glUseProgram(g_progPart);
            uMat4 (g_progPart, "uMVP",   proj * view);
            uFloat(g_progPart, "uScale", 1.3f);

            glDepthMask(GL_FALSE);  // 粒子不写入深度缓冲（防止互相遮挡）
            glBindVertexArray(g_partVAO);
            glDrawArrays(GL_POINTS, 0, (GLsizei)g_particles.size());
            glDepthMask(GL_TRUE);
        }

        // ===== [7] 绘制命中冲击环（面向相机的半透明圆环，逐渐放大并消失） =====
        if (!g_rings.empty()) {
            glUseProgram(g_progMain);
            // 中途用过粒子着色器，这里重设主着色器的相机/投影 uniform
            uMat4(g_progMain, "uView",     view);
            uMat4(g_progMain, "uProj",     proj);
            uVec3(g_progMain, "uLightPos", g_lightPos);
            uVec3(g_progMain, "uViewPos",  camPos);

            glDepthMask(GL_FALSE);
            for (const auto& r : g_rings) {
                float t     = r.age / r.maxAge;                 // 0 → 1
                float scl   = (0.12f + t * 0.85f) * (0.5f + r.force);  // 逐渐放大
                float alpha = (1.f - t) * 0.6f;                 // 逐渐消失

                // billboard：构造让圆环正对相机的旋转基
                glm::vec3 fwd = glm::normalize(camPos - r.pos);
                glm::vec3 rgt = glm::normalize(glm::cross(glm::vec3(0,1,0), fwd));
                glm::vec3 up  = glm::cross(fwd, rgt);
                glm::mat4 m(1.f);
                m[0] = glm::vec4(rgt, 0.f);
                m[1] = glm::vec4(up,  0.f);
                m[2] = glm::vec4(fwd, 0.f);
                m[3] = glm::vec4(r.pos, 1.f);
                m = glm::scale(m, glm::vec3(scl));
                // uAmbient=1 → 圆环呈纯亮色不受光照明暗影响
                drawSceneMesh(g_mRingFlat, m, glm::vec3(1.f, 0.9f, 0.4f), 1.0f, alpha);
            }
            glDepthMask(GL_TRUE);
        }

        // ===== [8] 绘制 HUD 力度条（屏幕左侧竖直矩形，正交投影） =====
        {
            glUseProgram(g_progMain);
            // 切换到正交投影 + 单位视图，按像素坐标绘制 2D 元素
            glm::mat4 ortho = glm::ortho(0.f, (float)g_width,
                                         0.f, (float)g_height, -1.f, 1.f);
            uMat4(g_progMain, "uProj", ortho);
            uMat4(g_progMain, "uView", glm::mat4(1.f));
            glDisable(GL_DEPTH_TEST);  // HUD 始终绘制在最上层

            const float bx = 40.f,  by = 120.f;   // 力度条左下角（像素）
            const float bw = 34.f,  bh = 320.f;   // 力度条尺寸

            // 背景槽（深色半透明）
            glm::mat4 mb = glm::translate(glm::mat4(1.f), glm::vec3(bx, by, 0.f));
            mb = glm::scale(mb, glm::vec3(bw, bh, 1.f));
            drawSceneMesh(g_mQuad, mb, glm::vec3(0.12f, 0.12f, 0.15f), 1.0f, 0.7f);

            // 填充条（高度按力度，颜色 绿→黄→红）
            float f = std::clamp(g_forceDisplay, 0.f, 1.f);
            glm::vec3 lo(0.2f, 0.9f, 0.3f), mid(0.95f, 0.85f, 0.2f), hi(1.f, 0.25f, 0.15f);
            glm::vec3 col = (f < 0.5f) ? glm::mix(lo,  mid, f * 2.f)
                                       : glm::mix(mid, hi, (f - 0.5f) * 2.f);
            glm::mat4 mf = glm::translate(glm::mat4(1.f), glm::vec3(bx, by, 0.f));
            mf = glm::scale(mf, glm::vec3(bw, bh * f, 1.f));
            drawSceneMesh(g_mQuad, mf, col, 1.0f, 0.95f);

            glEnable(GL_DEPTH_TEST);
        }

        glfwSwapBuffers(g_win);
    }

    // ---- 释放 GPU 资源 ----
    for (auto* mo : {&g_mBag, &g_mGnd, &g_mChain, &g_mRing, &g_mBeam,
                     &g_mQuad, &g_mRingFlat}) {
        glDeleteVertexArrays(1, &mo->vao);
        glDeleteBuffers(1, &mo->vbo);
    }
    glDeleteVertexArrays(1, &g_partVAO);
    glDeleteBuffers(1, &g_partVBO);
    glDeleteProgram(g_progMain);
    glDeleteProgram(g_progPart);

    glfwDestroyWindow(g_win);
    glfwTerminate();
    return 0;
}
