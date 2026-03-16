import { Immutable, MessageEvent, PanelExtensionContext } from "@foxglove/extension";
import { ReactElement, useEffect, useLayoutEffect, useMemo, useState } from "react";
import { createRoot } from "react-dom/client";

type CellValue = number | null;

interface VoltageRow {
  Slave?: string;
  [key: string]: string | number | null | undefined;
}

interface TableMessage {
  rows?: VoltageRow[];
}

const TABLE_TOPIC = "can/AMS_Cell_Table";
const NUM_SLAVES = 12;
const NUM_CELLS = 12;

const panelStyle: React.CSSProperties = {
  boxSizing: "border-box",
  width: "100%",
  height: "100%",
  padding: "12px",
  overflow: "auto",
  background: "var(--foxglove-background, var(--bg-primary))",
  color: "var(--foxglove-text-primary, var(--text-primary))",
  fontFamily: "Inter, system-ui, sans-serif",
};

const headerBarStyle: React.CSSProperties = {
  display: "flex",
  alignItems: "center",
  justifyContent: "space-between",
  marginBottom: "12px",
  gap: "12px",
  flexWrap: "wrap",
};

const titleStyle: React.CSSProperties = {
  fontSize: "18px",
  fontWeight: 700,
  margin: 0,
};

const metaStyle: React.CSSProperties = {
  fontSize: "12px",
  color: "var(--foxglove-text-secondary, var(--text-secondary))",
};

const tableStyle: React.CSSProperties = {
  width: "100%",
  borderCollapse: "collapse",
  tableLayout: "fixed",
  fontSize: "12px",
};

const thStyle: React.CSSProperties = {
  position: "sticky",
  top: 0,
  zIndex: 2,
  background: "var(--foxglove-background, #111)",
  borderBottom: "1px solid rgba(127, 127, 127, 0.35)",
  padding: "8px 6px",
  textAlign: "center",
  fontWeight: 700,
  whiteSpace: "nowrap",
};

const firstColHeaderStyle: React.CSSProperties = {
  ...thStyle,
  left: 0,
  zIndex: 3,
  textAlign: "left",
};

const rowHeaderStyle: React.CSSProperties = {
  position: "sticky",
  left: 0,
  zIndex: 1,
  background: "var(--foxglove-background, #111)",
  borderBottom: "1px solid rgba(127, 127, 127, 0.20)",
  padding: "8px 10px",
  textAlign: "left",
  fontWeight: 600,
  whiteSpace: "nowrap",
};

const emptyCellStyle: React.CSSProperties = {
  borderBottom: "1px solid rgba(127, 127, 127, 0.20)",
  padding: "8px 6px",
  textAlign: "center",
  color: "var(--foxglove-text-secondary, var(--text-secondary))",
  fontVariantNumeric: "tabular-nums",
};

function formatValue(value: CellValue): string {
  if (value == null || Number.isNaN(value)) {
    return "—";
  }
  return value.toFixed(3);
}

function getCellValue(row: VoltageRow, cellIndex: number): CellValue {
  const raw = row[`Cell ${String(cellIndex).padStart(2, "0")}`];
  return typeof raw === "number" ? raw : null;
}

function buildFallbackRows(): VoltageRow[] {
  return Array.from({ length: NUM_SLAVES }, (_, slaveIndex) => {
    const row: VoltageRow = { Slave: `Slave ${String(slaveIndex + 1).padStart(2, "0")}` };
    for (let cellIndex = 1; cellIndex <= NUM_CELLS; cellIndex += 1) {
      row[`Cell ${String(cellIndex).padStart(2, "0")}`] = null;
    }
    return row;
  });
}

function AmsCellTablePanel({ context }: { context: PanelExtensionContext }): ReactElement {
  const [messages, setMessages] = useState<Immutable<MessageEvent[]> | undefined>();
  const [renderDone, setRenderDone] = useState<(() => void) | undefined>();

  useLayoutEffect(() => {
    context.onRender = (renderState, done) => {
      setRenderDone(() => done);
      if (renderState.currentFrame) {
        setMessages(renderState.currentFrame);
      }
    };

    context.watch("currentFrame");
  }, [context]);

  useLayoutEffect(() => {
    context.subscribe([{ topic: TABLE_TOPIC }]);
  }, [context]);

  useEffect(() => {
    renderDone?.();
  }, [renderDone, messages]);

  const latestTableMessage = useMemo(() => {
    const latestForTopic = messages
      ?.slice()
      .reverse()
      .find((messageEvent) => messageEvent.topic === TABLE_TOPIC);

    return (latestForTopic?.message as TableMessage | undefined) ?? undefined;
  }, [messages]);

  const rows = useMemo(() => {
    const incomingRows = latestTableMessage?.rows;
    if (!incomingRows || !Array.isArray(incomingRows) || incomingRows.length === 0) {
      return buildFallbackRows();
    }

    const paddedRows = [...incomingRows];
    while (paddedRows.length < NUM_SLAVES) {
      paddedRows.push({ Slave: `Slave ${String(paddedRows.length + 1).padStart(2, "0")}` });
    }

    return paddedRows.slice(0, NUM_SLAVES);
  }, [latestTableMessage]);

  const populatedCellCount = useMemo(() => {
    let count = 0;
    for (const row of rows) {
      for (let cellIndex = 1; cellIndex <= NUM_CELLS; cellIndex += 1) {
        if (getCellValue(row, cellIndex) != null) {
          count += 1;
        }
      }
    }
    return count;
  }, [rows]);

  return (
    <div style={panelStyle}>
      <div style={headerBarStyle}>
        <div>
          <h2 style={titleStyle}>AMS Cell Table</h2>
          <div style={metaStyle}>Topic: {TABLE_TOPIC}</div>
        </div>
        <div style={metaStyle}>{populatedCellCount}/{NUM_SLAVES * NUM_CELLS} cells populated</div>
      </div>

      <table style={tableStyle}>
        <thead>
          <tr>
            <th style={firstColHeaderStyle}>Slave</th>
            {Array.from({ length: NUM_CELLS }, (_, index) => (
              <th key={index} style={thStyle}>
                Cell {String(index + 1).padStart(2, "0")}
              </th>
            ))}
          </tr>
        </thead>
        <tbody>
          {rows.map((row, rowIndex) => (
            <tr key={rowIndex}>
              <th style={rowHeaderStyle}>{row.Slave ?? `Slave ${String(rowIndex + 1).padStart(2, "0")}`}</th>
              {Array.from({ length: NUM_CELLS }, (_, cellOffset) => {
                const value = getCellValue(row, cellOffset + 1);
                return (
                  <td key={cellOffset} style={emptyCellStyle}>
                    {formatValue(value)}
                  </td>
                );
              })}
            </tr>
          ))}
        </tbody>
      </table>
    </div>
  );
}

export function initAmsCellTablePanel(context: PanelExtensionContext): () => void {
  const root = createRoot(context.panelElement);
  root.render(<AmsCellTablePanel context={context} />);

  return () => {
    root.unmount();
  };
}
