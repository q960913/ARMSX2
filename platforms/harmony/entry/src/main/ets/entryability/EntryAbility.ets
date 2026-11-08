import { AbilityConstant, UIAbility, Want } from '@kit.AbilityKit';
import { hilog } from '@kit.PerformanceAnalysisKit';
import { window } from '@kit.ArkUI';
import armsx2 from 'libarmsx2.so';

const DOMAIN = 0x3200;
const TAG = 'ARMSX2';

export default class EntryAbility extends UIAbility {
  onCreate(want: Want, launchParam: AbilityConstant.LaunchParam): void {
    hilog.info(DOMAIN, TAG, 'EntryAbility onCreate');
  }

  onWindowStageCreate(windowStage: window.WindowStage): void {
    windowStage.loadContent('pages/Index', (err) => {
      if (err.code) {
        hilog.error(DOMAIN, TAG, 'loadContent failed: %{public}s', JSON.stringify(err));
        return;
      }
      // Edge to edge, no system bars, screen kept on: it is a game.
      const win = windowStage.getMainWindowSync();
      win.setWindowLayoutFullScreen(true);
      win.setWindowSystemBarEnable([]);
      win.setWindowKeepScreenOn(true);
    });
  }

  onForeground(): void {
    armsx2.setForeground(true);
  }

  onBackground(): void {
    armsx2.setForeground(false);
  }
}
