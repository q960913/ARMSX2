// The NAPI surface of libarmsx2.so (entry/src/main/cpp/ohos_main.cpp).
import { resourceManager } from '@kit.LocalizationKit';

/** Unpacks resources and starts the emulator. Returns '' on success, else the error. */
export const init: (resMgr: resourceManager.ResourceManager, filesDir: string) => string;
/** True once a game runs on the JIT (writable code memory was granted). */
export const hasJit: () => boolean;
/** A DualShock 2 input: see PadInput in components/PadInput.ets. value 0..1. */
export const padInput: (port: number, input: number, value: number) => void;
export const openPauseMenu: () => void;
export const setForeground: (foreground: boolean) => void;
export const refreshGameList: () => void;
export const bootFile: (path: string) => void;
export const getBiosDir: () => string;
export const getGamesDir: () => string;
