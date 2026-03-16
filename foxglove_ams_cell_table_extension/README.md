# Foxglove AMS Cell Table Panel

This custom Foxglove panel subscribes directly to:

- `can/AMS_Cell_Table`

and renders a plain HTML table with:

- 12 rows (`Slave 01` to `Slave 12`)
- 12 columns (`Cell 01` to `Cell 12`)
- values taken from the already-processed `rows` payload coming from your Python bridge

## Expected message shape

```json
{
  "rows": [
    {
      "Slave": "Slave 01",
      "Cell 01": 3.254,
      "Cell 02": 3.251,
      "Cell 03": 3.248,
      "Cell 04": 3.249
    }
  ]
}
```

## Files

- `src/AmsCellTablePanel.tsx`
- `src/index.ts`

## Notes

- No dropdowns in the panel UI
- Topic is hardcoded to `can/AMS_Cell_Table`
- Empty cells render as `—`
- Values render with 3 decimals
