# TheXTech 鸿蒙 NEXT 移植 — 交接文档

> 最后更新：2026-09-21 (第三轮:触控/音频/手柄/旋转全部修复)  
> 项目目录：`E:\TheXTechOH`

---

## 一、项目概况

将 **TheXTech**（Super Mario Bros. X / SMBX 引擎的 C++ 重写版）通过源码交叉编译移植到 **HarmonyOS NEXT (aarch64)**，产出可安装可测试的 HAP 包。

**核心链路：** 源码交叉编译 → 共享库(libthextech.so) → NAPI桥接层 → HAP打包 → 上屏渲染 → 音频输出 → 触控操作

---

## 二、当前状态

### ✅ 已完成

| 里程碑 | 状态 | 说明 |
|--------|------|------|
| 交叉编译 / 共享库 / NAPI / HAP打包 | ✅ | 见第二轮记录 |
| 上屏渲染 / 游戏运行 | ✅ | SDL2 offscreen + EGL 注入 |
| **音频输出 (第三轮修复)** | ✅ | 三重bug修复后数据流打通:PlayDevice→WriteData got=32768 满额 |
| **触控按键 (第三轮修复)** | ✅ | 根因是 keystate 直写,见 §三 |
| **手柄支持 (第三轮新增)** | ✅ | 手柄按键以 KeyEvent 到达页面(2301+),已映射;连接检测隐藏虚拟键 |
| **自动左右横屏旋转 (第三轮新增)** | ✅ | module.json5 + EntryAbility 运行时均为 AUTO_ROTATION_LANDSCAPE |

### ⚠️ 需现场确认

| 项 | 说明 |
|----|------|
| 声音可听性 | PCM 数据流已验证满额,是否有实际声音需用户确认 |
| 旋转 | 需系统控制中心"自动旋转"开关为开;关则锁定当前方向 |

---

## 三、触控无效 — 真正的根因(第三轮实锤,推翻第二轮猜测)

### 3.1 三个叠加的bug(全部已修)

1. **SDL2 双实例**(第二轮猜测,已证实):`libentry.so` 和 `libthextech.so` 各自静态链接 SDL2,各自有独立事件队列/keystate。libentry 的 `SDL_PushEvent` 喂的是游戏不读的队列。
   **修复:** `sdl_core.cpp` 导出 `thetech_push_sdl_event()`,NAPI 层全部事件注入改走它。

2. **SDL_PushEvent 永远不更新键盘状态数组**(关键!第二轮未发现):TheXTech 通过 `SDL_GetKeyboardState()` 轮询输入(keyboard.cpp:175),而 SDL2 的 keystate **只在 `SDL_SendKeyboardKey()`(硬件事件路径)里更新**(SDL_keyboard.c:864),`SDL_PollEvent` 取出事件也不更新。所以即使事件进了正确的队列,游戏依然无响应。
   **修复:** 导出 `thetech_set_key_state(scancode, down)`:直接 const_cast 写引擎实例的 keystate 数组 + 同时推事件。`InjectKey` 改走它。**这是触控/键盘/手柄共用的唯一正确注入路径。**

3. **手柄按键事件到不了页面**(焦点问题):`onKeyEvent` 只派发给焦点组件,根 Stack 原本不可聚焦。
   **修复:** 根 Stack 加 `.focusable(true).defaultFocus(true)`,启动按钮卸载后焦点回落到 Stack。

### 3.2 手柄映射(Index.ets mapKeyCode)

`KEYCODE_BUTTON_A(2301)→Z(29跳跃) B(2302)→X(27奔跑) X(2304)→A(4) Y(2305)→S(22) L1→S R1→A SELECT(2311)→Esc START(2312)→Return`;DPAD(2012-15)键盘手柄共用。摇杆若被系统合成 DPAD 键也能工作。

### 3.3 手柄连接检测(已验证工作)

`inputDevice.on('change')` + `getDeviceList/getDeviceInfo` 查 `sources` 含 `'joystick'` → `gamepadConnected=true` → `TouchControls` 隐藏(卸载时自动 releaseAllInputs)。

### 3.4 原生输入通道(第四/五轮:游戏按原生类型识别输入,不再伪装键盘)

**触摸(原生):** TheXTech 的 TouchScreenController 轮询 `SDL_GetTouchFinger()` 状态(事件注入无效,同 keystate 陷阱)。桥:`thetech_send_touch()` 用 SDL 内部 `SDL_AddTouch(DIRECT)` 首次注册虚拟触摸屏 + `SDL_SendTouch/SendTouchMotion` 驱动状态。引擎在 FINGERDOWN 时重扫描设备并激活自带触摸 UI(素材在 gamedata/graphics/touchscreen/,**无需从安卓包提取**)。ArkTS:根 Stack onTouch(windowX/screenW 归一化;onTouch 是冒泡事件,只挂根,勿同时挂 XComponent 会双发)。旧自绘 TouchControls.ets 已弃用。

**手柄(原生):** SDL 构建原本 `SDL_JOYSTICK:BOOL=OFF` 整个子系统关闭!开启后 cmake 生成 `include-config-release/` 新配置,但 TheXTech/NAPI 用的是 `include/SDL2/SDL_config.h`(**需手动同步,cmake 不会更新它**;同步后必须 `ninja -t clean` 全量重编,否则混合配置链接失败如 `SDL_DUMMY_JoystickDriver undefined`)。桥:`thetech_attach_virtual_gamepad()` 用公开 API `SDL_JoystickAttachVirtual(GAMECONTROLLER, 4轴,16键,1帽)`——**返回值是设备索引不是实例ID**,须 `SDL_JoystickOpen(index)` 取句柄(首版用 FromInstanceID 是错的);attach 自动发 SDL_JOYDEVICEADDED,引擎 ConsumeEvent 后 OpenJoystick("Virtual Controller"),再由 `SDL_JoystickSetVirtualButton/Hat` 喂状态,引擎 `SDL_JoystickGetButton/Axis/Hat` 轮询。ArkTS 手柄键映射:A/B/X/Y/L1/R1/SELECT/START/MODE/THUMBL/THUMBR/L2/R2→按钮0-12,DPAD→hat(up=1,right=2,down=4,left=8),键盘键仍走 injectKey。

**图标:** 蓝蘑菇分层图标已生成(make_icon.py 从官方 logo 抠取,透明背景,1024×1024,前景蘑菇+深navy底,写入 AppScope 与 entry 的 media/)。

### 3.6 触控终局修复(第六轮,已实测正常)

**符号路由真相(本项目最重要的教训):** 引擎运行时实际使用的 SDL 实例是 **libentry 的副本**——libentry.so 与 libthextech.so 都导出公开 SDL 符号,运行时公开符号经 PLT 解析到 libentry(先加载)。因此:
- `thetech_set_key_state` 里调用**公开** `SDL_GetKeyboardState` → libentry 的数组 = 引擎读的数组(所以键盘/触控注入一直有效)
- 虚拟手柄用**公开** `SDL_JoystickAttachVirtual/SetVirtual*` → libentry 实例 = 引擎轮询的实例(所以手柄有效)
- 而 `SDL_AddTouch/SDL_SendTouch` 是 **hidden 内部符号**,在 sdl_core.cpp(libthextech)里调用会静态绑定到 libthextech 自己的 SDL 副本——注册到无人读取的实例!症状:`SDL_AddTouch rc=0` 但 `SDL_GetNumTouchDevices()=0`

**修复:** 触摸桥全部移到 `thextech_main.cpp`(libentry):同样的 extern "C" 声明绑定到 libentry 的 hidden SDL;`TheXTechThreadMain` 在 SDL_Init 成功后立即 `RegisterVirtualTouchscreen()`(早于引擎 ~1s 后的一次性扫描);NAPI `SendTouch` 直接调 libentry 的 SDL_SendTouch。日志验证:`RegisterVirtualTouchscreen rc=0 devices=1` → `Found 1 touch devices` → `scanTouchDevices -> 1 valid` → **首次触摸即激活原生触摸 UI(用户实测正常)**。

其它配套修复:触摸坐标 windowX/windowY 是 **vp 单位**,须乘 `display.getDefaultDisplaySync().densityPixels`(3.5)再除以像素宽;方向键同时镜像到虚拟手柄的帽子键+左摇杆轴(InitAsJoystick 配置两者都绑,双保险,用户实测手柄方向键正常)。

### 3.7 地图打包(第六轮)

rom/ 下新地图已解压并入包:worlds/ 新增 `SuperMarioZeroExtreme`(v1.2.3 纯地图,152M)与 `Mega Luigi -C-`(31M,7z 用 py7zr 解);DEMO 版原本已在。HAP 增至 **340MB**,安装正常。流程:解压 → 拷入 `resfile/gamedata/worlds/` → build-hap.cmd。

### 3.8 用户地图导入+管理(第七轮)

引擎原生支持可写地图根:`-u` 的 filesDir 下 `worlds/smbx/`(asset pack id 来自 gameinfo.ini,AppPathManager::userWorldsRootDir,启动时自动创建并随 episode 列表扫描,标记 editable)。Index.ets 启动页新增:
- **导入地图**:`picker.DocumentViewPicker` 选 .zip → 拷入沙箱 → `zlib.unzipFile` 解压 → 递归找含 .wld/.wldx/.lvl/.lvlx 的目录 → rename 移入 worlds/smbx(重名自动加后缀)。仅支持 zip(7z 不支持,需先转 zip)。
- **管理地图**:列出用户地图,AlertDialog 确认后递归删除(fs.rmdirSync 无递归参数,手写)。
- 注意:ArkTS `zlib.CompressLevel` 无 COMPRESS_LEVEL_DEFAULT,用 NO_COMPRESSION。

### 3.9 启动页壁纸与图标(第七轮)

- 壁纸:`make_wallpaper.py` 用 gamedata 的 background2-2(SMB3 蓝天山丘)平铺 2848×1276 + 暗角 + 官方 logo 蘑菇 30% 透明水印 → `media/launch_bg.jpg`(132KB),ImageFit.Cover 铺满,文字加 textShadow。
- 图标:官方 `resources/icon/thextech_512.png`(本身透明)→ 640/1024 居中前景 + 深navy底背景,写入 AppScope 与 entry。
- **调试注意**:`uitest uiInput click/keyEvent` 注入会真实作用在 UI 上(用户可见的"幽灵点击"),联调时慎用;注入的 touch 不触发应用 onTouch(只有 keyEvent 有效)。

---

## 四、音频无声音 — 三个叠加bug(第三轮全部修复)

`SDL_ohosaudio.c` (F:\portmaster\deps\SDL2-2.30.12\src\audio\ohos\):

1. **`impl->ProvidesOwnCallbackThread = SDL_TRUE`** → SDL_audio.c:1544 不创建音频线程 → `PlayDevice` 永不调用。改 `SDL_FALSE`,SDL_RunAudio 线程驱动 GetDeviceBuf→PlayDevice→WaitDevice。
2. **`mixbuf` 从未分配** → `GetDeviceBuf` 返回 NULL → SDL_RunAudio 走 work_buffer 分支(SDL_audio.c:785-788,只 delay 不 PlayDevice)→ 中间缓冲永远空。修复:OpenDevice 末尾(格式确定后)`SDL_malloc(spec.size)`。
3. **中间缓冲太小** → OHAudio FAST 模式回调一次要 32768 字节,原 `spec.size*4=16384` 只能喂一半(50%静音断续)。修复:缓冲上限至少 65536。

**验证日志:** `PlayDevice #1 size=4096` → `WriteData cb#2 len=32768 got=32768`(满额)。
**注意:** SDL 侧 spec.size 在 OpenDevice 入口前已算好(SDL_audio.c:1301),但驱动内改格式后须重调 `SDL_CalculateAudioSpec`。

---

## 五、关键文件清单

### 5.1 NAPI 桥接层

| 文件 | 说明 |
|------|------|
| `E:\TheXTechOH\hap\entry\src\main\cpp\thextech_main.cpp` | NAPI 层主文件。`InjectKey` 函数注入 `SDL_KEYDOWN`/`SDL_KEYUP`；`InjectTouch` 注入 `SDL_FINGERDOWN`/`SDL_FINGERUP`；`PrepareGameEnv` 设置 `SDL_AUDIODRIVER=ohos` |
| `E:\TheXTechOH\hap\entry\src\main\cpp\CMakeLists.txt` | NAPI 构建配置。链接 `libSDL2.a` + `libthextech.so`，无 `--exclude-libs` |
| `E:\TheXTechOH\hap\entry\src\main\cpp\libSDL2.a` | SDL2 静态库（含 EGL 修复 + OHOS 音频驱动） |
| `E:\TheXTechOH\hap\entry\src\main\cpp\libthextech.so` | 引擎共享库（已用新 `libSDL2.a` 重新编译） |

### 5.2 ArkTS 层

| 文件 | 说明 |
|------|------|
| `E:\TheXTechOH\hap\entry\src\main\ets\pages\Index.ets` | 主页面，XComponent + TouchControls，`.hitTestBehavior(HitTestMode.None)` |
| `E:\TheXTechOH\hap\entry\src\main\ets\components\TouchControls.ets` | 马里奥像素风触控UI。D-Pad十字、A红B黄圆、问号砖块Start。含调试日志 `TC_TOUCH`/`TC_HIT` |
| `E:\TheXTechOH\hap\entry\src\main\ets\entryability\EntryAbility.ets` | 入口Ability，`setWindowLayoutFullScreen(true)` |

### 5.3 SDL2 源码（已修改）

| 文件 | 说明 |
|------|------|
| `F:\portmaster\deps\SDL2-2.30.12\src\audio\ohos\SDL_ohosaudio.c` | OHOS OHAudio 驱动实现 |
| `F:\portmaster\deps\SDL2-2.30.12\src\audio\ohos\SDL_ohosaudio.h` | OHOS 音频驱动头文件（含 `#define _THIS SDL_AudioDevice *_this`） |
| `F:\portmaster\deps\SDL2-2.30.12\src\audio\SDL_audio.c` | 添加 `OHOSAUDIO_bootstrap` 条目 |
| `F:\portmaster\deps\SDL2-2.30.12\src\audio\SDL_sysaudio.h` | 添加 `extern AudioBootStrap OHOSAUDIO_bootstrap;` |
| `F:\portmaster\deps\SDL2-2.30.12\include\SDL_config.h.cmake` | 添加 `SDL_AUDIO_DRIVER_OHOS` |
| `F:\portmaster\deps\SDL2-2.30.12\CMakeLists.txt` | 添加 `SDL_OHOSAUDIO` 选项 |
| `F:\portmaster\deps\SDL2-2.30.12\src\video\offscreen\SDL_offscreenvideo.c` | `OFFSCREEN_VideoInit` 使用注入尺寸，添加 `OFFSCREEN_SetWindowSize` |
| `F:\portmaster\deps\SDL2-2.30.12\src\video\offscreen\SDL_offscreenwindow.c` | 添加 `SDL_OHOS_GetNativeWidth/Height` getter；`OH_NativeWindow_NativeWindowHandleOpt(window, SET_FORMAT, visual_id)` |

### 5.4 构建相关

| 文件 | 说明 |
|------|------|
| `E:\TheXTechOH\build-hap.cmd` | HAP 构建脚本（已内置 JAVA_HOME） |
| `E:\TheXTechOH\build-ohos\` | TheXTech Ninja 构建目录 |
| `F:\portmaster\deps\build-sdl2-ohos\` | SDL2 Ninja 构建目录 |

### 5.5 TheXTech 源码（参考，未修改）

| 文件 | 说明 |
|------|------|
| `E:\TheXTechOH\TheXTech\src\control\keyboard.cpp` | 键盘输入处理。`m_keyboardState = SDL_GetKeyboardState()` (line 834)，`m_keyboardState[key]` 检查按键 (line 175) |
| `E:\TheXTechOH\TheXTech\src\core\sdl\sdl_core.cpp` | `CoreSDL::init()` 包含 `SDL_INIT_AUDIO` |
| `E:\TheXTechOH\TheXTech\src\core\sdl\events_sdl.cpp` | `EventsSDL::doEvents()` → `SDL_PollEvent` → `Controls::ProcessEvent` |
| `E:\TheXTechOH\TheXTech\src\main.cpp` | `thetech_main` 入口（注意：不带 x） |

---

## 六、构建流程

### 6.1 构建 SDL2（含 OHOS 音频驱动）

```bash
# SDL2 源码：F:\portmaster\deps\SDL2-2.30.12
# SDL2 构建目录：F:\portmaster\deps\build-sdl2-ohos
"C:/Program Files/HuaWei/DevEco Studio/sdk/default/openharmony/26.0.0/native/build-tools/cmake/bin/ninja.exe" -C F:/portmaster/deps/build-sdl2-ohos
```

### 6.2 构建 libthextech.so

```bash
# TheXTech 源码：E:\TheXTechOH\TheXTech
# TheXTech 构建目录：E:\TheXTechOH\build-ohos
"C:/Program Files/HuaWei/DevEco Studio/sdk/default/openharmony/26.0.0/native/build-tools/cmake/bin/ninja.exe" -C E:/TheXTechOH/build-ohos
```

### 6.3 复制产物到 HAP 项目

```bash
cp F:/portmaster/deps/build-sdl2-ohos/libSDL2.a E:/TheXTechOH/hap/entry/src/main/cpp/libSDL2.a
cp E:/TheXTechOH/build-ohos/output/lib/libthextech.so E:/TheXTechOH/hap/entry/src/main/cpp/libthextech.so
```

### 6.4 构建 HAP

```bash
cd E:/TheXTechOH && build-hap.cmd
# 产物：E:\TheXTechOH\hap\entry\build\default\outputs\default\entry-default-signed.hap
```

### 6.5 安装到设备

```bash
# hdc 路径
HDC="C:/Program Files/HuaWei/DevEco Studio/sdk/default/openharmony/26.0.0/toolchains/hdc.exe"

# 安装
cd E:/TheXTechOH && "$HDC" install -r hap/entry/build/default/outputs/default/entry-default-signed.hap

# 启动
"$HDC" shell aa start -a EntryAbility -b com.thextech.game

# 抓日志
"$HDC" shell hilog -x 2>&1 | grep "A03200"  # TheXTechHOS 日志
"$HDC" shell hilog -x 2>&1 | grep "A03D00"  # ArkTS JSAPP 日志
```

---

## 七、关键技术约束

### 7.1 ArkTS 语法约束

- catch 子句不能有类型注解
- XComponent 控制器必须通过构造函数参数传递
- **UI 驱动变量必须用 `@State` 声明**，不能用 `private`（否则 UI 不更新）

### 7.2 HarmonyOS 资源路径

- 需文件系统路径访问的只读资源放 `resources/resfile/` 目录（不是 `rawfile/`）
- 用 `context.resourceDir` 获取路径

### 7.3 SDL2 调试日志

- **不要用 `SDL_Log`**，会被 `-O3` 优化消除
- 改用 `fprintf(stderr, ...)` 确保日志不被编译器优化掉

### 7.4 EGL 约束

- 鸿蒙 EGL 只提供 GLES config，没有桌面 GL config
- `SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES)` 必须在 `SDL_Init` **之后**调用（SDL_Init 内部会重置 GL 属性）
- offscreen driver 的 EGL config 需补上 `EGL_SURFACE_TYPE=EGL_WINDOW_BIT`

### 7.5 符号名

- TheXTech 入口函数名是 `thetech_main`（**不带 x**），不是 `thextech_main`
- 链接时需加 `--export-dynamic` 标志才能导出到动态符号表

### 7.6 TheXTech 输入处理

- **只处理 `SDL_KEYDOWN`/`SDL_KEYUP`**，不处理手柄事件
- 默认按键映射：Up=82, Down=81, Left=80, Right=79, Jump(A)=Z(29), Run(B)=X(27), Start=Return(40)
- 键盘状态通过 `SDL_GetKeyboardState()` 获取，不是直接从事件队列读取
- `SDL_PushEvent` 只将事件加入队列，键盘状态数组只有在 `SDL_PollEvent` 处理该事件时才会更新

---

## 八、设备信息

| 项目 | 值 |
|------|-----|
| 设备 ID | `4NZ0225605001361` |
| bundleName | `com.thextech.game` |
| 屏幕分辨率 | 2848x1276 |
| SDK 版本 | HarmonyOS NEXT, SDK 26.0.0 (兼容 6.1.1) |
| OpenGL ES | 3.2 (Maleoon 920) |
| hdc 路径 | `C:/Program Files/HuaWei/DevEco Studio/sdk/default/openharmony/26.0.0/toolchains/hdc.exe` |
| cmake 路径 | `C:/Program Files/HuaWei/DevEco Studio/sdk/default/openharmony/26.0.0/native/build-tools/cmake/bin/cmake.exe` |
| ninja 路径 | `C:/Program Files/HuaWei/DevEco Studio/sdk/default/openharmony/26.0.0/native/build-tools/cmake/bin/ninja.exe` |

---

## 九、下一步行动

1. **用户现场确认**:触控方向键/A/B 是否控制游戏;是否有声音;旋转(系统自动旋转开关需开)
2. 若游戏仍不响应:在 TheXTech 侧加日志确认 keystate 轮询路径(keyboard.cpp),排查 `m_keyboardState` 指针与 `thetech_set_key_state` 写入的数组是否同一块
3. 清理调试日志(TC_TOUCH/TC_HIT/KeyEvent/InjectKey/OHOSAUDIO 计数)

---

## 十、已踩过的坑（避免重复）

1. **`thextech_main` vs `thetech_main`**：源码主函数真名是 `thetech_main`（不带 x）
2. **`SDL_GL_SetAttribute` 时机**：必须在 `SDL_Init` 之后调用
3. **`SDL_Log` 被 -O3 消除**：用 `fprintf(stderr, ...)` 替代(stderr 已重定向到 hilog,tag "thextech:")
4. **`--exclude-libs`**：去掉此选项才能让两个库共享 SDL2 符号(但实际仍双实例,见坑11)
5. **`private` vs `@State`**：UI 驱动变量必须用 `@State`
6. **`resfile` vs `rawfile`**：需文件系统路径访问的资源放 `resfile/`
7. **Java 环境变量**：HAP 打包需用 .cmd 批处理设置
8. **`-fPIC` 与 `ExternalProject_Add`**：子项目需单独传递
9. **SDL2 cmake 配置文件**：`_IMPORT_PREFIX` 需改为绝对路径
10. **`entry/libs/arm64-v8a/libthextech.so` 需删除**：避免与 CMake 构建冲突
11. **SDL2 双实例不可避免**：两个 .so 各链各的 SDL2,符号解析各归各(全局作用域先找到自己)。跨库注入必须走 libthextech 导出的桥函数,不能直接调 SDL API
12. **SDL_PushEvent 不更新 keystate**：SDL2 只在 SDL_SendKeyboardKey(硬件路径)更新;注入必须直写 SDL_GetKeyboardState 数组
13. **hilog 隐私掩码**:格式串必须 `%{public}d/%{public}s/%{public}p`,否则显示 `<private>`
14. **hilog 取尾部日志用 `-z N`**(`-t` 是类型过滤,`-x` 与 `-z` 不能组合)
15. **EntryAbility 运行时 `setPreferredOrientation` 覆盖 module.json5 的 orientation 配置**——两处要一致
16. **ArkTS import**:`@ohos.multimodalInput.inputDevice` 是默认导出;`focusControl` 在该 SDK 中不存在(编译期即失败)
17. **`onKeyEvent` 需要焦点**:组件要 `.focusable(true).defaultFocus(true)` 才收得到按键