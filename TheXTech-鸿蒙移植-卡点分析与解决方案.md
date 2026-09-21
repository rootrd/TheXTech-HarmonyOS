# TheXTech 鸿蒙移植 · 卡点分析与解决方案

> 生成时间：2026-09-21
> 目标：把 TheXTech（SMBX 引擎 C++ 重写版）移植到 HarmonyOS NEXT（aarch64），产出可安装可测试的 HAP。
> 当前阶段：**共享库 + NAPI 桥接层已完成，上屏方案（SDL2 offscreen 注入）已在真机验证通过，阻塞点是 C++ 层符号拼写不一致。**

---

## 1. 结论速览（TL;DR）

| # | 卡点 | 状态 | 性质 | 解法工作量 |
|---|------|------|------|-----------|
| 1 | **符号名拼写不一致** `thetech_main`(源码) vs `thextech_main`(NAPI层) | 🔴 阻塞 | 代码 bug | 极小（改 5 处字符串） |
| 2 | **音频缺失**（SDL2 只有 DUMMY 驱动） | 🟡 功能缺失 | 需重编 SDL2 | 中（复刻 OHOS audio 驱动） |
| 3 | **游戏资源缺失**（SMBX 1.3 资源包） | 🟡 后续 | 资源准备 | 低（下载/解压） |
| 4 | SDL3 是否引入 | ⚪ 不切换 | 决策 | 不切换（见 §4） |

**一句话：现在距离游戏上屏只差一个字符拼写 bug（`thetech` 少写/多写一个 `x`）。修复后即可重新构建 HAP 上机。**

---

## 2. 当前状态盘点（已实测核实）

### 2.1 已完成并验证 ✅

- 交叉编译产出 `libthextech.so`（15.3MB，aarch64），三份副本 hash 一致 `54c7e7d1d4aa6db32ab1c1f0d780d6e4`，已同步进 HAP。
- `thetech_main` 已正确导出到动态符号表 `.dynsym`（`--export-dynamic` 生效，动态导出符号共 10601 个，不再只有单个符号）。
  - 证据：`llvm-nm -D libthextech.so | grep thetech_main` → `00000000004b1af8 T thetech_main`
- SDL2 offscreen 注入已真机验证：`surfaceId` 获取、`initRender OK`、`native window` 注入、`SDL_Init(video+events)` 均通过。
- NAPI 桥接层完整：`initRender / destroyRender / injectTouch / injectKey / injectGamepadButton / injectGamepadAxis / getFps / prepareGameEnv / setGameRoot` 九个接口齐备。
- SDL2 静态库内含 `SDL_OHOS_SetNativeWindow` / `SDL_OHOS_SetNativeWindowWithSize` 注入点（patch 已打进 libSDL2.a）。

### 2.2 构建链路

- 交叉编译：`E:\TheXTechOH\build-ohos\output\lib\libthextech.so`
- HAP native 工程：`E:\TheXTechOH\hap\entry\src\main\cpp\`（`thextech_main.cpp` + `CMakeLists.txt`）
- 打包：`E:\TheXTechOH\build-hap.cmd`（已内置 JAVA_HOME，规避环境变量传递链路坑）

---

## 3. 卡点精确定位

### 🔴 卡点 1（阻塞）：入口函数名拼写不一致

**现象**：TheXTech 源码的入口函数名与 NAPI 层声明的函数名差一个字母，导致编译时链接 `undefined symbol`（或早期 dlopen 方案下 `dlsym` 找不到符号）。

**证据链**：

1. 源码真名（不带 x）——`E:\TheXTechOH\TheXTech\src\main.cpp:260-262`：
   ```cpp
   extern "C"
   __attribute__((visibility("default")))
   int thetech_main(int argc, char**argv)
   ```
2. `.so` 实际导出（不带 x）——`llvm-nm -D` 显示 `thetech_main`。
3. NAPI 层调用（带 x）——`E:\TheXTechOH\hap\entry\src\main\cpp\thextech_main.cpp`：
   - 第 32 行：`extern "C" int thextech_main(int argc, char**argv);`
   - 第 193 行：`LOGI("calling thextech_main directly ...");`
   - 第 215 行：`LOGI("calling thextech_main argc=...");`
   - 第 216 行：`int retval = thextech_main(argc, argv);`
   - 第 217 行：`LOGI("thextech_main returned %d", retval);`

**根因**：历史上符号导出与 dlopen 问题反复折腾时，函数名被误写为 `thextech_main`（多一个 `x`），而 C++ 是大小写/拼写敏感强类型，两个名字是两个完全不同的符号。

**修复**：统一为源码真名 `thetech_main`（不带 x）。具体做法见 §5.1。

> ⚠️ 历史遗留证据：`hap\.hvigor\outputs\build-logs\build.log` 里记录了一次更早的失败构建，当时 `thextech_main.cpp` 里 `typedef int (*thextech_main_fn)(...)` 被改了一半成 `thetech_main_fn`，导致 `unknown type name 'thetech_main_fn'` 编译错误——说明这个拼写问题已经反复出现过，需要一次性定死。

---

### 🟡 卡点 2：音频缺失（当前用 DUMMY 占位，游戏无声）

**现象**：当前 `libSDL2.a` 编译时只启用了 `DUMMYAUDIO` 驱动，没有 OHOS 音频后端。

**证据**：`llvm-nm libSDL2.a | grep bootstrap` 只有 `DUMMYAUDIO_bootstrap`，无 `OHOSAUDIO`/`ALSA`/`PULSE` 等真正音频驱动。

**当前缓解**：`thextech_main.cpp` 中
- `SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS)`（跳过 AUDIO）
- `PrepareGameEnv` 设 `SDL_setenv("SDL_AUDIODRIVER", "dummy", 1)`

**影响**：渲染/输入不受影响，但没有声音。作为「先跑通画面」的阶段性方案可接受。

**根治方向**（三选一，见 §5.3）：
1. 重新编译 SDL2，加入 OHOS audio 驱动（`OH_Audio` Kit），forward-port 华为官方 `src/audio/ohos`（基于 2.0.12）。
2. 复刻 love-hos 的音频思路（OpenAL-soft → OH_Audio）。
3. 短期继续用 dummy，后续再补。

---

### 🟡 卡点 3：游戏资源缺失

**现象**：TheXTech 仓库无自带游戏资源，离线启动时报 UI 资源文件加载失败。

**影响**：引擎生命周期/渲染/音频基础设施已全部验证，但完整游戏逻辑需 SMBX 1.3 资源包（graphics/worlds/music/sounds/battle）。

**解决**：下载 TheXTech 官方 release 的 ready-to-use 资源包，或从 SMBX 1.3 官方安装包提取，经 `DocumentViewPicker` 导入沙盒（`setGameRoot` 指向）。

---

## 4. SDL3 对项目的帮助分析（结论：不切换）

### 4.1 SDL3 现状

- SDL 3.x 已正式发布（SDL 3.2.x 为当前稳定版），API 相对 SDL2 有重大重构（`SDL_Init`→`SDL_InitSubSystem`、窗口创建走 `properties` 体系、audio 走 `SDL_AudioStream`、新增 `SDL_GPU` 现代渲染 API）。
- TheXTech 官方主线**仍是 SDL2**（2.30.x），上游未完成 SDL3 迁移。
- SDL3 **没有 HarmonyOS 官方驱动**；华为/社区的 OHOS 移植（video/core/audio）全部基于 SDL2 2.0.12，forward-port 到 SDL3 仍需自己打 patch。

### 4.2 为什么当前不切换

| 维度 | 评估 |
|------|------|
| 收益 | 低。当前 SDL2 offscreen 注入已真机验证通过（渲染 OK），SDL3 能给的增量（更干净的 native window 注入、新 audio 抽象）都不属于当前阻塞点。 |
| 成本 | 高。需 (1) 把整个 TheXTech 从 SDL2→SDL3 API 大改；(2) 重新移植/打 OHOS patch；(3) 重编全依赖链（18 个 submodule 的 SDL 相关部分）。 |
| 阻塞关系 | 无关。卡点 1（符号名）、卡点 2（音频驱动）、卡点 3（资源）与 SDL 版本无关。 |

### 4.3 SDL3 的潜在增量价值（未来再评估）

若后续 SDL2 offscreen 暴露以下问题，再考虑 SDL3：
1. **多窗口 / resize 处理**：SDL3 的 `SDL_CreateWindowWithProperties` + 自定义 `SDL_PROP_WINDOW_CREATE_*` 属性，注入 native window 比 SDL2 的 `SDL_OHOS_SetNativeWindow*` 更规范（但仍需自定 OHOS property key）。
2. **音频后端**：SDL3 的 `SDL_AudioStream` 抽象更清晰，接 OH_Audio 时回调形态更友好。
3. **性能/渲染**：`SDL_GPU` 现代 API（但 TheXTech 走 GLES，当前用不上）。

**结论：当前阶段坚持 SDL2 2.30.12 + offscreen 注入，不引入 SDL3。把精力集中在修复符号名、补音频、备资源上。**

---

## 5. 详细解决方案（分步）

### 5.1 步骤 A —— 修复符号名（阻塞，最高优先）

改 `E:\TheXTechOH\hap\entry\src\main\cpp\thextech_main.cpp`，把 NAPI 层 5 处 `thextech_main` 统一改为 `thetech_main`：

```diff
- extern "C" int thextech_main(int argc, char**argv);
+ extern "C" int thetech_main(int argc, char**argv);

...
-     LOGI("calling thextech_main directly (linked at compile time)");
+     LOGI("calling thetech_main directly (linked at compile time)");
...
-     LOGI("calling thextech_main argc=%{public}d", argc);
-     int retval = thextech_main(argc, argv);
-     LOGI("thextech_main returned %{public}d", retval);
+     LOGI("calling thetech_main argc=%{public}d", argc);
+     int retval = thetech_main(argc, argv);
+     LOGI("thetech_main returned %{public}d", retval);
```

> 注意：源码文件 `thextech_main.cpp` 的**文件名**保留不动（`CMakeLists.txt:12` 引用它），只改里面的**符号名**；只需链接时能对得上 `.so` 里的真名 `thetech_main`。

### 5.2 步骤 B —— 重新构建 HAP 并上机

```powershell
# 1) 重新构建（build-hap.cmd 已内置 JAVA_HOME，规避环境变量传递坑）
cmd //c "E:\TheXTechOH\build-hap.cmd"

# 2) 安装到真机
hdc install -r hap\entry\build\default\outputs\default\entry-default-signed.hap

# 3) 启动
hdc shell aa start -a EntryAbility -b <bundleName>

# 4) 看日志（应看到 thetech_main 被调用、游戏线程启动）
hdc shell hilog | grep -i "TheXTechHOS\|thetech"
```

预期日志序列（修复后）：
```
initRender OK ...         // surfaceId 到位
SDL_Init OK (video+events)
calling thetech_main argc=...
thetech_main returned ...
```
若仍报 `undefined symbol: thextech_main` 或运行期崩，说明 .so 未同步，需重跑 §2.2 交叉编译并用 `llvm-nm -D` 复核导出名。

### 5.3 步骤 C —— 补音频（可选，分阶段）

**短期**：维持 `SDL_AUDIODRIVER=dummy`，先交付「有画面无声」版本。

**中期（推荐）**：重新编译 SDL2，加入 OH_Audio 音频驱动：
1. 参考华为官方 SDL2 2.0.12 的 `src/audio/ohos/`（位于 `E:\StardewValley-HOS\third_party\SDL2\src\audio\ohos\`），forward-port 到 2.30.12 的 audio 抽象层。
2. 编译时启用 `-DSDL_AUDIO=ON`，确认 `nnm libSDL2.a` 出现 `OHOSAUDIO_bootstrap`。
3. `PrepareGameEnv` 里把 `SDL_AUDIODRIVER` 从 `dummy` 改为 `ohos`。
4. `SDL_Init` 恢复 `SDL_INIT_AUDIO`。

**备选**：love-hos 的 OpenAL-soft → OH_Audio 链路，可复刻其对接方式到 MixerX 后端。

### 5.4 步骤 D —— 备资源（用于完整游戏逻辑测试）

1. 下载 SMBX 1.3 资源包（TheXTech 官方 release 的 ready-to-use 包，或 SMBX 1.3 安装包提取 graphics/worlds/music/sounds/battle）。
2. 放入 HAP `resources/rawfile/` 或经 `DocumentViewPicker` 导入沙盒。
3. `setGameRoot(gameRoot, gameHome)` 指向资源目录，再 `initRender`。

---

## 6. 验证清单

- [ ] `llvm-nm -D libthextech.so | grep thetech_main` 有输出（导出正常）
- [ ] `thextech_main.cpp` 中所有符号引用统一为 `thetech_main`
- [ ] HAP 构建无 `undefined symbol: thextech_main`
- [ ] 真机日志出现 `thetech_main returned ...`
- [ ] 画面渲染正常（surface 上屏）
- [ ] （可选）音频启用后声音正常
- [ ] （可选）导入资源后跑通一个关卡

---

## 7. 关键文件索引

| 文件 | 作用 | 状态 |
|------|------|------|
| `TheXTech\src\main.cpp:262` | 引擎入口 `thetech_main`（真名） | 已导出 ✅ |
| `hap\entry\src\main\cpp\thextech_main.cpp` | NAPI 桥接层（符号名需改） | 🔴 待改 |
| `hap\entry\src\main\cpp\CMakeLists.txt` | native 构建配置 | 正常 |
| `hap\entry\src\main\cpp\libthextech.so` | 交叉编译共享库副本 | 已同步 ✅ |
| `hap\entry\libs\arm64-v8a\libthextech.so` | 打包用共享库 | 已同步 ✅ |
| `build-ohos\output\lib\libthextech.so` | 交叉编译原始产物 | 一致 ✅ |
| `build-hap.cmd` | HAP 打包脚本（内置 JAVA_HOME） | 正常 |
| `hap\entry\src\main\cpp\libSDL2.a` | SDL2 offscreen（仅 DUMMY 音频） | 🟡 待补音频 |