import { common } from '@kit.AbilityKit';
import { fileIo as fs, picker } from '@kit.CoreFileKit';
import { KeyCode } from '@kit.InputKit';
import { promptAction } from '@kit.ArkUI';
import armsx2 from 'libarmsx2.so';
import { PadInput } from '../components/PadInput';
import { VirtualPad } from '../components/VirtualPad';

// The whole app is one screen: the emulator's own UI (PCSX2's Big Picture mode) is
// drawn into the XComponent below by the core. This page adds the on-screen pad, a
// small toolbar for importing files, and physical controller keys.

// Physical controller buttons -> DualShock 2.
const KEY_MAP: Map<number, PadInput> = new Map<number, PadInput>();
KEY_MAP.set(KeyCode.KEYCODE_BUTTON_A, PadInput.CROSS);
KEY_MAP.set(KeyCode.KEYCODE_BUTTON_B, PadInput.CIRCLE);
KEY_MAP.set(KeyCode.KEYCODE_BUTTON_X, PadInput.SQUARE);
KEY_MAP.set(KeyCode.KEYCODE_BUTTON_Y, PadInput.TRIANGLE);
KEY_MAP.set(KeyCode.KEYCODE_BUTTON_L1, PadInput.L1);
KEY_MAP.set(KeyCode.KEYCODE_BUTTON_R1, PadInput.R1);
KEY_MAP.set(KeyCode.KEYCODE_BUTTON_L2, PadInput.L2);
KEY_MAP.set(KeyCode.KEYCODE_BUTTON_R2, PadInput.R2);
KEY_MAP.set(KeyCode.KEYCODE_BUTTON_SELECT, PadInput.SELECT);
KEY_MAP.set(KeyCode.KEYCODE_BUTTON_START, PadInput.START);
KEY_MAP.set(KeyCode.KEYCODE_BUTTON_THUMBL, PadInput.L3);
KEY_MAP.set(KeyCode.KEYCODE_BUTTON_THUMBR, PadInput.R3);
KEY_MAP.set(KeyCode.KEYCODE_DPAD_UP, PadInput.UP);
KEY_MAP.set(KeyCode.KEYCODE_DPAD_DOWN, PadInput.DOWN);
KEY_MAP.set(KeyCode.KEYCODE_DPAD_LEFT, PadInput.LEFT);
KEY_MAP.set(KeyCode.KEYCODE_DPAD_RIGHT, PadInput.RIGHT);

async function importFiles(context: common.UIAbilityContext, destDir: string): Promise<number> {
  const documentPicker = new picker.DocumentViewPicker(context);
  const options = new picker.DocumentSelectOptions();
  options.maxSelectNumber = 50;
  const uris: string[] = await documentPicker.select(options);

  let copied = 0;
  for (const uri of uris) {
    const name = decodeURIComponent(uri.substring(uri.lastIndexOf('/') + 1));
    const src = fs.openSync(uri, fs.OpenMode.READ_ONLY);
    try {
      await fs.copyFile(src.fd, `${destDir}/${name}`);
      copied++;
    } finally {
      fs.closeSync(src);
    }
  }
  return copied;
}

@Entry
@Component
struct Index {
  @State error: string = '';
  @State showPad: boolean = true;
  @State busy: string = '';
  private context: common.UIAbilityContext = getContext(this) as common.UIAbilityContext;

  aboutToAppear(): void {
    // Unpacks the bundled resources on the first launch of a build, then starts the
    // core in Big Picture mode. The XComponent's surface arrives on its own.
    this.error = armsx2.init(this.context.resourceManager, this.context.filesDir);
  }

  onBackPress(): boolean {
    armsx2.openPauseMenu();
    return true;
  }

  private async runImport(what: string, destDir: string): Promise<void> {
    try {
      this.busy = `Importing ${what}...`;
      const n = await importFiles(this.context, destDir);
      this.busy = '';
      if (n > 0) {
        armsx2.refreshGameList();
        promptAction.showToast({ message: `Imported ${n} file(s)` });
      }
    } catch (e) {
      this.busy = '';
      promptAction.showToast({ message: `Import failed: ${JSON.stringify(e)}` });
    }
  }

  @Builder
  ToolbarButton(label: string, action: () => void) {
    Text(label)
      .fontSize(13)
      .fontColor(Color.White)
      .padding({ left: 10, right: 10, top: 6, bottom: 6 })
      .borderRadius(14)
      .backgroundColor('#55000000')
      .onClick(action)
  }

  build() {
    Stack() {
      XComponent({ id: 'armsx2_gs', type: XComponentType.SURFACE, libraryname: 'armsx2' })
        .width('100%')
        .height('100%')

      if (this.showPad && this.error === '') {
        VirtualPad()
      }

      Row({ space: 8 }) {
        this.ToolbarButton('Menu', () => {
          armsx2.openPauseMenu();
        })
        this.ToolbarButton(this.showPad ? 'Hide pad' : 'Show pad', () => {
          this.showPad = !this.showPad;
        })
        this.ToolbarButton('Import BIOS', () => {
          this.runImport('BIOS', armsx2.getBiosDir());
        })
        this.ToolbarButton('Import games', () => {
          this.runImport('games', armsx2.getGamesDir());
        })
      }
      .position({ left: '50%', top: 8 })
      .translate({ x: '-50%' })

      if (this.busy !== '') {
        Text(this.busy)
          .fontColor(Color.White)
          .backgroundColor('#AA000000')
          .padding(16)
          .borderRadius(8)
      }

      if (this.error !== '') {
        Text(`ARMSX2 could not start:\n${this.error}`)
          .fontColor(Color.White)
          .backgroundColor('#CC800000')
          .padding(16)
          .borderRadius(8)
          .margin(32)
      }
    }
    .width('100%')
    .height('100%')
    .backgroundColor(Color.Black)
    .focusable(true)
    .defaultFocus(true)
    .onKeyEvent((event: KeyEvent) => {
      if (event.keyCode === KeyCode.KEYCODE_BUTTON_MODE && event.type === KeyType.Down) {
        armsx2.openPauseMenu();
        return;
      }
      const input = KEY_MAP.get(event.keyCode);
      if (input === undefined) {
        return;
      }
      if (event.type === KeyType.Down) {
        armsx2.padInput(0, input, 1);
      } else if (event.type === KeyType.Up) {
        armsx2.padInput(0, input, 0);
      }
    })
  }
}
