import { ExtensionContext } from "@foxglove/extension";
import { initAMSPanel } from "./StepViewerPanel";

export function activate(extensionContext: ExtensionContext): void {
  extensionContext.registerPanel({
    name: "step-viewer",
    initPanel: initAMSPanel,
  });
}