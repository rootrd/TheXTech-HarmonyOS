import testNapi from 'libentry.so';

const SDL_SCANCODE_UP: number = 82;
const SDL_SCANCODE_DOWN: number = 81;
const SDL_SCANCODE_LEFT: number = 80;
const SDL_SCANCODE_RIGHT: number = 79;
const SDL_SCANCODE_Z: number = 29;
const SDL_SCANCODE_X: number = 27;
const SDL_SCANCODE_RETURN: number = 40;

const COLOR_DPAD_BODY = '#FF333333';
const COLOR_DPAD_DARK = '#FF1A1A1A';
const COLOR_DPAD_LIGHT = '#FF555555';
const COLOR_A_RED = '#FFE52521';
const COLOR_A_DARK = '#FF8B0000';
const COLOR_B_YELLOW = '#FFFBD000';
const COLOR_B_DARK = '#FFB89000';
const COLOR_QBLOCK = '#FFF8B800';
const COLOR_QBLOCK_DARK = '#FF8B6914';
const COLOR_PIXEL_BORDER = '#FF000000';
const COLOR_TEXT_WHITE = '#FFFFFFFF';
const COLOR_TEXT_BLACK = '#FF000000';

interface TouchZone {
  id: string;
  cx: number;
  cy: number;
  halfW: number;
  halfH: number;
  scancode: number;
  kind: string;
}

@Component
export struct TouchControls {
  @Prop showControls: boolean = false;
  @State zones: TouchZone[] = [];
  @State pressedIds: string = '';
  @State paused: boolean = false;
  private overlayW: number = 0;
  private overlayH: number = 0;
  private activeTouches: Map<string, number> = new Map();

  aboutToDisappear(): void {
    this.releaseAllInputs();
  }

  private releaseAllInputs(): void {
    this.zones.forEach((el: TouchZone): void => {
      if (this.isPressed(el.id)) {
        testNapi.injectKey(el.scancode, 0);
      }
    });
    this.pressedIds = '';
    this.activeTouches.clear();
  }

  private buildZones(): void {
    const w = this.overlayW;
    const h = this.overlayH;
    if (w <= 0 || h <= 0) {
      return;
    }

    const zones: TouchZone[] = [];
    const unit = Math.min(w, h) * 0.06;

    // D-Pad cross layout
    const dpadCx = w * 0.16;
    const dpadCy = h * 0.74;
    const dpadArm = unit * 2.2;
    const dpadThick = unit * 1.1;

    zones.push({
      id: 'dpadUp', cx: dpadCx, cy: dpadCy - dpadArm, halfW: dpadThick, halfH: dpadArm * 0.7,
      scancode: SDL_SCANCODE_UP, kind: 'dpad'
    });
    zones.push({
      id: 'dpadDown', cx: dpadCx, cy: dpadCy + dpadArm, halfW: dpadThick, halfH: dpadArm * 0.7,
      scancode: SDL_SCANCODE_DOWN, kind: 'dpad'
    });
    zones.push({
      id: 'dpadLeft', cx: dpadCx - dpadArm, cy: dpadCy, halfW: dpadArm * 0.7, halfH: dpadThick,
      scancode: SDL_SCANCODE_LEFT, kind: 'dpad'
    });
    zones.push({
      id: 'dpadRight', cx: dpadCx + dpadArm, cy: dpadCy, halfW: dpadArm * 0.7, halfH: dpadThick,
      scancode: SDL_SCANCODE_RIGHT, kind: 'dpad'
    });

    // A button (red circle)
    const aR = unit * 1.6;
    const aCx = w * 0.86;
    const aCy = h * 0.66;
    zones.push({
      id: 'btnA', cx: aCx, cy: aCy, halfW: aR, halfH: aR,
      scancode: SDL_SCANCODE_Z, kind: 'a'
    });

    // B button (yellow circle)
    const bR = unit * 1.3;
    const bCx = w * 0.76;
    const bCy = h * 0.80;
    zones.push({
      id: 'btnB', cx: bCx, cy: bCy, halfW: bR, halfH: bR,
      scancode: SDL_SCANCODE_X, kind: 'b'
    });

    // Start button (question block)
    const qSize = unit * 1.0;
    zones.push({
      id: 'btnStart', cx: w * 0.50, cy: h * 0.94, halfW: qSize, halfH: qSize,
      scancode: SDL_SCANCODE_RETURN, kind: 'qblock'
    });

    this.zones = zones;
  }

  private hitTest(x: number, y: number): TouchZone | null {
    for (let i = this.zones.length - 1; i >= 0; i--) {
      const el = this.zones[i];
      if (el.kind === 'a' || el.kind === 'b') {
        const dx = x - el.cx;
        const dy = y - el.cy;
        if (dx * dx + dy * dy <= el.halfW * el.halfW) {
          return el;
        }
      } else {
        if (Math.abs(x - el.cx) <= el.halfW && Math.abs(y - el.cy) <= el.halfH) {
          return el;
        }
      }
    }
    return null;
  }

  private isPressed(id: string): boolean {
    return this.pressedIds.indexOf('|' + id + '|') >= 0;
  }

  private setPressed(id: string, pressed: boolean): void {
    const token = '|' + id + '|';
    if (pressed && !this.isPressed(id)) {
      this.pressedIds += token;
    }
    if (!pressed && this.isPressed(id)) {
      this.pressedIds = this.pressedIds.replace(token, '');
    }
  }

  private handleTouch(event: TouchEvent): void {
    if (!this.showControls || this.paused) {
      return;
    }
    for (let i = 0; i < event.changedTouches.length; i++) {
      const touch = event.changedTouches[i];
      const id = touch.id;
      const x = Number(touch.x);
      const y = Number(touch.y);
      console.info('TC_TOUCH type=' + event.type + ' x=' + x + ' y=' + y);
      if (event.type === TouchType.Down) {
        const el = this.hitTest(x, y);
        if (el && !this.activeTouches.has(el.id)) {
          console.info('TC_HIT id=' + el.id + ' scancode=' + el.scancode);
          this.activeTouches.set(el.id, id);
          if (el.id === 'btnStart') {
            this.togglePause();
          } else {
            testNapi.injectKey(el.scancode, 1);
            this.setPressed(el.id, true);
          }
        } else {
          console.info('TC_NO_HIT x=' + x + ' y=' + y);
        }
      } else if (event.type === TouchType.Up) {
        let foundId = '';
        for (const entry of this.activeTouches.entries()) {
          if (entry[1] === id) {
            foundId = entry[0];
            break;
          }
        }
        if (foundId !== '') {
          this.activeTouches.delete(foundId);
          if (foundId !== 'btnStart') {
            const el = this.zones.find((e: TouchZone) => e.id === foundId);
            if (el) {
              testNapi.injectKey(el.scancode, 0);
              this.setPressed(foundId, false);
            }
          }
        }
      }
    }
  }

  private togglePause(): void {
    this.paused = !this.paused;
    console.info('TC_TOGGLE_PAUSE paused=' + this.paused);
    testNapi.injectKey(SDL_SCANCODE_RETURN, 1);
    testNapi.injectKey(SDL_SCANCODE_RETURN, 0);
  }

  private exitToHome(): void {
    this.releaseAllInputs();
    this.paused = false;
    AppStorage.setOrCreate('reqExitGame', (AppStorage.get<number>('reqExitGame') ?? 0) + 1);
  }

  @Builder
  private DpadArmView(el: TouchZone) {
    Stack() {
      // base body
      Rect()
        .width(el.halfW * 2)
        .height(el.halfH * 2)
        .fill(this.isPressed(el.id) ? COLOR_DPAD_DARK : COLOR_DPAD_BODY)
        .stroke(COLOR_PIXEL_BORDER)
        .strokeWidth(3)
      // inner highlight stripe
      Rect()
        .width(el.halfW * 2 - 8)
        .height(el.halfH * 2 - 8)
        .fill(COLOR_DPAD_LIGHT)
        .opacity(this.isPressed(el.id) ? 0.0 : 0.25)
    }
    .width(el.halfW * 2)
    .height(el.halfH * 2)
    .position({ x: el.cx - el.halfW, y: el.cy - el.halfH })
    .hitTestBehavior(HitTestMode.None)
  }

  @Builder
  private CircleButtonView(el: TouchZone, baseColor: string, darkColor: string, label: string) {
    Stack() {
      // outer pixel border ring
      Circle()
        .width(el.halfW * 2)
        .height(el.halfH * 2)
        .fill(COLOR_PIXEL_BORDER)
      // main body
      Circle()
        .width(el.halfW * 2 - 6)
        .height(el.halfH * 2 - 6)
        .fill(this.isPressed(el.id) ? darkColor : baseColor)
        .position({ x: 3, y: 3 })
      // inner shadow ring (bottom-right)
      Circle()
        .width(el.halfW * 2 - 12)
        .height(el.halfH * 2 - 12)
        .fill(darkColor)
        .opacity(this.isPressed(el.id) ? 0.0 : 0.35)
        .position({ x: 6, y: 6 })
      // label text
      Text(label)
        .fontSize(el.halfW * 0.9)
        .fontColor(COLOR_TEXT_WHITE)
        .fontWeight(FontWeight.Bold)
        .textAlign(TextAlign.Center)
    }
    .width(el.halfW * 2)
    .height(el.halfH * 2)
    .position({
      x: this.isPressed(el.id) ? el.cx - el.halfW + 3 : el.cx - el.halfW,
      y: this.isPressed(el.id) ? el.cy - el.halfH + 3 : el.cy - el.halfH
    })
    .hitTestBehavior(HitTestMode.None)
  }

  @Builder
  private QBlockView(el: TouchZone) {
    Stack() {
      // outer pixel border
      Rect()
        .width(el.halfW * 2)
        .height(el.halfH * 2)
        .fill(COLOR_PIXEL_BORDER)
      // main yellow body
      Rect()
        .width(el.halfW * 2 - 6)
        .height(el.halfH * 2 - 6)
        .fill(this.isPressed(el.id) ? COLOR_QBLOCK_DARK : COLOR_QBLOCK)
        .position({ x: 3, y: 3 })
      // inner darker border lines (pixel brick pattern)
      Rect()
        .width(el.halfW * 2 - 12)
        .height(el.halfH * 2 - 12)
        .fill(COLOR_QBLOCK)
        .stroke(COLOR_QBLOCK_DARK)
        .strokeWidth(2)
        .position({ x: 6, y: 6 })
        .opacity(this.isPressed(el.id) ? 0.5 : 1.0)
      // question mark
      Text('?')
        .fontSize(el.halfW * 1.1)
        .fontColor(COLOR_TEXT_WHITE)
        .fontWeight(FontWeight.Bold)
        .textAlign(TextAlign.Center)
    }
    .width(el.halfW * 2)
    .height(el.halfH * 2)
    .position({
      x: this.isPressed(el.id) ? el.cx - el.halfW + 2 : el.cx - el.halfW,
      y: this.isPressed(el.id) ? el.cy - el.halfH + 2 : el.cy - el.halfH
    })
    .hitTestBehavior(HitTestMode.None)
  }

  @Builder
  private DpadCenterView() {
    // center square of the D-pad cross
    Stack() {
      Rect()
        .width(this.overlayW > 0 ? Math.min(this.overlayW, this.overlayH) * 0.06 * 2.2 : 0)
        .height(this.overlayW > 0 ? Math.min(this.overlayW, this.overlayH) * 0.06 * 2.2 : 0)
        .fill(COLOR_DPAD_BODY)
        .stroke(COLOR_PIXEL_BORDER)
        .strokeWidth(3)
    }
    .hitTestBehavior(HitTestMode.None)
  }

  build() {
    Stack() {
      // D-Pad center square (visual only, behind arms)
      if (this.overlayW > 0) {
        Stack() {
          this.DpadCenterView()
        }
        .position({
          x: this.overlayW * 0.16 - Math.min(this.overlayW, this.overlayH) * 0.06 * 1.1,
          y: this.overlayH * 0.74 - Math.min(this.overlayW, this.overlayH) * 0.06 * 1.1
        })
        .hitTestBehavior(HitTestMode.None)
      }

      // All touch zones
      ForEach(this.zones, (el: TouchZone) => {
        if (el.kind === 'dpad') {
          this.DpadArmView(el)
        } else if (el.kind === 'a') {
          this.CircleButtonView(el, COLOR_A_RED, COLOR_A_DARK, 'A')
        } else if (el.kind === 'b') {
          this.CircleButtonView(el, COLOR_B_YELLOW, COLOR_B_DARK, 'B')
        } else if (el.kind === 'qblock') {
          this.QBlockView(el)
        }
      }, (el: TouchZone) => el.id)

      // Pause overlay
      if (this.paused) {
        Column() {
          Text('PAUSED')
            .fontSize(32)
            .fontColor(COLOR_TEXT_WHITE)
            .fontWeight(FontWeight.Bold)
            .margin({ bottom: 8 })
          Text('游戏已暂停')
            .fontSize(16)
            .fontColor('#CCFFFFFF')
            .margin({ bottom: 30 })

          Row() {
            // Resume button - green
            Stack() {
              Rect()
                .width(120)
                .height(50)
                .fill('#FF43B047')
                .stroke(COLOR_PIXEL_BORDER)
                .strokeWidth(3)
              Text('继续')
                .fontSize(18)
                .fontColor(COLOR_TEXT_WHITE)
                .fontWeight(FontWeight.Bold)
            }
            .width(120)
            .height(50)
            .hitTestBehavior(HitTestMode.Default)
            .onTouch((event: TouchEvent) => {
              if (event.type === TouchType.Down) {
                this.togglePause();
              }
            })

            // Exit button - dark red
            Stack() {
              Rect()
                .width(120)
                .height(50)
                .fill('#FF8B0000')
                .stroke(COLOR_PIXEL_BORDER)
                .strokeWidth(3)
              Text('退出')
                .fontSize(18)
                .fontColor(COLOR_TEXT_WHITE)
                .fontWeight(FontWeight.Bold)
            }
            .width(120)
            .height(50)
            .margin({ left: 24 })
            .hitTestBehavior(HitTestMode.Default)
            .onTouch((event: TouchEvent) => {
              if (event.type === TouchType.Down) {
                this.exitToHome();
              }
            })
          }
        }
        .width('100%')
        .height('100%')
        .justifyContent(FlexAlign.Center)
        .backgroundColor('#C0101418')
        .hitTestBehavior(HitTestMode.Default)
        .onTouch((event: TouchEvent) => {
          event.stopPropagation();
        })
      }
    }
    .width('100%')
    .height('100%')
    .hitTestBehavior(this.paused ? HitTestMode.Default : HitTestMode.Block)
    .onTouch((event: TouchEvent): void => {
      this.handleTouch(event);
    })
    .onAreaChange((_oldValue: Area, newValue: Area) => {
      const w = Number(newValue.width);
      const h = Number(newValue.height);
      if (w !== this.overlayW || h !== this.overlayH) {
        this.overlayW = w;
        this.overlayH = h;
        this.buildZones();
      }
    })
  }
}
