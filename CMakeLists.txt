cmake_minimum_required(VERSION 3.16)

# ----------------------------------------------------------------
# vcpkg 工具链自动检测（必须在 project() 之前生效）
# 若用户设置了环境变量 VCPKG_ROOT 且未手动指定工具链，则自动启用 vcpkg。
# 也可在命令行手动传入:
#   -DCMAKE_TOOLCHAIN_FILE=<vcpkg>/scripts/buildsystems/vcpkg.cmake
# ----------------------------------------------------------------
if(NOT DEFINED CMAKE_TOOLCHAIN_FILE AND DEFINED ENV{VCPKG_ROOT})
    set(CMAKE_TOOLCHAIN_FILE "$ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake"
        CACHE STRING "vcpkg toolchain file")
    message(STATUS "已自动启用 vcpkg 工具链: ${CMAKE_TOOLCHAIN_FILE}")
endif()

project(PunchingBag LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 单配置生成器（Makefile/Ninja）下默认 Release，方便课程演示
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# ----------------------------------------------------------------
# macOS Homebrew 路径自动检测
# Apple Silicon (M1/M2/M3): /opt/homebrew
# Intel Mac:                 /usr/local
# ----------------------------------------------------------------
if(APPLE)
    if(EXISTS "/opt/homebrew")
        list(PREPEND CMAKE_PREFIX_PATH "/opt/homebrew")
    endif()
    if(EXISTS "/usr/local/lib/cmake")
        list(APPEND CMAKE_PREFIX_PATH "/usr/local")
    endif()
endif()

# ----------------------------------------------------------------
# 查找依赖库
#   Windows (vcpkg): vcpkg install glfw3 glm glew
#   macOS  (brew)  : brew install glfw glm
#   Linux  (apt)   : sudo apt install libglfw3-dev libglew-dev libglm-dev
# ----------------------------------------------------------------
find_package(OpenGL REQUIRED)

# GLFW：vcpkg / Homebrew / Linux 均提供 CONFIG 包（目标名为 glfw）
find_package(glfw3 CONFIG REQUIRED)

# GLEW：仅 Windows/Linux 需要（macOS 用系统 OpenGL 框架，无需 GLEW）
# vcpkg 与 Linux 均提供 GLEW::GLEW 导入目标
if(NOT APPLE)
    find_package(GLEW REQUIRED)
endif()

# GLM：纯头文件库，优先用 CONFIG 目标 glm::glm；找不到则退回头文件搜索
find_package(glm CONFIG QUIET)
if(NOT TARGET glm::glm)
    find_path(GLM_INCLUDE_DIR glm/glm.hpp
        PATHS
            /opt/homebrew/include
            /usr/local/include
            /usr/include)
    if(NOT GLM_INCLUDE_DIR)
        message(FATAL_ERROR
            "未找到 GLM！\n"
            "  Windows: vcpkg install glm\n"
            "  macOS  : brew install glm\n"
            "  Linux  : sudo apt install libglm-dev")
    endif()
    message(STATUS "GLM headers found at: ${GLM_INCLUDE_DIR}")
endif()

# ----------------------------------------------------------------
# 可执行目标
# ----------------------------------------------------------------
add_executable(punching_bag src/main.cpp)

# macOS 抑制 OpenGL 弃用警告
target_compile_definitions(punching_bag PRIVATE
    $<$<PLATFORM_ID:Darwin>:GL_SILENCE_DEPRECATION>
)

# MSVC：源码含中文注释/字符串，强制以 UTF-8 解析源文件，避免编译错误或乱码
if(MSVC)
    target_compile_options(punching_bag PRIVATE /utf-8)
endif()

# 包含目录
target_include_directories(punching_bag PRIVATE ${OPENGL_INCLUDE_DIR})
if(DEFINED GLM_INCLUDE_DIR)
    target_include_directories(punching_bag PRIVATE ${GLM_INCLUDE_DIR})
endif()

# ----------------------------------------------------------------
# 链接库
# ----------------------------------------------------------------
target_link_libraries(punching_bag PRIVATE
    OpenGL::GL
    glfw
)
# Windows/Linux 额外链接 GLEW
if(NOT APPLE AND TARGET GLEW::GLEW)
    target_link_libraries(punching_bag PRIVATE GLEW::GLEW)
endif()
if(TARGET glm::glm)
    target_link_libraries(punching_bag PRIVATE glm::glm)
endif()

# 输出目录（方便直接运行）
set_target_properties(punching_bag PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}
)
