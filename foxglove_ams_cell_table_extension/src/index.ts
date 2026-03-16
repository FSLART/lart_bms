import { ExtensionContext } from "@foxglove/extension";

import { initAmsCellTablePanel } from "./AmsCellTablePanel";

export function activate(extensionContext: ExtensionContext): void {
  extensionContext.registerPanel({
    name: "ams-cell-table-panel",
    initPanel: initAmsCellTablePanel,
  });
}
