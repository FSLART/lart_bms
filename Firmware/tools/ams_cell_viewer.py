"""AMS cell voltage viewer via SLCAN.

    pip install python-can pyserial
    python ams_cell_viewer.py          # GUI
    python ams_cell_viewer.py --test   # self-check do decode

Layout das frames (powertrain_t26.dbc):
  Slave_NN_Voltage_ID_1..3 = 0x600 + 7*(NN-1) + 0..2, 4 celulas u16 LE, 1 mV/bit
  Master_MSC_ID_4 (0x706): byte1 = bal_bitmask_slave_id (1-based),
                           bytes2-3 = bal_bitmask u16 LE, bit i = celula i+1
"""
import struct
import sys
import threading
import time
import tkinter as tk
from tkinter import ttk

SLAVES, CELLS = 12, 12
OV, UV = 4.20, 2.80      # SAFETY_CELL_OV_V / SAFETY_CELL_UV_V em adbms_to_CAN.c
VMIN, VMAX = 2.7, 4.3    # escala do eixo
STALE = 5.0              # s sem frame -> ignora; > 12 slaves x ~250 ms da 0x706 em roda
F = ('Segoe UI', 8)
COL = {'max': '#ef4444', 'min': '#3b82f6', 'ok': '#22c55e', 'ov': '#f97316', 'uv': '#991b1b', 'bal': '#fde047'}

volts = [None] * (SLAVES * CELLS)
vt = [0.0] * (SLAVES * CELLS)
mask = [0] * SLAVES
mt = [0.0] * SLAVES


def decode(arb_id, data, now):
    off = arb_id - 0x600
    if 0 <= off < 7 * SLAVES and off % 7 < 3 and len(data) >= 8:
        base = off // 7 * CELLS + off % 7 * 4
        for i, mv in enumerate(struct.unpack_from('<4H', data)):
            volts[base + i], vt[base + i] = mv / 1000, now
    elif arb_id == 0x706 and len(data) >= 4 and 1 <= data[1] <= SLAVES:
        mask[data[1] - 1], mt[data[1] - 1] = data[2] | data[3] << 8, now


class App:
    def __init__(self, root):
        self.root, self.bus, self.th, self.msg = root, None, None, 'desligado'
        bar = ttk.Frame(root, padding=6)
        bar.pack(fill='x')
        ttk.Label(bar, text='Probe SLCAN').pack(side='left')
        self.port = ttk.Combobox(bar, width=40, postcommand=self.scan)
        self.port.pack(side='left', padx=4)
        ttk.Label(bar, text='Baud CAN').pack(side='left', padx=(8, 0))
        self.baud = ttk.Combobox(bar, width=9, state='readonly', values=['125000', '250000', '500000', '1000000'])
        self.baud.set('500000')
        self.baud.pack(side='left', padx=4)
        self.btn = ttk.Button(bar, command=self.toggle)
        self.btn.pack(side='left', padx=8)
        self.status = ttk.Label(bar)
        self.status.pack(side='left')
        self.cv = tk.Canvas(root, bg='white', highlightthickness=0)
        self.cv.pack(fill='both', expand=True)
        self.scan()
        self.draw()

    def scan(self):
        from serial.tools import list_ports
        self.port['values'] = [f'{p.device}  {p.description}' for p in list_ports.comports()]
        if not self.port.get() and self.port['values']:
            self.port.current(0)

    def toggle(self):
        if self.bus:
            self.bus = None     # o reader sai do loop e fecha o bus
            self.th.join()
            self.msg = 'desligado'
            return
        try:
            import can
            bus = can.Bus(interface='slcan', channel=self.port.get().split()[0], bitrate=int(self.baud.get()))
        except Exception as e:
            self.msg = f'erro: {e}'
            return
        self.bus, self.msg = bus, f'ligado {self.port.get().split()[0]} @ {self.baud.get()}'
        self.th = threading.Thread(target=self.reader, args=(bus,), daemon=True)
        self.th.start()

    def reader(self, bus):
        try:
            while self.bus is bus:
                m = bus.recv(0.2)
                if m and not m.is_extended_id:
                    decode(m.arbitration_id, m.data, time.monotonic())
        except Exception as e:
            self.bus, self.msg = None, f'erro: {e}'
        finally:
            bus.shutdown()

    def draw(self):
        self.btn['text'] = 'Desligar' if self.bus else 'Ligar'
        self.status['text'] = self.msg
        now, cv = time.monotonic(), self.cv
        cv.delete('all')
        W, H = cv.winfo_width(), cv.winfo_height()
        L, R, T, B = 50, 28, 40, 60
        pw, ph = W - L - R, H - T - B
        n = SLAVES * CELLS
        bw = pw / n
        y = lambda v: T + ph * (VMAX - min(max(v, VMIN), VMAX)) / (VMAX - VMIN)
        name = lambda i: f'S{i // CELLS + 1:02d} C{i % CELLS + 1}'

        live = [v if v is not None and now - t < STALE else None for v, t in zip(volts, vt)]
        idx = [i for i in range(n) if live[i] is not None]
        imax = max(idx, key=live.__getitem__, default=None)
        imin = min(idx, key=live.__getitem__, default=None)
        bal = [now - mt[i // CELLS] < STALE and mask[i // CELLS] >> i % CELLS & 1 for i in range(n)]

        head = f'AMS Cell Voltage   {len(idx)}/{n} celulas   {sum(bal)} a descarregar'
        if idx:
            head += (f'   max {live[imax]:.3f} V ({name(imax)})   min {live[imin]:.3f} V ({name(imin)})'
                     f'   delta {(live[imax] - live[imin]) * 1000:.0f} mV')
        cv.create_text(L, 18, text=head, anchor='w', font=('Segoe UI', 11, 'bold'), fill='#111')

        for k in range(9):
            v = VMIN + k * (VMAX - VMIN) / 8
            cv.create_line(L, y(v), W - R, y(v), fill='#e5e7eb')
            cv.create_text(L - 4, y(v), text=f'{v:.2f}', anchor='e', font=F, fill='#6b7280')
        for i in range(n):
            x0 = L + i * bw
            if bal[i]:
                cv.create_rectangle(x0, T, x0 + bw, T + ph, fill=COL['bal'], outline='')
            v = live[i]
            if v is None:
                continue
            c = 'ov' if v > OV else 'uv' if v < UV else 'max' if i == imax else 'min' if i == imin else 'ok'
            cv.create_rectangle(x0 + 1, y(v), x0 + bw - 1, T + ph, fill=COL[c], outline='')
        for lim, c, txt in ((OV, 'ov', 'OV'), (UV, 'uv', 'UV')):
            cv.create_line(L, y(lim), W - R, y(lim), fill=COL[c], dash=(4, 3))
            cv.create_text(W - R + 3, y(lim), text=txt, anchor='w', font=F, fill=COL[c])
        for s in range(SLAVES + 1):
            x = L + s * CELLS * bw
            cv.create_line(x, T, x, T + ph, fill='#374151' if s % 2 == 0 else '#9ca3af', width=2 if s % 2 == 0 else 1)
            if s < SLAVES:
                cv.create_text(x + CELLS * bw / 2, T + ph + 10, text=f'S{s + 1:02d}', font=F, fill='#6b7280')
            if s % 2 == 0 and s < SLAVES:
                cv.create_text(x + CELLS * bw, T + ph + 26, text=f'Module {s // 2 + 1}', font=('Segoe UI', 9, 'bold'))

        x = L
        for c, txt in (('max', 'Max'), ('min', 'Min'), ('ok', 'Normal'), ('ov', 'Overvoltage'),
                       ('uv', 'Undervoltage'), ('bal', 'A descarregar (bal)')):
            cv.create_rectangle(x, H - 16, x + 10, H - 6, fill=COL[c], outline='#9ca3af')
            x = cv.bbox(cv.create_text(x + 14, H - 11, text=txt, anchor='w', font=F, fill='#374151'))[2] + 16
        self.root.after(200, self.draw)


def selftest():
    decode(0x600 + 7 * 2 + 1, struct.pack('<4H', 3700, 3701, 3702, 3703), 1.0)   # S03 celulas 5-8
    assert volts[2 * CELLS + 4] == 3.7 and volts[2 * CELLS + 7] == 3.703
    decode(0x600 + 7 * 2 + 3, struct.pack('<4H', 1, 2, 3, 4), 1.0)               # Temperature_ID_1 -> ignora
    assert volts[2 * CELLS + 8] is None
    decode(0x706, bytes([3, 12, 0x05, 0x08, 0, 0, 0, 0]), 1.0)                   # S12 celulas 1, 3, 12
    decode(0x706, bytes([3, 0, 0xFF, 0xFF, 0, 0, 0, 0]), 1.0)                    # slave 0 -> ignora
    assert mask == [0] * 11 + [0x0805]
    print('ok')


if __name__ == '__main__':
    if '--test' in sys.argv:
        selftest()
    else:
        root = tk.Tk()
        root.title('AMS Cell Voltage')
        root.geometry('1400x650')
        App(root)
        root.mainloop()
