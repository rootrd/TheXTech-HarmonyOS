# TheXTech 鸿蒙移植 · 转交文档

> 生成时间：2026-09-20 | 目标：把 SMBX 引擎 C++ 重写版 TheXTech 移植到鸿蒙（HarmonyOS NEXT，aarch64）
> 一句话结论：**TheXTech 是 SMBX 三个变体里唯一有 Linux + Android 版本的，也是唯一现实可移植到鸿蒙的入口。走「源码交叉编译」路线（同 love2d/Godot），不需要 ELF Loader。**

---

## 1. 选型背景与结论

### 1.1 TheXTech 是什么

SMBX（Super Mario Bros. X）引擎从 VB6 逐行重写为 C++，官方描述为「SMBX 1.3 引擎的直接延续」。GPL-3.0，415 star。

- 引擎：C++ + SDL2 + CMake
- 游戏形态：**编辑器型平台**——吃 SMBX 关卡资源（graphics/worlds/自定义 episode），可加载成千上万玩家自制关卡
- 仓库：<https://github.com/TheXTech/TheXTech>

### 1.2 三个 SMBX 变体的平台支持（已调查完毕）

| 版本 | Linux | Android | 结论 |
|------|:-----:|:-------:|------|
| **TheXTech**（1.3 重写） | ✅ 原生（debian 打包） | ✅ 完整 gradle 工程 | **唯一可移植，就是它** |
| **SMBX2**（LunaLua，`WohlSoft/LunaLua`） | ❌ | ❌ | Windows-only，本质是 DLL 注入（`dll-injection`），无载体 |
| **SMBX-38A** | ❌ | ❌ | 中国社区闭源分支，无源码 |

- SMBX2 核心仓库 `WohlSoft/LunaLua`（C++ 37 star，GPL-3.0），主题标签 `dll-injection`——把 Lua 扩展 DLL 注入原版 Windows SMBX.exe，离开 Windows EXE 无法运行。
- TheXTech 内置 `LunaScript`（LunaDLL Autocode 解释器），所以 1.3 生态里**带脚本的关卡也能跑**。
- **生态提醒**：TheXTech 只能跑 SMBX 1.3 冻结存量（够玩很久，但不再新增）；活跃新内容在 SMBX2（LunaLua），暂不可达。练手目标先跑通 1.3 存量即可。

### 1.3 技术路线判断

**走源码交叉编译路线**（与 love2d、Godot 4.6.1 同类），**不使用**自研 ELF Loader。理由：TheXTech 源码完整，C++ + SDL2 + CMake 直编到 aarch64-linux-ohos 即可。

---

## 2. 技术栈与依赖清单（完整 submodule，已核实）

来源 `.gitmodules`，共 18 个：

| 依赖 | 用途 | 鸿蒙适配难度 | 备注 |
|------|------|:-----------:|------|
| **SDL2** | 窗口/输入/事件/渲染上下文 | ✅ 低 | 已有配方（love2d 用 SDL2 2.30.12 + offscreen driver） |
| **freetype** | 字体渲染 | ✅ 低 | 已有配方（love2d 用 freetype 2.13.3） |
| **lz4** | 压缩 | ✅ 低 | 纯 C |
| **IniProcessor** | INI 解析 | ✅ 低 | 纯 C++ |
| **DirManager** | 目录管理 | ✅ 低 | 纯 C++ |
| **PGE_File_Formats** | SMBX 文件格式 | ✅ 低 | 纯 C++ |
| **FileMapper** | 内存文件映射 | ⚠️ 中 | 读文件 mmap 不涉 exec，理论 OK，需实测 |
| **FreeImageLite** (`libFreeImage`) | 图像加载（PNG/GIF 等） | ⚠️ 中 | 大量格式，opencv 类交叉编译经验可参考 |
| **SDL-Mixer-X**（MixerX） | 音频混合 | ⚠️⚠️ 高 | SDL_mixer 继承者，音频后端需接鸿蒙 |
| **AudioCodecs** | 音频解码集合 | ⚠️⚠️ 高 | 与 MixerX 配套，大量编解码器 |
| **LuaJIT**（WohlSoft fork v2.1） | Lua 脚本 JIT | ⚠️⚠️ 高 | JIT 需 W^X；但匿名 mmap RWX 已验证可行 |
| **luabind**（deboostified） | Lua C++ 绑定 | ⚠️ 中 | 依赖 LuaJIT |
| **luau**（TheXTech fork） | Roblox Lua（新脚本） | ⚠️ 中 | 较新 VM，交叉编译需评估 |
| **glew-cmake** | OpenGL 函数加载 | ⚠️⚠️ 高 | 鸿蒙无桌面 GL，需换 GLES 加载器 |
| **angle-shader-translator** | GLSL→GLSL ES 翻译 | ✅ 利好 | 已内置，鸿蒙 GLES 直接受益 |
| thextech-discord-rpc | Discord 集成 | 砍掉 | 鸿蒙无 Discord |
| mbediso | ISO 挂载 | 可选砍 | 海湾关卡包用不到 |
| SDL_net | 网络 | 可选砍 | 单人本地玩用不到 |

**关键利好**：`angle-shader-translator` 的存在证明 TheXTech 作者已为 GLES 移动端（Android）预留了 shader 翻译路径，鸿蒙 GLES3 可直接复用。

---

## 3. 核心难点预判（按风险排序）

1. **渲染后端**（最难）：TheXTech 用 SDL2 + OpenGL + GLEW。鸿蒙只有 GLES3 + EGL（无桌面 GL）。解法：复用 love2d 的「SDL2 offscreen driver + EGL/GLES」桥接，`angle-shader-translator` 做 shader 翻译。
2. **音频栈**：MixerX + AudioCodecs 是 SDL_mixer 的替代，后端需接鸿蒙音频（OpenSLES，同 love2d 的 OpenAL-soft→OpenSLES 思路）。
3. **LuaJIT**：JIT 需要可写可执行内存。鸿蒙手机 SELinux 禁 file+PROT_EXEC，但匿名 mmap(RWX) + 执行已验证可行（`dlopen_test` 结论）。
4. **CMake 交叉编译**：18 个 submodule 逐个编，`ohos.toolchain.cmake` 需处理各库的特殊选项。

---

## 4. 移植步骤（详细）

### 阶段 0：桌面构建验证（先跑通原版）

目的：确认源码 + 资源能正常编译运行，建立基线。

```bash
git clone --recurse-submodules https://github.com/TheXTech/TheXTech.git
cd TheXTech
mkdir build && cd build
cmake .. -G Ninja -DCMAKE_BUILD_TYPE=Release
ninja
# 运行：需 SMBX 资源（graphics/worlds/music/sounds），放 thextech.ini 指定
```

> 资源获取：SMBX 1.3 资源包（graphics、music、sounds、worlds）需单独下载。TheXTech 官方 releases 里有 ready-to-use 包；或从 SMBX 1.3 官方安装包提取。

### 阶段 1：鸿蒙工具链准备

复用现有环境（AGENTS.md 已有）：

```powershell
# 交叉编译工具链（已有）
C:\ohos-sdk\native\build-tools\cmake\bin\cmake.exe
C:\ohos-sdk\native\build-tools\cmake\bin\ninja.exe
C:\ohos-sdk\native\llvm\bin\   # clang / llvm-ar / llvm-nm
# 工具链文件（已有）
F:\portmaster\deps\ohos.toolchain.cmake
```

### 阶段 2：交叉编译依赖库（逐个，顺序）

先编「已有配方」的，再啃难的：

1. **SDL2**（✅ 直接复用）：`deps/SDL2-2.30.12` 已有鸿蒙版本，含 offscreen driver 注入（`SDL_OHOS_SetNativeWindowWithSize`）。TheXTech 用 SDL OpenGL context，需确认 offscreen driver 支持 GLES context 创建。
2. **freetype**（✅ 复用）：`deps/freetype-2.13.3` 已有。
3. **纯 C/C++ 轻量库**（lz4/IniProcessor/DirManager/PGE_File_Formats/FileMapper）：直接交叉编译，难度低。
4. **FreeImageLite**：configure 时禁用不必要格式，编静态库。
5. **AudioCodecs + MixerX**：最难。参考 love2d 音频栈做法，后端指向鸿蒙音频。先评估 MixerX 是否只依赖 SDL2 的 audio 接口（若是，则 SDL2 的 OpenSLES 后端即可接管，工作量骤减）。
6. **LuaJIT + luabind**：LuaJIT 交叉编译到 aarch64（有成熟方案），luabind 依赖它。
7. **luau**：交叉编译评估，必要时临时禁用（TheXTech 的新脚本系统，1.3 老关卡主要走 LunaScript/LuaJIT）。
8. **glew-cmake + angle-shader-translator**：glew 是桌面 GL 加载器，鸿蒙需换成 GLES 加载（或用 TheXTech 已有的 GLES 路径）；angle-shader-translator 直接编。

> 建议：优先让 **SDL2 + 渲染路径** 先跑通（空窗口/单色渲染），音频用无声 stub，先把渲染链打通，再回头补音频和 Lua。

### 阶段 3：CMake 交叉编译主工程

```powershell
cmake -S TheXTech -B build-ohos -G Ninja `
  -DCMAKE_TOOLCHAIN_FILE=F:\portmaster\deps\ohos.toolchain.cmake `
  -DCMAKE_MAKE_PROGRAM=C:\ohos-sdk\native\build-tools\cmake\bin\ninja.exe `
  -DCMAKE_BUILD_TYPE=Release `
  -D部分选项=...   # 禁用 discord-rpc / SDL_net / mbediso / luau（如需要）
```

产出：`thextech` 静态库或可链接对象的 aarch64-linux-ohos 版本。

### 阶段 4：鸿蒙 HAP 集成

参照 `love-hos` 工程结构（可直接复制改造）：

```
thextech-hos/
├── entry/src/main/
│   ├── ets/pages/Index.ets          # 主菜单 + XComponent 渲染窗口
│   ├── ets/service/GamepadManager.ets    # 实体手柄（复用）
│   ├── ets/service/OverlayWindowManager.ets # 触控层独立子窗口（复用）
│   ├── cpp/                          # NAPI 桥接 + TheXTech 主循环
│   └── resources/rawfile/            # SMBX 资源（graphics/worlds/...）
└── build.ps1
```

桥接要点（复用 love-hos 资产）：
- **渲染**：SDL2 offscreen driver 注入 → EGL/GLES context → 帧写入 → OHNativeWindow 上屏。
- **触屏虚拟手柄**：`TouchControls.ets` + `OverlayWindowManager.ets`（zLevel 9000 独立子窗口，手柄连接自动隐藏）。
- **实体手柄**：`game_controller_bridge.cpp`（dlopen `libohgame_controller.z.so`）→ NAPI → SDL GameController 映射（复用）。
- **关卡导入**：`DocumentViewPicker` 选关卡/episode 包 → 复制进 filesDir（复用沙盒导入方案）。

### 阶段 5：资源与内容

- SMBX 1.3 基础资源（graphics/music/sounds/worlds/battle）打包进 rawfile 或首次导入。
- 自定义关卡通过「导入按钮」选 `.lvl`/`.wld`/episode 目录加入。
- 存档/设置走 `SettingsStore`（Preferences）持久化。

### 阶段 6：真机调试

```powershell
hdc install -r entry\build\default\outputs\default\entry-default-signed.hap
hdc shell aa start -a EntryAbility -b <bundleName>
hdc shell snapshot_display -f /data/local/tmp/x.jpeg
hdc file recv /data/local/tmp/x.jpeg x.jpeg
hdc shell hilog -x
```

---

## 5. 可复用资产清单（直接搬）

| 资产 | 位置 | 用途 |
|------|------|------|
| SDL2 鸿蒙配方（offscreen driver） | `love-hos` / `deps/SDL2-2.30.12` | 渲染上下文 + 输入 |
| freetype 鸿蒙配方 | `deps/freetype-2.13.3` | 字体 |
| 触控虚拟手柄 | `TouchControls.ets` | 手机无键盘 |
| 触控层独立子窗口 | `OverlayWindowManager.ets` | XComponent 之上覆盖 |
| 实体手柄桥接 | `game_controller_bridge.cpp` | GamePad Kit → SDL |
| 沙盒文件导入 | `DocumentViewPicker` 流程 | 关卡/episode 导入 |
| 音频后端思路 | OpenAL-soft → OpenSLES | 参考对接 MixerX |
| 交叉编译工具链 | `deps/ohos.toolchain.cmake` | 全依赖编译 |

---

## 6. 快速决策备忘

- **bundleName 建议**：`com.thextech.rootrd`（参照 `com.smb.rootrd`、`com.sm63.rootrd`）。
- **签名**：参考 smb-hos 已有 debug 签名，独立申请不干扰其他项目。
- **GPL-3.0 传染**：TheXTech 及多数 WohlSoft 库为 GPL，闭源分发前需法律评估（当前练手/自用无碍）。
- **必须先验证的生死点**：SDL2 offscreen driver 能否在鸿蒙创建 GLES context 并让 TheXTech 的 OpenGL 渲染经 `angle-shader-translator` 输出上屏。**这一步不通，后面全停。**

---

## 7. 对接知识库

- 沙箱/进程模型：`G:\知识库\鸿蒙移植经验库\01-沙箱与进程模型.md`
- 渲染/GLEL：`G:\知识库\鸿蒙移植经验库\02-渲染与图形栈.md`
- 构建调试：`G:\知识库\鸿蒙移植经验库\04-构建部署调试.md`
- 引擎移植：`G:\知识库\鸿蒙移植经验库\03-游戏引擎移植.md`
- 通用运行时/ELF：`G:\知识库\鸿蒙移植经验库\22-PortMaster通用运行时与自研ELF-Loader.md`