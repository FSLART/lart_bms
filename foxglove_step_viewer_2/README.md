# Foxglove GLB Viewer Extension

A custom Foxglove Studio panel that displays a **3D GLB model** using
Google's `@google/model-viewer` web component.

The model is embedded directly inside the extension bundle so the
extension remains completely self‑contained.

## Features

-   Interactive 3D viewer
-   Mouse orbit / zoom / pan
-   Embedded GLB model (no external hosting required)
-   Works inside Foxglove Studio panels

## Project Structure

    foxglove_3d_object_viewer/
    │
    ├─ package.json
    ├─ tsconfig.json
    ├─ README.md
    ├─ CHANGELOG.md
    │
    ├─ scripts/
    │   └─ embed-glb.js
    │
    └─ src/
        ├─ index.ts
        ├─ StepViewerPanel.tsx
        ├─ custom.d.ts
        └─ MODULE_Assembly_V6.glb

## How it works

During build:

GLB → base64 → modelData.ts → extension.js

The script `scripts/embed-glb.js` reads the GLB model and converts it to
a base64 data URL which is then bundled inside the extension.

## Build

Install dependencies:

npm install

Build and install the extension locally:

npm run local-install

Foxglove Studio will automatically detect the extension.

## Controls

Mouse:

-   Left drag → rotate model
-   Scroll → zoom
-   Right drag → pan

## Notes

Embedding the GLB increases bundle size. A 15 MB model may result in a
\~60‑70 MB extension bundle because base64 encoding increases size.
