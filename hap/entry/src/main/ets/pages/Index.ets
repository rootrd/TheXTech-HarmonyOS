import testNapi from 'libentry.so';
import { common } from '@kit.AbilityKit';
import display from '@ohos.display';
import inputDevice from '@ohos.multimodalInput.inputDevice';
import fs from '@ohos.file.fs';
import zlib from '@ohos.zlib';
import { picker } from '@kit.CoreFileKit';

const SDL_SCANCODE_RETURN: number = 40;
const SDL_SCANCODE_ESCAPE: number = 41;
const SDL_SCANCODE_BACKSPACE: number = 42;
const SDL_SCANCODE_TAB: number = 43;
const SDL_SCANCODE_SPACE: number = 44;
const SDL_SCANCODE_UP: number = 82;
const SDL_SCANCODE_DOWN: number = 81;
const SDL_SCANCODE_LEFT: number = 80;
const SDL_SCANCODE_RIGHT: number = 79;
const SDL_SCANCODE_LCTRL: number = 224;
const SDL_SCANCODE_LSHIFT: number = 225;
const SDL_SCANCODE_LALT: number = 226;
const SDL_SCANCODE_RCTRL: number = 228;
const SDL_SCANCODE_RSHIFT: number = 229;
const SDL_SCANCODE_RALT: number = 230;

const KEYCODE_0: number = 2000;
const KEYCODE_A: number = 2017;
const KEYCODE_ALT_LEFT: number = 2045;
const KEYCODE_ALT_RIGHT: number = 2046;
const KEYCODE_SHIFT_LEFT: number = 2047;
const KEYCODE_SHIFT_RIGHT: number = 2048;
const KEYCODE_TAB: number = 2049;
const KEYCODE_SPACE: number = 2050;
const KEYCODE_ENTER: number = 2054;
const KEYCODE_DEL: number = 2055;
const KEYCODE_ESCAPE: number = 2070;
const KEYCODE_CTRL_LEFT: number = 2072;
const KEYCODE_CTRL_RIGHT: number = 2073;
// Gamepad buttons (@ohos.multimodalInput.keyCode), routed to the engine's
// native joystick input method through the SDL virtual gamepad.
const KEYCODE_DPAD_UP: number = 2012;
const KEYCODE_DPAD_DOWN: number = 2013;
const KEYCODE_DPAD_LEFT: number = 2014;
const KEYCODE_DPAD_RIGHT: number = 2015;
const KEYCODE_DPAD_CENTER: number = 2016;
const KEYCODE_BUTTON_A: number = 2301;
const KEYCODE_BUTTON_B: number = 2302;
const KEYCODE_BUTTON_X: number = 2304;
const KEYCODE_BUTTON_Y: number = 2305;
const KEYCODE_BUTTON_L1: number = 2307;
const KEYCODE_BUTTON_R1: number = 2308;
const KEYCODE_BUTTON_L2: number = 2309;
const KEYCODE_BUTTON_R2: number = 2310;
const KEYCODE_BUTTON_SELECT: number = 2311;
const KEYCODE_BUTTON_START: number = 2312;
const KEYCODE_BUTTON_MODE: number = 2313;
const KEYCODE_BUTTON_THUMBL: number = 2314;
const KEYCODE_BUTTON_THUMBR: number = 2315;

@Entry
@Component
struct Index {
  private xcompController: XComponentController = new XComponentController();
  private surfaceId: string = '';
  @State renderStarted: boolean = false;
  private screenW: number = 0;
  private screenH: number = 0;
  private keyState: number[] = new Array(512).fill(0);
  private joyAttached: boolean = false;
  private touchLogged: boolean = false;
  private pxPerVp: number = 3.5;
  @State importMsg: string = '';
  @State showManager: boolean = false;
  @State userEpisodes: string[] = [];
  private hatUp: boolean = false;
  private hatDown: boolean = false;
  private hatLeft: boolean = false;
  private hatRight: boolean = false;
  private deviceChangeListener: (data: inputDevice.DeviceListener) => void =
    (data: inputDevice.DeviceListener): void => {
      console.info('TheXTech inputDevice ' + data.type + ' id=' + data.deviceId);
      this.checkGamepads();
    };

  mapKeyCode(code: number): number {
    if (code >= KEYCODE_0 && code <= KEYCODE_0 + 9) {
      const d: number = code - KEYCODE_0;
      return d === 0 ? 39 : 29 + d;
    }
    if (code >= KEYCODE_A && code <= KEYCODE_A + 25) {
      return 4 + (code - KEYCODE_A);
    }
    switch (code) {
      case KEYCODE_ALT_LEFT: return SDL_SCANCODE_LALT;
      case KEYCODE_ALT_RIGHT: return SDL_SCANCODE_RALT;
      case KEYCODE_SHIFT_LEFT: return SDL_SCANCODE_LSHIFT;
      case KEYCODE_SHIFT_RIGHT: return SDL_SCANCODE_RSHIFT;
      case KEYCODE_TAB: return SDL_SCANCODE_TAB;
      case KEYCODE_SPACE: return SDL_SCANCODE_SPACE;
      case KEYCODE_ENTER: return SDL_SCANCODE_RETURN;
      case KEYCODE_DEL: return SDL_SCANCODE_BACKSPACE;
      case KEYCODE_ESCAPE: return SDL_SCANCODE_ESCAPE;
      case KEYCODE_CTRL_LEFT: return SDL_SCANCODE_LCTRL;
      case KEYCODE_CTRL_RIGHT: return SDL_SCANCODE_RCTRL;
      default: return -1;
    }
  }

  private ensureJoyAttached(): void {
    if (this.joyAttached) {
      return;
    }
    try {
      const r: number = testNapi.attachVirtualGamepad();
      if (r === 0) {
        this.joyAttached = true;
        console.info('TheXTech virtual gamepad attached');
      }
    } catch (e) {
      console.error('TheXTech attachVirtualGamepad failed');
    }
  }

  // Gamepad keys -> SDL virtual joystick (engine's native gamepad input).
  // Returns true when the keycode belonged to a gamepad.
  private handleGamepadKey(code: number, down: boolean): boolean {
    let button: number = -1;
    switch (code) {
      case KEYCODE_BUTTON_A: button = 0; break;
      case KEYCODE_BUTTON_B: button = 1; break;
      case KEYCODE_BUTTON_X: button = 2; break;
      case KEYCODE_BUTTON_Y: button = 3; break;
      case KEYCODE_BUTTON_L1: button = 4; break;
      case KEYCODE_BUTTON_R1: button = 5; break;
      case KEYCODE_BUTTON_SELECT: button = 6; break;
      case KEYCODE_BUTTON_START: button = 7; break;
      case KEYCODE_BUTTON_MODE: button = 8; break;
      case KEYCODE_BUTTON_THUMBL: button = 9; break;
      case KEYCODE_BUTTON_THUMBR: button = 10; break;
      case KEYCODE_BUTTON_L2: button = 11; break;
      case KEYCODE_BUTTON_R2: button = 12; break;
      default: break;
    }
    if (button >= 0) {
      this.ensureJoyAttached();
      testNapi.virtualGamepadButton(button, down ? 1 : 0);
      return true;
    }

    switch (code) {
      case KEYCODE_DPAD_UP: this.hatUp = down; break;
      case KEYCODE_DPAD_DOWN: this.hatDown = down; break;
      case KEYCODE_DPAD_LEFT: this.hatLeft = down; break;
      case KEYCODE_DPAD_RIGHT: this.hatRight = down; break;
      case KEYCODE_DPAD_CENTER:
        this.ensureJoyAttached();
        testNapi.virtualGamepadButton(7, down ? 1 : 0);
        return true;
      default: return false;
    }
    this.ensureJoyAttached();
    let mask: number = 0;
    if (this.hatUp) { mask |= 1; }
    if (this.hatRight) { mask |= 2; }
    if (this.hatDown) { mask |= 4; }
    if (this.hatLeft) { mask |= 8; }
    testNapi.virtualGamepadHat(mask);
    return true;
  }

  handleKeyEvent(event: KeyEvent): boolean {
    const down: boolean = event.type === KeyType.Down;
    const up: boolean = event.type === KeyType.Up;
    if (!down && !up) {
      return false;
    }
    console.info('TheXTech KeyEvent keyCode=' + event.keyCode +
      ' type=' + (down ? 'down' : 'up'));
    if (this.handleGamepadKey(event.keyCode, down)) {
      return true;
    }
    const sc: number = this.mapKeyCode(event.keyCode);
    if (sc < 0 || sc > 511) {
      return false;
    }
    if (this.keyState[sc] === (down ? 1 : 0)) {
      return true;
    }
    this.keyState[sc] = down ? 1 : 0;
    testNapi.injectKey(sc, down ? 1 : 0);
    return true;
  }

  aboutToAppear(): void {
    console.info('TheXTech Index aboutToAppear');
    inputDevice.on('change', this.deviceChangeListener);
    this.checkGamepads();
  }

  aboutToDisappear(): void {
    try {
      inputDevice.off('change', this.deviceChangeListener);
    } catch (e) {
      console.error('inputDevice.off error');
    }
    try {
      testNapi.destroyRender();
    } catch (e) {
      console.error('destroy error: ' + (e as Error).message);
    }
  }

  // Scan input devices; attach the virtual gamepad as soon as a joystick
  // is present so the engine's gamepad input method takes over.
  private async checkGamepads(): Promise<void> {
    try {
      const ids: Array<number> = await inputDevice.getDeviceList();
      let found: boolean = false;
      for (let i = 0; i < ids.length; i++) {
        try {
          const info: inputDevice.InputDeviceData = await inputDevice.getDeviceInfo(ids[i]);
          if (info.sources.indexOf('joystick') >= 0) {
            found = true;
            console.info('TheXTech gamepad device: ' + info.name + ' id=' + ids[i]);
          }
        } catch (e) {
          console.error('TheXTech getDeviceInfo failed');
        }
      }
      console.info('TheXTech gamepadConnected=' + found);
      if (found && this.renderStarted) {
        this.ensureJoyAttached();
      }
    } catch (e) {
      console.error('TheXTech getDeviceList failed');
    }
  }

  onSurfaceLoad(): void {
    const sid: string = this.xcompController.getXComponentSurfaceId();
    if (this.renderStarted && this.surfaceId === sid) {
      return; // same surface (e.g. repeated onLoad), keep running
    }
    // Screen auto-rotation recreates the XComponent surface with a new id
    // while the game is running: tear down and relaunch on the new surface.
    // A fresh start waits for the user to press 启动游戏 (so the map
    // import / manage controls on the launch screen are reachable).
    const isRotation: boolean = this.renderStarted && this.surfaceId !== sid;
    if (isRotation) {
      console.info('TheXTech surface recreated (rotation), relaunching');
      this.exitGame();
    }
    this.surfaceId = sid;
    console.log('TheXTech surfaceId=' + sid);
    if (isRotation) {
      setTimeout(() => {
        this.launchGame();
      }, 800);
    }
  }

  launchGame(): void {
    if (this.renderStarted) {
      return;
    }
    try {
      const rect = this.xcompController.getXComponentSurfaceRect();
      this.screenW = rect.surfaceWidth;
      this.screenH = rect.surfaceHeight;
      const context: common.UIAbilityContext = getContext(this) as common.UIAbilityContext;

      const assetPackPath: string = context.resourceDir + '/gamedata';
      const userDir: string = context.filesDir;

      testNapi.prepareGameEnv();
      testNapi.setGameRoot(assetPackPath, userDir);

      const ret: number = testNapi.initRender(this.surfaceId, this.screenW, this.screenH,
        assetPackPath, userDir);
      try {
        const d: display.Display = display.getDefaultDisplaySync();
        if (d.densityPixels > 0) {
          this.pxPerVp = d.densityPixels;
        }
        console.info('TheXTech pxPerVp=' + this.pxPerVp);
      } catch (e) {
        console.error('TheXTech getDisplay failed');
      }
      console.log('TheXTech initRender ret=' + ret + ' ' + this.screenW + 'x' + this.screenH);
      if (ret === 0) {
        this.renderStarted = true;
      }
    } catch (e) {
      console.error('TheXTech launch error: ' + (e as Error).message);
    }
  }

  onBackPress(): boolean {
    if (this.showManager) {
      this.showManager = false;
      return true;
    }
    if (this.renderStarted) {
      this.exitGame();
      return true;
    }
    return false;
  }

  // ---- User map import & management (engine scans filesDir/worlds/smbx) ----

  private userWorldsRoot(): string {
    const ctx: common.UIAbilityContext = getContext(this) as common.UIAbilityContext;
    return ctx.filesDir + '/worlds/smbx';
  }

  private pathExists(p: string): boolean {
    try {
      fs.statSync(p);
      return true;
    } catch (e) {
      return false;
    }
  }

  private removeRecursive(p: string): void {
    if (fs.statSync(p).isDirectory()) {
      const names: string[] = fs.listFileSync(p);
      for (let i = 0; i < names.length; i++) {
        this.removeRecursive(p + '/' + names[i]);
      }
      fs.rmdirSync(p);
    } else {
      fs.unlinkSync(p);
    }
  }

  private isEpisodeDir(p: string): boolean {
    try {
      const names: string[] = fs.listFileSync(p);
      for (let i = 0; i < names.length; i++) {
        const l: string = names[i].toLowerCase();
        if (l.endsWith('.wld') || l.endsWith('.wldx') ||
            l.endsWith('.lvl') || l.endsWith('.lvlx')) {
          return true;
        }
      }
    } catch (e) {
      // not a directory
    }
    return false;
  }

  private findEpisodeDirs(root: string, depth: number, out: string[]): void {
    if (this.isEpisodeDir(root)) {
      out.push(root);
      return;
    }
    if (depth <= 0) {
      return;
    }
    let names: string[] = [];
    try {
      names = fs.listFileSync(root);
    } catch (e) {
      return;
    }
    for (let i = 0; i < names.length; i++) {
      const child: string = root + '/' + names[i];
      try {
        if (fs.statSync(child).isDirectory()) {
          this.findEpisodeDirs(child, depth - 1, out);
        }
      } catch (e) {
        // skip
      }
    }
  }

  private refreshEpisodes(): void {
    const root: string = this.userWorldsRoot();
    const list: string[] = [];
    try {
      const names: string[] = fs.listFileSync(root);
      for (let i = 0; i < names.length; i++) {
        const child: string = root + '/' + names[i];
        try {
          if (fs.statSync(child).isDirectory()) {
            list.push(names[i]);
          }
        } catch (e) {
          // skip
        }
      }
    } catch (e) {
      // root missing
    }
    this.userEpisodes = list;
  }

  private async importMaps(): Promise<void> {
    try {
      const pv: picker.DocumentViewPicker = new picker.DocumentViewPicker();
      const uris: Array<string> = await pv.select({
        maxNumber: 10,
        fileSuffixExtensions: ['.zip']
      } as picker.DocumentSelectOptions);
      if (!uris || uris.length === 0) {
        return;
      }
      const ctx: common.UIAbilityContext = getContext(this) as common.UIAbilityContext;
      const tmp: string = ctx.filesDir + '/import_tmp';
      const root: string = this.userWorldsRoot();
      try {
        fs.mkdirSync(root, true);
      } catch (e) {
        // exists
      }
      let imported: number = 0;
      let failed: number = 0;
      for (let u = 0; u < uris.length; u++) {
        const zipPath: string = tmp + '/src' + u + '.zip';
        const exDir: string = tmp + '/ex' + u;
        try {
          fs.mkdirSync(exDir, true);
          fs.copyFileSync(uris[u], zipPath);
          await zlib.unzipFile(zipPath, exDir, {
            level: zlib.CompressLevel.COMPRESS_LEVEL_NO_COMPRESSION
          } as zlib.Options);
        } catch (e) {
          failed++;
          continue;
        }
        const eps: string[] = [];
        this.findEpisodeDirs(exDir, 3, eps);
        for (let k = 0; k < eps.length; k++) {
          let name: string = eps[k].substring(eps[k].lastIndexOf('/') + 1);
          if (!name) {
            name = 'Episode' + u;
          }
          let dest: string = root + '/' + name;
          let n: number = 1;
          while (this.pathExists(dest)) {
            dest = root + '/' + name + '_' + n;
            n++;
          }
          try {
            fs.renameSync(eps[k], dest);
            imported++;
          } catch (er) {
            failed++;
          }
        }
      }
      try {
        if (this.pathExists(tmp)) {
          this.removeRecursive(tmp);
        }
      } catch (e) {
        // best effort
      }
      this.importMsg = imported > 0
        ? '已导入 ' + imported + ' 个地图' + (failed > 0 ? '，失败 ' + failed : '')
        : (failed > 0 ? '导入失败 ' + failed + ' 个（仅支持 zip）' : '未找到地图内容');
      this.refreshEpisodes();
    } catch (e) {
      this.importMsg = '导入失败: ' + (e as Error).message;
    }
  }

  private deleteEpisode(name: string): void {
    AlertDialog.show({
      title: '删除地图',
      message: '确定删除「' + name + '」吗？',
      primaryButton: {
        value: '取消',
        action: (): void => {}
      },
      secondaryButton: {
        value: '删除',
        fontColor: '#E52521',
        action: (): void => {
          try {
            this.removeRecursive(this.userWorldsRoot() + '/' + name);
          } catch (e) {
            console.error('TheXTech delete failed: ' + (e as Error).message);
          }
          this.refreshEpisodes();
        }
      }
    });
  }

  private exitGame(): void {
    console.info('TheXTech exitGame');
    try {
      testNapi.destroyRender();
    } catch (e) {
      console.error('destroy error: ' + (e as Error).message);
    }
    this.renderStarted = false;
  }

  // Raw touch passthrough: the engine renders its own on-screen touch
  // controls and reads SDL touch-device state, so we forward every touch
  // (normalized to the window) instead of drawing our own buttons.
  private handleTouch(event: TouchEvent, source: string): void {
    if (!this.renderStarted) {
      return;
    }
    let action: number = 2; // move
    if (event.type === TouchType.Down) {
      action = 0;
    } else if (event.type === TouchType.Up || event.type === TouchType.Cancel) {
      action = 1;
    }
    const w: number = this.screenW > 0 ? this.screenW : 1;
    const h: number = this.screenH > 0 ? this.screenH : 1;
    const touches: TouchObject[] = event.changedTouches;
    if (!this.touchLogged) {
      this.touchLogged = true;
      const t0: TouchObject = touches.length > 0 ? touches[0] : event.touches[0];
      console.info('TheXTech first touch src=' + source + ' type=' + event.type +
        ' wx=' + (t0 ? t0.windowX : -1) + ' wy=' + (t0 ? t0.windowY : -1) +
        ' screenW=' + this.screenW + ' screenH=' + this.screenH);
    }
    for (let i = 0; i < touches.length; i++) {
      const t: TouchObject = touches[i];
      // windowX/windowY are in vp; the window size is in px -> convert first
      let nx: number = (t.windowX * this.pxPerVp) / w;
      let ny: number = (t.windowY * this.pxPerVp) / h;
      if (nx < 0) { nx = 0; }
      if (nx > 1) { nx = 1; }
      if (ny < 0) { ny = 0; }
      if (ny > 1) { ny = 1; }
      testNapi.sendTouch(action, t.id, nx, ny);
    }
  }

  build() {
    Stack() {
      XComponent({
        id: 'thextech_surface',
        type: 'surface',
        controller: this.xcompController
      })
        .onLoad(() => {
          this.onSurfaceLoad();
        })
        .width('100%')
        .height('100%')

      if (!this.renderStarted) {
        Image($r('app.media.launch_bg'))
          .objectFit(ImageFit.Cover)
          .width('100%')
          .height('100%')

        Column() {
          // The title logo (mushroom + "TheXTech") is part of the wallpaper.
          Text('Super Mario Bros. X Engine')
            .fontSize(20)
            .fontColor('#FFF7ED')
            .textShadow({
              radius: 6,
              color: 'rgba(0,0,0,0.85)',
              offsetX: 1,
              offsetY: 1
            })

          Button('启动游戏')
            .fontSize(20)
            .fontColor('#FFFFFF')
            .backgroundColor('#7C3AED')
            .borderRadius(12)
            .margin({ top: 26 })
            .padding({ left: 48, right: 48, top: 14, bottom: 14 })
            .onClick(() => {
              this.launchGame();
            })

          Row() {
            Button('导入地图')
              .fontSize(16)
              .fontColor('#FFFFFF')
              .backgroundColor('#2563EB')
              .borderRadius(10)
              .padding({ left: 24, right: 24, top: 10, bottom: 10 })
              .onClick(() => {
                this.importMaps();
              })
            Button('管理地图')
              .fontSize(16)
              .fontColor('#FFFFFF')
              .backgroundColor(this.showManager ? '#475569' : '#0891B2')
              .borderRadius(10)
              .margin({ left: 16 })
              .padding({ left: 24, right: 24, top: 10, bottom: 10 })
              .onClick(() => {
                this.showManager = !this.showManager;
                if (this.showManager) {
                  this.refreshEpisodes();
                }
              })
          }
          .margin({ top: 18 })

          if (this.importMsg !== '') {
            Text(this.importMsg)
              .fontSize(13)
              .fontColor('#FDE68A')
              .margin({ top: 10 })
          }

          if (this.showManager) {
            Column() {
              Text('已导入的地图（' + this.userEpisodes.length + '）')
                .fontSize(14)
                .fontColor('#E2E8F0')
                .margin({ bottom: 6 })
              Scroll() {
                Column() {
                  ForEach(this.userEpisodes, (name: string) => {
                    Row() {
                      Text(name)
                        .fontSize(13)
                        .fontColor('#F1F5F9')
                        .layoutWeight(1)
                        .maxLines(1)
                        .textOverflow({ overflow: TextOverflow.Ellipsis })
                      Button('删除')
                        .fontSize(12)
                        .fontColor('#FFFFFF')
                        .backgroundColor('#DC2626')
                        .borderRadius(8)
                        .padding({ left: 14, right: 14, top: 6, bottom: 6 })
                        .onClick(() => {
                          this.deleteEpisode(name);
                        })
                    }
                    .width('100%')
                    .padding({ left: 10, right: 10, top: 5, bottom: 5 })
                    .borderRadius(8)
                    .backgroundColor('rgba(15,23,42,0.65)')
                    .margin({ bottom: 5 })
                  }, (name: string) => name)
                  if (this.userEpisodes.length === 0) {
                    Text('暂无导入的地图（zip 导入后在此管理）')
                      .fontSize(13)
                      .fontColor('#94A3B8')
                  }
                }
                .width('100%')
              }
              .height(160)
              .width(520)
              .margin({ top: 10 })
              .align(Alignment.Center)
            }
            .width('100%')
            .margin({ top: 12 })
          }
        }
        .width('100%')
        .height('100%')
        .justifyContent(FlexAlign.End)
        .padding({ bottom: 240 })
      }
    }
    .width('100%')
    .height('100%')
    .key('gameRoot')
    .focusable(true)
    .defaultFocus(true)
    .onTouch((event: TouchEvent) => {
      this.handleTouch(event, 'root');
    })
    .onKeyEvent((event: KeyEvent) => {
      this.handleKeyEvent(event);
    })
  }
}
