import { PanelExtensionContext } from "@foxglove/extension";
import React, { ReactElement } from "react";
import { createRoot } from "react-dom/client";
import "@google/model-viewer";
import { MODEL_URL } from "./modelData";

declare global {
  namespace JSX {
    interface IntrinsicElements {
      "model-viewer": React.DetailedHTMLProps<
        React.HTMLAttributes<HTMLElement>,
        HTMLElement
      > & {
        src?: string;
        alt?: string;
        "camera-controls"?: boolean;
        "auto-rotate"?: boolean;
        "shadow-intensity"?: string | number;
        "environment-image"?: string;
        exposure?: string | number;
        "interaction-prompt"?: string;
        "camera-orbit"?: string;
        orientation?: string;
        style?: React.CSSProperties;
      };
    }
  }
}

function StepViewerPanel(): ReactElement {
  console.log("3D VIEWER modelUrl =", MODEL_URL.slice(0, 64) + "...");

  return (
    <div
      style={{
        width: "100%",
        height: "100%",
        margin: 0,
        padding: 0,
        overflow: "hidden",
        background: "#111",
      }}
    >
      <model-viewer
        src={MODEL_URL}
        alt="Battery Module"
        camera-controls
        shadow-intensity="2"
        exposure="0.4"
        environment-image="neutral"
        interaction-prompt="none"
		orientation="0deg 90deg 0deg"	
        camera-orbit="-35deg 80deg 0"
        style={{
          width: "100%",
          height: "100%",
          display: "block",
          background: "transparent",
        }}
      />
    </div>
  );
}

export function initAMSPanel(context: PanelExtensionContext): () => void {
  const root = createRoot(context.panelElement);
  root.render(<StepViewerPanel />);
  return () => root.unmount();
}