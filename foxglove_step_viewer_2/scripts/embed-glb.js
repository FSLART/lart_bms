const fs = require("fs");
const path = require("path");

const input = path.join(__dirname, "..", "src", "MODULE_Assembly_V6.glb");
const output = path.join(__dirname, "..", "src", "modelData.ts");

const buffer = fs.readFileSync(input);
const base64 = buffer.toString("base64");

const content = `export const MODEL_URL = "data:model/gltf-binary;base64,${base64}";
`;

fs.writeFileSync(output, content, "utf8");

console.log("Generated src/modelData.ts");