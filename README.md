# TheXTech for HarmonyOS NEXT (鸿蒙)

将 [TheXTech](https://github.com/Wohlstand/TheXTech)(Super Mario Bros. X 引擎的 C++ 重写版)移植到 **HarmonyOS NEXT (aarch64)** 的完整工程:源码交叉编译 → 共享库 → NAPI 桥接 → HAP 安装包,支持 GPU 渲染、OHAudio 音频、原生触摸/手柄/键盘输入、屏幕自动旋转、应用内导入用户地图。

> 上游基线:TheXTech `1e8e735f0ecec2720666b7907947763a5f1f69e7`(main, 1.3.8-dev),在本仓库内为纯源码快照(含本项目修改,不带上游 .git)。

## 功能特性

| 功能 | 说明 |
|------|------|
| 渲染 | SDL2 offscreen 驱动 + 原生窗口注入 + EGL/GLES3 (Maleoon) |
| 音频 | SDL2 OHAudio 驱动(回调桥接推模式,见 `patches/sdl2-ohos/`) |
| 触摸 | TheXTech 原生触摸输入:SDL 虚拟触摸屏 + 手指状态直驱,游戏自带触控 UI |
| 手柄 | SDL 虚拟手柄驱动:蓝牙手柄按键经 ArkTS 路由,游戏识别为原生手柄 |
| 键盘 | 键盘状态直写注入 |
| 旋转 | 左右横屏自动旋转(surface 重建自动恢复游戏) |
| 地图 | 应用内导入 zip 地图(免重打包)+ 管理删除 |
| 图标/启动页 | 官方 thetech_512 图标;启动页为游戏素材合成的马里奥场景壁纸 |

## 仓库结构

```
TheXTech/            TheXTech 引擎源码(含修改:src/core/sdl/sdl_core.cpp 等)
hap/                 HarmonyOS 应用工程(ArkTS + NAPI C++)
  entry/src/main/cpp/   thextech_main.cpp(NAPI 桥)、CMakeLists、SDL2 头
  entry/src/main/ets/   Index.ets(启动页/导入/管理)、EntryAbility
patches/sdl2-ohos/   SDL2 的 OHOS 音频驱动与 offscreen 修改
build-hap.cmd        HAP 构建入口(含 JAVA_HOME 设置)
make_icon.py         图标生成(官方 logo → 分层图标)
make_wallpaper.py    启动页壁纸生成(游戏素材合成)
HANDOVER.md          完整移植文档:全部根因分析、踩坑记录、构建流程
```

## 构建

依赖:DevEco Studio(SDK 26.x)、其自带 LLVM/clang、CMake/Ninja、Python3(可选)。

1. **SDL2 静态库**(含 `patches/sdl2-ohos/` 中的 OHOS 音频驱动与 offscreen 修改,将补丁文件放入 SDL2-2.30.12 对应路径):
   ```bash
   # 用 DevEco 的 OHOS 工具链 cmake 配置后:
   ninja -C <sdl2-build-dir>
   cp <sdl2-build-dir>/libSDL2.a hap/entry/src/main/cpp/
   ```
   注意开启 `SDL_JOYSTICK=ON`(虚拟手柄驱动);cmake 重配置后需手动同步
   `include-config-release/SDL2/SDL_config.h` 到 `include/SDL2/`。
2. **libthextech.so**:
   ```bash
   ninja -C build-ohos
   cp build-ohos/output/lib/libthextech.so hap/entry/src/main/cpp/
   ```
3. **游戏素材**(体积与版权原因不入库):从 TheXTech 官方发布包获取 SMBX 素材,
   解压到 `hap/entry/src/main/resources/resfile/gamedata/`(需包含 worlds/、graphics/ 等)。
4. **签名**:配置你自己的签名证书(`hap/build-profile.json5`),不要使用他人的 p12。
5. **打包安装**:
   ```bash
   cd hap && ../build-hap.cmd   # 产物 entry-default-signed.hap
   hdc install -r entry/build/default/outputs/default/entry-default-signed.hap
   ```

## 移植要点(摘录,详见 HANDOVER.md)

- **SDL2 双实例符号路由**:libentry.so 与 libthextech.so 各自静态链接 SDL2;运行时
  公开 SDL 符号经 PLT 统一解析到 libentry 的实例。因此键盘状态直写、虚拟手柄等
  走公开 API 的桥有效;而 `SDL_AddTouch` 等 hidden 符号必须在 libentry 侧调用。
- **键盘状态**:`SDL_PushEvent` 不会更新 `SDL_GetKeyboardState` 数组(仅硬件路径
  `SDL_SendKeyboardKey` 更新),注入必须直写数组。
- **音频**:OHAudio 回调模式与 SDL 推模式经中间缓冲桥接;SDL 音频线程
  (`ProvidesOwnCallbackThread=SDL_FALSE`)驱动 PlayDevice/WriteDevice。
- **触摸**:虚拟触摸屏须在引擎启动扫描前注册(SDL_AddTouch, DIRECT 类型)。

## 许可与致谢

- TheXTech 引擎:GPL-3.0(© Vitaly Novichkov 等,基于 Andrew Spinks 的 SMBX 代码)
- SDL2:zlib 许可
- 游戏素材(SMBX 资产包、第三方地图)版权归各自作者,不入库,请自行获取
- 图标与 logo 来自 TheXTech 官方资源

本仓库仅为技术移植研究用途。
