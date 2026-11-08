import armsx2 from 'libarmsx2.so';
import { PadInput } from './PadInput';

// An on-screen DualShock 2, drawn over the game surface. Each control reports
// press/release straight to the core; the left stick reports its deflection.

@Component
struct PadButton {
  label: string = '';
  input: PadInput = PadInput.CROSS;
  diameter: number = 56;
  @State pressed: boolean = false;

  build() {
    Text(this.label)
      .width(this.diameter)
      .height(this.diameter)
      .borderRadius(this.diameter / 2)
      .textAlign(TextAlign.Center)
      .fontSize(this.diameter * 0.4)
      .fontColor(Color.White)
      .backgroundColor(this.pressed ? '#AAFFFFFF' : '#44FFFFFF')
      .onTouch((event: TouchEvent) => {
        if (event.type === TouchType.Down) {
          this.pressed = true;
          armsx2.padInput(0, this.input, 1);
        } else if (event.type === TouchType.Up || event.type === TouchType.Cancel) {
          this.pressed = false;
          armsx2.padInput(0, this.input, 0);
        }
      })
  }
}

@Component
struct ShoulderButton {
  label: string = '';
  input: PadInput = PadInput.L1;
  @State pressed: boolean = false;

  build() {
    Text(this.label)
      .width(72)
      .height(36)
      .borderRadius(8)
      .textAlign(TextAlign.Center)
      .fontSize(16)
      .fontColor(Color.White)
      .backgroundColor(this.pressed ? '#AAFFFFFF' : '#44FFFFFF')
      .onTouch((event: TouchEvent) => {
        if (event.type === TouchType.Down) {
          this.pressed = true;
          armsx2.padInput(0, this.input, 1);
        } else if (event.type === TouchType.Up || event.type === TouchType.Cancel) {
          this.pressed = false;
          armsx2.padInput(0, this.input, 0);
        }
      })
  }
}

@Component
struct AnalogStick {
  radius: number = 60;
  @State knobX: number = 0;
  @State knobY: number = 0;

  private report(dx: number, dy: number): void {
    const r = this.radius;
    const len = Math.sqrt(dx * dx + dy * dy);
    const scale = len > r ? r / len : 1;
    this.knobX = dx * scale;
    this.knobY = dy * scale;
    const nx = this.knobX / r;
    const ny = this.knobY / r;
    armsx2.padInput(0, PadInput.L_RIGHT, Math.max(nx, 0));
    armsx2.padInput(0, PadInput.L_LEFT, Math.max(-nx, 0));
    armsx2.padInput(0, PadInput.L_DOWN, Math.max(ny, 0));
    armsx2.padInput(0, PadInput.L_UP, Math.max(-ny, 0));
  }

  build() {
    Stack() {
      Circle({ width: this.radius * 0.9, height: this.radius * 0.9 })
        .fill('#88FFFFFF')
        .translate({ x: this.knobX, y: this.knobY })
    }
    .width(this.radius * 2)
    .height(this.radius * 2)
    .borderRadius(this.radius)
    .backgroundColor('#33FFFFFF')
    .onTouch((event: TouchEvent) => {
      const t = event.touches[0];
      if (event.type === TouchType.Down || event.type === TouchType.Move) {
        this.report(t.x - this.radius, t.y - this.radius);
      } else if (event.type === TouchType.Up || event.type === TouchType.Cancel) {
        this.report(0, 0);
      }
    })
  }
}

@Component
export struct VirtualPad {
  build() {
    Stack() {
      // Shoulders
      Row() {
        Column({ space: 8 }) {
          ShoulderButton({ label: 'L2', input: PadInput.L2 })
          ShoulderButton({ label: 'L1', input: PadInput.L1 })
        }
        Blank()
        Column({ space: 8 }) {
          ShoulderButton({ label: 'R2', input: PadInput.R2 })
          ShoulderButton({ label: 'R1', input: PadInput.R1 })
        }
      }
      .width('100%')
      .padding({ left: 24, right: 24, top: 16 })
      .position({ x: 0, y: 0 })

      // D-pad and left stick
      Column({ space: 16 }) {
        Grid() {
          GridItem() {}
          GridItem() { PadButton({ label: '▲', input: PadInput.UP, diameter: 48 }) }
          GridItem() {}
          GridItem() { PadButton({ label: '◀', input: PadInput.LEFT, diameter: 48 }) }
          GridItem() {}
          GridItem() { PadButton({ label: '▶', input: PadInput.RIGHT, diameter: 48 }) }
          GridItem() {}
          GridItem() { PadButton({ label: '▼', input: PadInput.DOWN, diameter: 48 }) }
          GridItem() {}
        }
        .columnsTemplate('1fr 1fr 1fr')
        .rowsTemplate('1fr 1fr 1fr')
        .width(150)
        .height(150)

        AnalogStick()
      }
      .position({ left: 24, bottom: 16 })

      // Face buttons
      Grid() {
        GridItem() {}
        GridItem() { PadButton({ label: '△', input: PadInput.TRIANGLE }) }
        GridItem() {}
        GridItem() { PadButton({ label: '□', input: PadInput.SQUARE }) }
        GridItem() {}
        GridItem() { PadButton({ label: '○', input: PadInput.CIRCLE }) }
        GridItem() {}
        GridItem() { PadButton({ label: '✕', input: PadInput.CROSS }) }
        GridItem() {}
      }
      .columnsTemplate('1fr 1fr 1fr')
      .rowsTemplate('1fr 1fr 1fr')
      .width(180)
      .height(180)
      .position({ right: 24, bottom: 40 })

      // Select / Start
      Row({ space: 24 }) {
        ShoulderButton({ label: 'SELECT', input: PadInput.SELECT })
        ShoulderButton({ label: 'START', input: PadInput.START })
      }
      .position({ left: '50%', bottom: 16 })
      .translate({ x: '-50%' })
    }
    .width('100%')
    .height('100%')
    .hitTestBehavior(HitTestMode.Transparent)
  }
}
