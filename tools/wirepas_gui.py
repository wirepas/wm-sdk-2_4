#!/usr/bin/env python3
"""
wirepas_gui.py  —  Qt GUI for Wirepas UART (WAPS) console

Requires:  pip install pyserial PySide6
Optional:  pip install wirepas-mqtt-library   (for MQTT/MQTTS backend transport)
Usage:     python tools/wirepas_gui.py [-p PORT] [-b BAUD]
"""

import argparse
import csv
import json
import struct
import sys
import time
from collections import deque
from typing import Optional

try:
    from matplotlib.figure import Figure
    from matplotlib.backends.backend_qtagg import FigureCanvasQTAgg
    _MATPLOTLIB_OK = True
except ImportError:
    _MATPLOTLIB_OK = False

try:
    from PySide6.QtCore import (
        Qt, QObject, Signal, QTimer, QAbstractTableModel, QModelIndex,
        QSortFilterProxyModel,
    )
    from PySide6.QtGui import QColor, QFont, QAction
    from PySide6.QtWidgets import (
        QApplication, QMainWindow, QWidget, QVBoxLayout, QHBoxLayout,
        QFormLayout, QLabel, QLineEdit, QPushButton,
        QComboBox, QSpinBox, QDoubleSpinBox, QGroupBox, QTabWidget, QTableView,
        QHeaderView, QTextEdit, QSplitter, QStatusBar, QScrollArea,
        QCheckBox, QFileDialog, QProgressBar, QSizePolicy, QDialog, QMessageBox,
    )
    from PySide6.QtSerialPort import QSerialPortInfo
except ImportError:
    print("Missing dependency:  pip install PySide6 pyserial")
    sys.exit(1)

try:
    import serial.tools.list_ports
except ImportError:
    pass

# Import protocol layer from sibling module
import os
sys.path.insert(0, os.path.dirname(__file__))
from wirepas_uart import (
    WapsConn, FC, CSAP, NodeRole, _ROLE_NAMES,
    cmd_send, cmd_appconfig_read, cmd_appconfig_write,
    csap_attr_read, csap_attr_write, cmd_node_info, cmd_node_configure,
    cmd_stack_start, cmd_stack_stop, cmd_poll,
    cmd_otap_status, cmd_otap_clear, cmd_otap_upload, cmd_otap_target, cmd_otap_target_read,
    cmd_remote_scratchpad_status_req, parse_remote_scratchpad_status,
    REMOTE_API_RSP_SRC_EP, REMOTE_API_RSP_DST_EP,
    _SCRATCH_ACTION_NAMES,
    cmd_remote_configure,
    cmd_remote_read_csap, parse_remote_csap_read, decode_csap_role,
    parse_rx_ind, parse_rx_frag_ind, FragReassembler, cbor_to_str,
    cbor_diag_to_str, parse_diag_packet, DIAG_SRC_EP, DIAG_DST_EP,
    ADDR_BROADCAST, ADDR_MCAST_BIT, parse_adc_cbor,
)
from wirepas_mqtt import MqttConn, mqtt_lib_available


# ─── Colour palette ────────────────────────────────────────────────────────────
COL_BCAST = "#d4a017"
COL_MCAST = "#9b59b6"
COL_UCAST = "#1565c0"
COL_TX    = "#27ae60"
COL_ERR   = "#e74c3c"
COL_DIM   = "#888888"
COL_BG    = "#1e1e1e"
COL_BG2   = "#252526"
COL_FG    = "#d4d4d4"
COL_SEL   = "#264f78"
COL_BORDER= "#3c3c3c"


STYLESHEET = f"""
QWidget {{
    background-color: {COL_BG};
    color: {COL_FG};
    font-family: "Helvetica Neue", "Segoe UI", Arial, Helvetica;
    font-size: 13px;
}}
QGroupBox {{
    border: 1px solid {COL_BORDER};
    border-radius: 4px;
    margin-top: 10px;
    padding: 6px;
}}
QGroupBox::title {{
    subcontrol-origin: margin;
    left: 8px;
    color: #888;
    font-size: 11px;
    text-transform: uppercase;
    letter-spacing: 1px;
}}
QLineEdit, QComboBox, QSpinBox, QTextEdit {{
    background-color: {COL_BG2};
    border: 1px solid {COL_BORDER};
    border-radius: 3px;
    padding: 3px 6px;
    color: {COL_FG};
    selection-background-color: {COL_SEL};
}}
QLineEdit:focus, QComboBox:focus, QSpinBox:focus {{
    border: 1px solid #5a9fd4;
}}
QPushButton {{
    background-color: #2d5a8e;
    color: white;
    border: none;
    border-radius: 3px;
    padding: 5px 14px;
    font-weight: 600;
}}
QPushButton:hover  {{ background-color: #3a73b5; }}
QPushButton:pressed {{ background-color: #1e3f6b; }}
QPushButton:disabled {{ background-color: #333; color: #666; }}
QPushButton#btn_connect  {{ background-color: #1e6b3a; }}
QPushButton#btn_connect:hover  {{ background-color: #27913f; }}
QPushButton#btn_disconnect {{ background-color: #6b1e1e; }}
QPushButton#btn_disconnect:hover {{ background-color: #912727; }}
QPushButton#btn_send {{ background-color: #1e6b4a; }}
QPushButton#btn_send:hover {{ background-color: #27a06b; }}
QTableView {{
    background-color: {COL_BG2};
    border: 1px solid {COL_BORDER};
    gridline-color: #2a2a2a;
    alternate-background-color: #222222;
    selection-background-color: {COL_SEL};
}}
QHeaderView::section {{
    background-color: #2d2d2d;
    color: #aaa;
    border: none;
    border-right: 1px solid {COL_BORDER};
    border-bottom: 1px solid {COL_BORDER};
    padding: 4px 6px;
    font-size: 11px;
    text-transform: uppercase;
    letter-spacing: 0.5px;
}}
QTabWidget::pane {{ border: 1px solid {COL_BORDER}; }}
QTabBar::tab {{
    background: {COL_BG2};
    color: #888;
    padding: 6px 16px;
    border: 1px solid {COL_BORDER};
    border-bottom: none;
    border-top-left-radius: 3px;
    border-top-right-radius: 3px;
}}
QTabBar::tab:selected {{ background: {COL_BG}; color: {COL_FG}; }}
QStatusBar {{ background-color: {COL_BG2}; border-top: 1px solid {COL_BORDER}; }}
QScrollBar:vertical {{
    background: {COL_BG2}; width: 10px; margin: 0;
}}
QScrollBar::handle:vertical {{
    background: #555; border-radius: 5px; min-height: 20px;
}}
QCheckBox {{ color: {COL_FG}; }}
QCheckBox::indicator {{
    width: 14px; height: 14px;
    border: 1px solid {COL_BORDER}; border-radius: 2px;
    background: {COL_BG2};
}}
QCheckBox::indicator:checked {{ background: #2d5a8e; border-color: #5a9fd4; }}
QLabel#lbl_node_addr {{ font-size: 18px; font-weight: bold; color: #5a9fd4; }}
QLabel#lbl_status_ok  {{ color: #27ae60; font-weight: bold; }}
QLabel#lbl_status_err {{ color: #e74c3c; font-weight: bold; }}
"""


# ─── RX message table model ────────────────────────────────────────────────────
_COLUMNS = ["Time", "Type", "Src addr", "Dst addr", "Src EP", "Dst EP",
            "QoS", "Hops", "Delay ms", "Bytes", "Payload (hex)", "CBOR"]

class RxTableModel(QAbstractTableModel):
    def __init__(self, max_rows: int = 500):
        super().__init__()
        self._rows: list[dict] = []
        self._max_rows = max_rows

    def set_max_rows(self, n: int):
        self._max_rows = max(1, n)
        self._trim()

    def rowCount(self, parent=QModelIndex()):
        return len(self._rows)

    def columnCount(self, parent=QModelIndex()):
        return len(_COLUMNS)

    def headerData(self, section, orientation, role=Qt.ItemDataRole.DisplayRole):
        if orientation == Qt.Orientation.Horizontal and role == Qt.ItemDataRole.DisplayRole:
            return _COLUMNS[section]
        return None

    def data(self, index, role=Qt.ItemDataRole.DisplayRole):
        if not index.isValid() or index.row() >= len(self._rows):
            return None
        row = self._rows[index.row()]
        col = index.column()

        if role == Qt.ItemDataRole.DisplayRole:
            keys = ["ts", "ptype", "src_addr", "dst_addr", "src_ep", "dst_ep",
                    "qos", "hops", "delay_ms", "apdu_len", "hex", "cbor"]
            return str(row.get(keys[col], ""))

        if role == Qt.ItemDataRole.ForegroundRole:
            ptype = row.get("ptype", "")
            color = {
                "BCAST": COL_BCAST,
                "MCAST": COL_MCAST,
                "UCAST": COL_UCAST,
            }.get(ptype, COL_FG)
            return QColor(color)

        if role == Qt.ItemDataRole.FontRole and col in (10, 11):
            f = QFont("Courier New")
            f.setPointSize(11)
            return f

        return None

    def _trim(self):
        excess = len(self._rows) - self._max_rows
        if excess > 0:
            self.beginRemoveRows(QModelIndex(), 0, excess - 1)
            del self._rows[:excess]
            self.endRemoveRows()

    def add_row(self, row: dict):
        pos = len(self._rows)
        self.beginInsertRows(QModelIndex(), pos, pos)
        self._rows.append(row)
        self.endInsertRows()
        if len(self._rows) > self._max_rows:
            self._trim()

    def clear(self):
        self.beginResetModel()
        self._rows.clear()
        self.endResetModel()

    def get_row(self, idx: int) -> Optional[dict]:
        if 0 <= idx < len(self._rows):
            return self._rows[idx]
        return None


# ─── Traffic filter proxy ─────────────────────────────────────────────────────
class RxFilterProxy(QSortFilterProxyModel):
    def __init__(self):
        super().__init__()
        self._ep_vals:   set[int]      = set()
        self._src_ep:    Optional[int] = None
        self._dst_ep:    Optional[int] = None
        self._src_node:  Optional[int] = None
        self._dst_node:  Optional[int] = None
        self._max_hops:  int           = 0
        self._ptype:     str           = ""
        self._qos:       Optional[int] = None
        self._min_delay: int           = 0
        self._max_delay: int           = 0
        self._min_bytes: int           = 0
        self._max_bytes: int           = 0
        self._search:    str           = ""

    def _invalidate(self):
        # invalidateFilter() is deprecated in Qt6; use invalidateRowsFilter()
        # when the running PySide6 provides it, else fall back.
        fn = getattr(self, "invalidateRowsFilter", None)
        (fn or self.invalidateFilter)()

    # ── setters ──────────────────────────────────────────────────────────────
    def _ep_from_text(self, text: str) -> Optional[int]:
        t = text.strip()
        try:
            return int(t, 0) if t else None
        except ValueError:
            return None

    def set_ep(self, text: str):
        vals: set[int] = set()
        for tok in text.replace(",", " ").split():
            try:
                vals.add(int(tok, 0))
            except ValueError:
                pass
        self._ep_vals = vals
        self._invalidate()

    def set_src_ep(self, text: str):
        self._src_ep = self._ep_from_text(text)
        self._invalidate()

    def set_dst_ep(self, text: str):
        self._dst_ep = self._ep_from_text(text)
        self._invalidate()

    def _node_from_text(self, text: str) -> Optional[int]:
        t = text.strip()
        try:
            return int(t, 0) if t else None
        except ValueError:
            return None

    def set_node(self, text: str):
        # combined src OR dst — kept for the existing "Node" field
        v = self._node_from_text(text)
        self._src_node = v
        self._dst_node = v
        self._invalidate()

    def set_src_node(self, text: str):
        self._src_node = self._node_from_text(text)
        self._invalidate()

    def set_dst_node(self, text: str):
        self._dst_node = self._node_from_text(text)
        self._invalidate()

    def set_max_hops(self, n: int):
        self._max_hops = n
        self._invalidate()

    def set_ptype(self, p: str):
        self._ptype = p
        self._invalidate()

    def set_qos(self, text: str):
        try:
            self._qos = int(text) if text not in ("", "All") else None
        except ValueError:
            self._qos = None
        self._invalidate()

    def set_min_delay(self, n: int):
        self._min_delay = n
        self._invalidate()

    def set_max_delay(self, n: int):
        self._max_delay = n
        self._invalidate()

    def set_min_bytes(self, n: int):
        self._min_bytes = n
        self._invalidate()

    def set_max_bytes(self, n: int):
        self._max_bytes = n
        self._invalidate()

    def set_search(self, text: str):
        self._search = text.strip().lower()
        self._invalidate()

    def clear_filters(self):
        self._ep_vals   = set()
        self._src_ep    = None
        self._dst_ep    = None
        self._src_node  = None
        self._dst_node  = None
        self._max_hops  = 0
        self._ptype     = ""
        self._qos       = None
        self._min_delay = 0
        self._max_delay = 0
        self._min_bytes = 0
        self._max_bytes = 0
        self._search    = ""
        self._invalidate()

    # ── filter logic ─────────────────────────────────────────────────────────
    def filterAcceptsRow(self, src_row: int, _src_parent) -> bool:
        row = self.sourceModel().get_row(src_row)
        if row is None:
            return True

        if self._ptype and row.get("ptype", "") != self._ptype:
            return False
        if self._max_hops > 0 and row.get("hops", 0) > self._max_hops:
            return False
        if self._qos is not None and row.get("qos") != self._qos:
            return False

        # Combined EP (src OR dst)
        if self._ep_vals:
            if row.get("src_ep") not in self._ep_vals and row.get("dst_ep") not in self._ep_vals:
                return False

        # Individual EP filters
        if self._src_ep is not None and row.get("src_ep") != self._src_ep:
            return False
        if self._dst_ep is not None and row.get("dst_ep") != self._dst_ep:
            return False

        # Node filters — set_node() sets both src and dst to same value (OR logic)
        src_addr = dst_addr = -1
        if self._src_node is not None or self._dst_node is not None:
            try:
                src_addr = int(row.get("src_addr", -1))
                dst_addr = int(row.get("dst_addr", -1))
            except (ValueError, TypeError):
                pass
        if self._src_node is not None and self._dst_node is not None \
                and self._src_node == self._dst_node:
            # Combined: src OR dst must match
            if src_addr != self._src_node and dst_addr != self._dst_node:
                return False
        else:
            if self._src_node is not None and src_addr != self._src_node:
                return False
            if self._dst_node is not None and dst_addr != self._dst_node:
                return False

        delay = row.get("delay_ms", 0)
        if self._min_delay > 0 and delay < self._min_delay:
            return False
        if self._max_delay > 0 and delay > self._max_delay:
            return False

        size = row.get("apdu_len", 0)
        if self._min_bytes > 0 and size < self._min_bytes:
            return False
        if self._max_bytes > 0 and size > self._max_bytes:
            return False

        if self._search:
            haystack = (
                row.get("hex",   "") + " " +
                row.get("cbor",  "") + " " +
                row.get("ascii", "")
            ).lower()
            if self._search not in haystack:
                return False

        return True


# ─── Network node registry table model ────────────────────────────────────────
class NodeTableModel(QAbstractTableModel):
    COLS = [
        "Addr (hex)", "Addr (dec)", "Role", "Mode", "Hops", "Delay ms",
        "Pkts", "Last seen", "Endpoints", "Stack FW", "App FW", "Area ID", "proc_seq", "Action",
    ]

    def __init__(self):
        super().__init__()
        self._nodes: dict[int, dict] = {}   # addr → info
        self._order: list[int] = []         # insertion order

    # ── public API ────────────────────────────────────────────────────────────
    def update_node(self, addr: int, **kw):
        is_new = addr not in self._nodes
        if is_new:
            self._nodes[addr] = {
                "addr": addr,
                "first_seen": time.strftime("%H:%M:%S"),
                "last_seen":  time.strftime("%H:%M:%S"),
                "hops": 0, "delay_ms": 0, "pkt_count": 0,
                "src_eps": set(), "fw": (), "app_fw": (), "proc_seq": 255,
                "stored_seq": 0, "action_name": "", "app_area_id": 0,
                "role": "", "mode": "",
            }
            self.beginInsertRows(QModelIndex(), len(self._order), len(self._order))
            self._order.append(addr)
            self.endInsertRows()

        n = self._nodes[addr]
        n["last_seen"] = time.strftime("%H:%M:%S")
        if kw.pop("incr_pkt", False):
            n["pkt_count"] += 1
        src_ep = kw.pop("src_ep", None)
        if src_ep is not None:
            n["src_eps"].add(src_ep)
        for k, v in kw.items():
            n[k] = v

        if not is_new:
            row = self._order.index(addr)
            self.dataChanged.emit(
                self.index(row, 0), self.index(row, len(self.COLS) - 1))

    def clear(self):
        self.beginResetModel()
        self._nodes.clear()
        self._order.clear()
        self.endResetModel()

    def get_all(self) -> list[dict]:
        return [self._nodes[a] for a in self._order]

    def addr_at(self, proxy_row: int, proxy: "QSortFilterProxyModel") -> Optional[int]:
        src_idx = proxy.mapToSource(proxy.index(proxy_row, 0))
        if src_idx.isValid():
            return self._order[src_idx.row()]
        return None

    # ── QAbstractTableModel ───────────────────────────────────────────────────
    def rowCount(self, parent=QModelIndex()) -> int:
        return len(self._order)

    def columnCount(self, parent=QModelIndex()) -> int:
        return len(self.COLS)

    def headerData(self, section: int, orientation, role=Qt.DisplayRole):
        if orientation == Qt.Horizontal and role == Qt.DisplayRole:
            return self.COLS[section]
        return None

    def data(self, index: QModelIndex, role=Qt.DisplayRole):
        if not index.isValid() or index.row() >= len(self._order):
            return None
        n   = self._nodes[self._order[index.row()]]
        col = index.column()
        if role == Qt.DisplayRole:
            eps    = ", ".join(str(e) for e in sorted(n["src_eps"]))
            fw     = ("v" + ".".join(str(x) for x in n["fw"]))     if n.get("fw")     else "—"
            app_fw = ("v" + ".".join(str(x) for x in n["app_fw"])) if n.get("app_fw") else "—"
            area   = f"0x{n['app_area_id']:08x}" if n.get("app_area_id") else "—"
            return (
                f"0x{n['addr']:08x}",
                str(n["addr"]),
                n.get("role") or "—",
                n.get("mode") or "—",
                str(n["hops"]),
                str(n["delay_ms"]),
                str(n["pkt_count"]),
                n["last_seen"],
                eps or "—",
                fw,
                app_fw,
                area,
                str(n["proc_seq"]),
                n.get("action_name") or "—",
            )[col]
        if role == Qt.UserRole:
            return n["addr"]
        return None


# ─── Log window (separate floating window) ────────────────────────────────────
class LogWindow(QDialog):
    def __init__(self):
        super().__init__(None, Qt.WindowType.Window | Qt.WindowType.WindowTitleHint |
                         Qt.WindowType.WindowCloseButtonHint |
                         Qt.WindowType.WindowMinimizeButtonHint)
        self.setWindowTitle("Wirepas — Log")
        self.resize(900, 260)
        lay = QVBoxLayout(self)
        lay.setContentsMargins(4, 4, 4, 4)
        lay.setSpacing(4)

        toolbar = QHBoxLayout()
        toolbar.addStretch()
        btn_clear = QPushButton("Clear")
        btn_clear.setFixedHeight(22)
        btn_clear.clicked.connect(self._clear)
        toolbar.addWidget(btn_clear)
        lay.addLayout(toolbar)

        self._log = QTextEdit()
        self._log.setReadOnly(True)
        self._log.setFont(QFont("Courier New", 11))
        lay.addWidget(self._log)

    def append(self, html: str):
        self._log.append(html)
        bar = self._log.verticalScrollBar()
        bar.setValue(bar.maximum())

    def _clear(self):
        self._log.clear()

    def closeEvent(self, event):
        # Emit a signal so the checkbox in Config can sync its state
        self.visibilityChanged.emit(False)
        super().closeEvent(event)

    visibilityChanged = Signal(bool)


# ─── Motor control window ────────────────────────────────────────────────────
# Motor RS485 protocol (from uart_cmd.h):
#   Frame TX/RX:  STX(0x02) | ADDR | CMD | NBR_DATA | DATA... | END(0x03)
#   GET_STATUS reply: pos(int32 LE 4B) fault_summary(1B) fault(1B) mA(uint16 LE 2B)
#   GET_CURRENT reply: mA(uint16 LE) peak_mA(uint16 LE) oc(uint8)

_MOTOR_CMD = {
    'MOVE_ABS': 0x01, 'MOVE_REL': 0x02, 'STOP':       0x03,
    'HOME':     0x04, 'SET_SPEED': 0x05, 'GET_POS':    0x06,
    'GET_STATUS': 0x07, 'SLEEP':  0x08, 'WAKE':        0x09,
    'ENABLE':   0x0A, 'DISABLE': 0x0B, 'CLR_FAULT':   0x0C,
    'SET_KP':   0x0D, 'SET_KI':  0x0E, 'SET_KD':      0x0F,
    'GET_CURRENT': 0x10,
    'DEEP_SLEEP':  0x11, 'AUTO_SLEEP':    0x12, 'HOMING':  0x13,
    'SET_ADDR':    0x14, 'SET_RAMP':      0x15, 'SET_RAMP_DOWN': 0x16,
    'GET_UID':     0x17, 'BEEP':          0x18,
}
_MOTOR_CMD_NAME = {v: k for k, v in _MOTOR_CMD.items()}


def _motor_frame(addr: int, cmd: int, data: bytes = b'') -> bytes:
    return bytes([0x02, addr, cmd, len(data)]) + data + bytes([0x03])


def _parse_motor_reply(raw: bytes) -> Optional[dict]:
    """Parse a raw motor reply frame (as forwarded by RS485 bridge on EP 2)."""
    if len(raw) < 5 or raw[0] != 0x02 or raw[-1] != 0x03:
        return None
    addr     = raw[1]
    cmd      = raw[2]
    nbr_data = raw[3]
    if len(raw) < 4 + nbr_data + 1:
        return None
    data = raw[4:4 + nbr_data]
    d: dict = {"addr": addr, "cmd": cmd, "cmd_name": _MOTOR_CMD_NAME.get(cmd, f"0x{cmd:02x}"),
               "nbr_data": nbr_data, "data": data}

    if cmd == 0x07 and nbr_data >= 8:
        pos, fault_summary, fault, mA = struct.unpack_from('<iBBH', data)
        d.update(pos=pos, fault_summary=fault_summary, fault=fault, mA=mA)

    elif cmd == 0x06 and nbr_data >= 4:
        pos, = struct.unpack_from('<i', data)
        d.update(pos=pos)

    elif cmd == 0x10 and nbr_data >= 5:
        mA, peak_mA, oc = struct.unpack_from('<HHB', data)
        d.update(mA=mA, peak_mA=peak_mA, oc=bool(oc))

    elif cmd == 0x17 and nbr_data >= 12:
        w0, w1, w2 = struct.unpack_from('<III', data)
        d["uid"] = f"{w0:08X}-{w1:08X}-{w2:08X}"

    elif nbr_data == 1:
        d["ack"] = (data[0] == 0x00)

    return d


class MotorPlot(QWidget):
    """Matplotlib widget: position (top) and current (bottom) vs. time."""
    MAXLEN = 1200

    def __init__(self, parent=None):
        super().__init__(parent)
        lay = QVBoxLayout(self)
        lay.setContentsMargins(0, 0, 0, 0)

        if not _MATPLOTLIB_OK:
            lay.addWidget(QLabel("matplotlib not installed — pip install matplotlib"))
            self._ok = False
            return
        self._ok = True

        self._fig = Figure(facecolor="#1e1e1e", tight_layout=True)
        self._ax_pos = self._fig.add_subplot(211)
        self._ax_cur = self._fig.add_subplot(212, sharex=self._ax_pos)

        for ax in (self._ax_pos, self._ax_cur):
            ax.set_facecolor("#252526")
            ax.tick_params(colors="#aaa", labelsize=8)
            ax.grid(True, alpha=0.25, lw=0.5)
            for spine in ax.spines.values():
                spine.set_edgecolor("#555")

        self._ax_pos.set_ylabel("ticks", fontsize=8, color="#aaa")
        self._ax_cur.set_ylabel("mA",    fontsize=8, color="#aaa")
        self._ax_cur.set_xlabel("t (s)", fontsize=8, color="#aaa")
        self._ax_pos.set_title("Position", fontsize=9, color="#ccc")
        self._ax_cur.set_title("Current",  fontsize=9, color="#ccc")

        self._ln_pos,  = self._ax_pos.plot([], [], "#5a9fd4", lw=1.2, label="pos")
        self._ln_sp,   = self._ax_pos.plot([], [], "#e74c3c", lw=0.9, ls="--", label="setpoint")
        self._ln_cur,  = self._ax_cur.plot([], [], "#27ae60", lw=1.2)
        self._ax_pos.legend(fontsize=7, facecolor="#333", labelcolor="#ccc")

        self._canvas = FigureCanvasQTAgg(self._fig)
        lay.addWidget(self._canvas)

        n = self.MAXLEN
        self._T:  deque = deque(maxlen=n)
        self._P:  deque = deque(maxlen=n)
        self._SP: deque = deque(maxlen=n)
        self._I:  deque = deque(maxlen=n)
        self._t0: float = time.time()

    def push(self, pos: int, sp: int, mA: int):
        if not self._ok:
            return
        t = time.time() - self._t0
        self._T.append(t); self._P.append(pos)
        self._SP.append(sp); self._I.append(mA)
        tl = list(self._T)
        self._ln_pos.set_data(tl, list(self._P))
        self._ln_sp.set_data(tl,  list(self._SP))
        self._ln_cur.set_data(tl, list(self._I))
        for ax in (self._ax_pos, self._ax_cur):
            ax.relim(); ax.autoscale_view()
        self._canvas.draw_idle()

    def set_setpoint(self, sp: int):
        if not self._ok:
            return
        if self._SP:
            self._SP[-1] = sp  # update last point immediately for display

    def clear(self):
        if not self._ok:
            return
        for d in (self._T, self._P, self._SP, self._I):
            d.clear()
        for ln in (self._ln_pos, self._ln_sp, self._ln_cur):
            ln.set_data([], [])
        self._t0 = time.time()
        self._canvas.draw_idle()


class MotorPanel(QGroupBox):
    """All controls + live status for a single motor (one RS485 address).

    Commands are sent through the owning MotorWindow, which holds the shared
    bridge address and Wirepas send function. Each panel owns one MotorPlot.
    """

    def __init__(self, title: str, default_motor: int, plot: "MotorPlot", owner: "MotorWindow"):
        super().__init__(title)
        self._owner = owner
        self._plot  = plot
        self._sp    = 0   # current setpoint (for the plot)

        lay = QVBoxLayout(self)
        lay.setSpacing(5)

        # Motor address
        addr_row = QHBoxLayout()
        addr_row.addWidget(QLabel("Motor addr:"))
        self._spn_motor = QSpinBox()
        self._spn_motor.setRange(1, 8)
        self._spn_motor.setValue(default_motor)
        self._spn_motor.setFixedWidth(60)
        addr_row.addWidget(self._spn_motor)
        addr_row.addStretch()
        lay.addLayout(addr_row)

        # Status
        def _status_lbl():
            l = QLabel("—")
            l.setFont(QFont("Courier New", 11))
            l.setStyleSheet("color:#5a9fd4;")
            return l

        sta_lay = QFormLayout()
        sta_lay.setSpacing(2)
        self._lbl_pos   = _status_lbl()
        self._lbl_mA    = _status_lbl()
        self._lbl_fault = _status_lbl()
        sta_lay.addRow("Position:", self._lbl_pos)
        sta_lay.addRow("Current:",  self._lbl_mA)
        sta_lay.addRow("Fault:",    self._lbl_fault)
        lay.addLayout(sta_lay)

        # Simple commands (incl. new Sleep / Wake)
        for row in (
            [("Enable", "ENABLE"), ("Disable", "DISABLE"), ("Stop", "STOP")],
            [("Home", "HOME"), ("Clr fault", "CLR_FAULT"), ("Status", "GET_STATUS")],
            [("Get pos", "GET_POS"), ("Get I", "GET_CURRENT")],
            [("Sleep", "SLEEP"), ("Wake", "WAKE")],
        ):
            hl = QHBoxLayout()
            for label, cmd_name in row:
                btn = QPushButton(label)
                btn.clicked.connect(lambda _, c=cmd_name: self._send(c))
                hl.addWidget(btn)
            hl.addStretch()
            lay.addLayout(hl)

        # Move
        mv = QHBoxLayout()
        mv.addWidget(QLabel("Counts:"))
        self._txt_counts = QLineEdit("1000")
        self._txt_counts.setFont(QFont("Courier New", 11))
        self._txt_counts.setFixedWidth(80)
        self._txt_counts.setToolTip("Counts (int32). Positive = forward.")
        mv.addWidget(self._txt_counts)
        btn_rel = QPushButton("REL")
        btn_rel.setStyleSheet("background:#1e6b4a;")
        btn_rel.clicked.connect(self._move_rel)
        mv.addWidget(btn_rel)
        btn_abs = QPushButton("ABS")
        btn_abs.setStyleSheet("background:#2d5a8e;")
        btn_abs.clicked.connect(self._move_abs)
        mv.addWidget(btn_abs)
        lay.addLayout(mv)

        # Speed
        spd = QHBoxLayout()
        spd.addWidget(QLabel("Speed:"))
        self._spn_speed = QSpinBox()
        self._spn_speed.setRange(0, 100)
        self._spn_speed.setValue(50)
        self._spn_speed.setSuffix(" %")
        self._spn_speed.setFixedWidth(70)
        spd.addWidget(self._spn_speed)
        btn_spd = QPushButton("Set")
        btn_spd.clicked.connect(
            lambda: self._send("SET_SPEED", bytes([self._spn_speed.value()])))
        spd.addWidget(btn_spd)
        spd.addStretch()
        lay.addLayout(spd)

        # PID gains (new) — sent as float32 LE
        pid_lay = QFormLayout()
        pid_lay.setSpacing(2)
        self._pid_spn = {}
        for name, cmd_name in (("Kp", "SET_KP"), ("Ki", "SET_KI"), ("Kd", "SET_KD")):
            row = QHBoxLayout()
            spn = QDoubleSpinBox()
            spn.setRange(-128.0, 127.0)   # firmware stores k × 256 as int16
            spn.setDecimals(3)
            spn.setSingleStep(0.1)
            spn.setFixedWidth(90)
            row.addWidget(spn)
            btn = QPushButton("Set")
            btn.clicked.connect(
                lambda _, c=cmd_name, s=spn: self._send(c, self._pack_gain(s.value())))
            row.addWidget(btn)
            row.addStretch()
            holder = QWidget(); holder.setLayout(row)
            pid_lay.addRow(name + ":", holder)
            self._pid_spn[name] = spn
        lay.addLayout(pid_lay)

        # Ramp (new) — up / down accel time in ms, uint16 LE (0 = disabled)
        ramp_lay = QFormLayout(); ramp_lay.setSpacing(2)
        self._spn_ramp_up = QSpinBox()
        self._spn_ramp_up.setRange(0, 65535); self._spn_ramp_up.setSuffix(" ms"); self._spn_ramp_up.setFixedWidth(100)
        self._spn_ramp_dn = QSpinBox()
        self._spn_ramp_dn.setRange(0, 65535); self._spn_ramp_dn.setSuffix(" ms"); self._spn_ramp_dn.setFixedWidth(100)
        for spn, cmd_name, label in ((self._spn_ramp_up, "SET_RAMP", "Ramp up:"),
                                     (self._spn_ramp_dn, "SET_RAMP_DOWN", "Ramp dn:")):
            r = QHBoxLayout(); r.addWidget(spn)
            b = QPushButton("Set")
            b.clicked.connect(lambda _, c=cmd_name, s=spn: self._send(c, struct.pack("<H", s.value())))
            r.addWidget(b); r.addStretch()
            holder = QWidget(); holder.setLayout(r)
            ramp_lay.addRow(label, holder)
        lay.addLayout(ramp_lay)

        # Buzzer / beep (new) — freq_hz(2) dur_ms(2) duty_pct(1)
        buzz = QHBoxLayout()
        buzz.addWidget(QLabel("Buzz:"))
        self._spn_bz_freq = QSpinBox(); self._spn_bz_freq.setRange(0, 20000); self._spn_bz_freq.setValue(2000); self._spn_bz_freq.setSuffix(" Hz"); self._spn_bz_freq.setFixedWidth(85)
        self._spn_bz_dur  = QSpinBox(); self._spn_bz_dur.setRange(0, 65535); self._spn_bz_dur.setValue(200); self._spn_bz_dur.setSuffix(" ms"); self._spn_bz_dur.setFixedWidth(85)
        self._spn_bz_duty = QSpinBox(); self._spn_bz_duty.setRange(0, 100); self._spn_bz_duty.setValue(50); self._spn_bz_duty.setSuffix(" %"); self._spn_bz_duty.setFixedWidth(65)
        buzz.addWidget(self._spn_bz_freq); buzz.addWidget(self._spn_bz_dur); buzz.addWidget(self._spn_bz_duty)
        b_bz = QPushButton("Buzz"); b_bz.clicked.connect(self._buzz)
        buzz.addWidget(b_bz)
        lay.addLayout(buzz)

        # Homing (new) — dir(1) speed(1) timeout_ms(2 LE)
        hm = QHBoxLayout()
        hm.addWidget(QLabel("Homing:"))
        self._cmb_home_dir = QComboBox(); self._cmb_home_dir.addItems(["fwd", "rev"]); self._cmb_home_dir.setFixedWidth(60)
        self._spn_home_spd = QSpinBox(); self._spn_home_spd.setRange(0, 100); self._spn_home_spd.setValue(30); self._spn_home_spd.setSuffix(" %"); self._spn_home_spd.setFixedWidth(65)
        self._spn_home_to  = QSpinBox(); self._spn_home_to.setRange(0, 65535); self._spn_home_to.setValue(5000); self._spn_home_to.setSuffix(" ms"); self._spn_home_to.setFixedWidth(85)
        hm.addWidget(self._cmb_home_dir); hm.addWidget(self._spn_home_spd); hm.addWidget(self._spn_home_to)
        b_hm = QPushButton("Go"); b_hm.clicked.connect(self._homing)
        hm.addWidget(b_hm)
        lay.addLayout(hm)

        # Change motor address (SET_ADDR) — dedicated, with confirmation since it
        # re-addresses the slave (sent to the panel's *current* motor address).
        chad = QHBoxLayout()
        chad.addWidget(QLabel("Change addr →"))
        self._spn_newaddr = QSpinBox()
        self._spn_newaddr.setRange(1, 8)
        self._spn_newaddr.setFixedWidth(55)
        self._spn_newaddr.setToolTip("New RS485 address (1–8) to assign to the motor "
                                     "currently selected above")
        chad.addWidget(self._spn_newaddr)
        b_sa = QPushButton("Apply addr")
        b_sa.clicked.connect(self._set_addr)
        chad.addWidget(b_sa)
        chad.addStretch()
        lay.addLayout(chad)

        # Misc commands (new): auto-sleep, deep sleep, get UID
        msc = QHBoxLayout()
        msc.addWidget(QLabel("Auto-slp:"))
        self._spn_autosleep = QSpinBox(); self._spn_autosleep.setRange(0, 65535); self._spn_autosleep.setSuffix(" s"); self._spn_autosleep.setFixedWidth(80)
        msc.addWidget(self._spn_autosleep)
        b_as = QPushButton("Set")
        b_as.clicked.connect(lambda: self._send("AUTO_SLEEP", struct.pack("<H", self._spn_autosleep.value())))
        msc.addWidget(b_as)
        for label, cmd_name in (("Deep sleep", "DEEP_SLEEP"), ("Get UID", "GET_UID")):
            b = QPushButton(label)
            b.clicked.connect(lambda _, c=cmd_name: self._send(c))
            msc.addWidget(b)
        msc.addStretch()
        lay.addLayout(msc)

    # ── API used by MotorWindow ───────────────────────────────────────────────

    def motor_addr(self) -> int:
        return self._spn_motor.value()

    def handle_reply(self, d: dict):
        """Update status labels + plot from a parsed reply for this motor."""
        if "pos" in d and "mA" in d:
            pos = d["pos"]; mA = d["mA"]
            fault = bool(d.get("fault_summary", 0) or d.get("fault", 0))
            self._lbl_pos.setText(f"{pos:+d}")
            self._lbl_mA.setText(f"{mA} mA")
            self._lbl_fault.setText("FAULT" if fault else "OK")
            self._lbl_fault.setStyleSheet(
                "color:#e74c3c; font-weight:bold;" if fault else "color:#27ae60; font-weight:bold;")
            self._plot.push(pos, self._sp, mA)
        elif "pos" in d:
            self._lbl_pos.setText(f"{d['pos']:+d}")
            self._plot.push(d["pos"], self._sp, 0)
        elif "mA" in d:
            mA = d["mA"]; peak = d.get("peak_mA", 0)
            self._lbl_mA.setText(f"{mA} mA  (peak {peak})")

    def clear_plot(self):
        self._plot.clear()

    # ── Internal ──────────────────────────────────────────────────────────────

    def _send(self, cmd_name: str, data: bytes = b''):
        self._owner.send_to_motor(self.motor_addr(), cmd_name, data)

    def _counts(self) -> Optional[int]:
        try:
            return int(self._txt_counts.text())
        except ValueError:
            self._owner.append_log("Invalid count value", "#e74c3c")
            return None

    def _move_rel(self):
        counts = self._counts()
        if counts is None:
            return
        self._sp += counts
        self._plot.set_setpoint(self._sp)
        self._send("MOVE_REL", struct.pack("<i", counts))

    def _move_abs(self):
        counts = self._counts()
        if counts is None:
            return
        self._sp = counts
        self._plot.set_setpoint(self._sp)
        self._send("MOVE_ABS", struct.pack("<i", counts))

    @staticmethod
    def _pack_gain(value: float) -> bytes:
        """PID gain → int16 (value × 256), little-endian (firmware format)."""
        fixed = max(-32768, min(32767, int(round(value * 256))))
        return struct.pack("<h", fixed)

    def _buzz(self):
        self._send("BEEP", struct.pack("<HHB",
                                       self._spn_bz_freq.value(),
                                       self._spn_bz_dur.value(),
                                       self._spn_bz_duty.value()))

    def _homing(self):
        data = bytes([self._cmb_home_dir.currentIndex(), self._spn_home_spd.value()]) \
            + struct.pack("<H", self._spn_home_to.value())
        self._send("HOMING", data)

    def _set_addr(self):
        """Re-address the currently-selected motor (SET_ADDR), with confirmation."""
        cur = self.motor_addr()
        new = self._spn_newaddr.value()
        if cur == new:
            self._owner.append_log(f"Motor already at address {new}", "#e0a030")
            return
        reply = QMessageBox.question(
            self, "Change motor address",
            f"Re-address motor {cur} → {new}?\n\n"
            f"The command is sent to motor {cur}; on success the motor adopts "
            f"address {new} and this panel will target {new}.",
            QMessageBox.StandardButton.Yes | QMessageBox.StandardButton.No)
        if reply != QMessageBox.StandardButton.Yes:
            return
        self._send("SET_ADDR", bytes([new]))      # sent to the current address (cur)
        self._spn_motor.setValue(new)             # re-target this panel at the new address


class MotorWindow(QDialog):
    """Floating motor control + diagnostics window for two motors.

    Send commands to the RS485 bridge via Wirepas EP 1.
    Receive motor replies via EP 2 (call feed_reply() from the main window);
    replies are routed to the panel whose motor address matches.
    """

    visibilityChanged = Signal(bool)

    EP_DOWN = 1   # gateway → bridge → motor
    EP_UP   = 2   # motor → bridge → gateway

    def __init__(self):
        super().__init__(None,
                         Qt.WindowType.Window |
                         Qt.WindowType.WindowTitleHint |
                         Qt.WindowType.WindowCloseButtonHint |
                         Qt.WindowType.WindowMinimizeButtonHint)
        self.setWindowTitle("Motor Control")
        self.resize(1550, 780)

        self._send_fn = None   # set by MainWindow: fn(dst_addr, dst_ep, payload, src_ep)

        # ── Layout ──────────────────────────────────────────────────────────────
        root = QHBoxLayout(self)
        root.setSpacing(6)
        root.setContentsMargins(6, 6, 6, 6)

        # Left: shared bridge target + two motor panels (scrollable)
        left = QWidget()
        llay = QVBoxLayout(left); llay.setSpacing(6)

        tgt_grp = QGroupBox("Bridge")
        tgt_lay = QFormLayout(tgt_grp)
        self._txt_node = QLineEdit("0x0000006f")
        self._txt_node.setFont(QFont("Courier New", 11))
        self._txt_node.setToolTip("Wirepas node address of the RS485 bridge")
        tgt_lay.addRow("Bridge addr:", self._txt_node)
        llay.addWidget(tgt_grp)

        poll_grp = QGroupBox("Auto-poll GET_STATUS (both motors)")
        poll_lay = QHBoxLayout(poll_grp)
        self._chk_poll = QCheckBox("Enable")
        self._chk_poll.toggled.connect(self._on_poll_toggle)
        poll_lay.addWidget(self._chk_poll)
        poll_lay.addWidget(QLabel("every"))
        self._spn_poll_ms = QSpinBox()
        self._spn_poll_ms.setRange(100, 10000)
        self._spn_poll_ms.setValue(500)
        self._spn_poll_ms.setSuffix(" ms")
        self._spn_poll_ms.setFixedWidth(90)
        poll_lay.addWidget(self._spn_poll_ms)
        poll_lay.addStretch()
        llay.addWidget(poll_grp)

        self._poll_timer = QTimer()
        self._poll_timer.timeout.connect(self._do_poll_all)

        btn_clr = QPushButton("Clear both plots")
        btn_clr.clicked.connect(lambda: (self._panelA.clear_plot(), self._panelB.clear_plot()))
        llay.addWidget(btn_clr)

        # Right: two stacked plots (one per motor) + log
        right = QSplitter(Qt.Orientation.Vertical)
        self._plotA = MotorPlot()
        self._plotB = MotorPlot()
        right.addWidget(self._plotA)
        right.addWidget(self._plotB)
        self._log = QTextEdit()
        self._log.setReadOnly(True)
        self._log.setFont(QFont("Courier New", 10))
        self._log.setMaximumHeight(120)
        right.addWidget(self._log)
        right.setStretchFactor(0, 1)
        right.setStretchFactor(1, 1)

        # Two motor panels
        self._panelA = MotorPanel("Motor A", 1, self._plotA, self)
        self._panelB = MotorPanel("Motor B", 2, self._plotB, self)
        llay.addWidget(self._panelA)
        llay.addWidget(self._panelB)
        llay.addStretch()

        scroll = QScrollArea()
        scroll.setWidgetResizable(True)
        scroll.setWidget(left)
        scroll.setFixedWidth(740)
        root.addWidget(scroll)
        root.addWidget(right, stretch=1)

    # ── Public API ────────────────────────────────────────────────────────────

    def set_send_fn(self, fn):
        """fn(dst_addr: int, dst_ep: int, payload: bytes, src_ep: int) → None"""
        self._send_fn = fn

    def feed_reply(self, raw: bytes):
        """Called from MainWindow when an EP 2 packet arrives. Routes the reply
        to the panel whose motor address matches the frame's address."""
        d = _parse_motor_reply(raw)
        if d is None:
            self.append_log(f"[RX] unparsed: {raw.hex()}", "#888")
            return

        motor = d["addr"]
        routed = False
        for panel in (self._panelA, self._panelB):
            if panel.motor_addr() == motor:
                panel.handle_reply(d)
                routed = True
        self.append_log(f"[m{motor} {d['cmd_name']}] {self._fmt_reply(d)}",
                        "#5a9fd4" if routed else "#888")

    # ── Internal helpers ──────────────────────────────────────────────────────

    @staticmethod
    def _fmt_reply(d: dict) -> str:
        if "pos" in d and "mA" in d:
            fault = d.get("fault_summary", 0) or d.get("fault", 0)
            return f"pos={d['pos']:+d}  mA={d['mA']}  fault={'!' if fault else 'OK'}"
        if "pos" in d:
            return f"pos={d['pos']:+d}"
        if "mA" in d:
            return f"mA={d['mA']}  peak={d.get('peak_mA', 0)}  oc={d.get('oc', False)}"
        if "uid" in d:
            return f"UID={d['uid']}"
        if "ack" in d:
            return "ACK" if d["ack"] else "NAK"
        return d["data"].hex()

    def _node_addr(self) -> Optional[int]:
        try:
            return int(self._txt_node.text().strip(), 0)
        except ValueError:
            self.append_log("Invalid bridge address", "#e74c3c")
            return None

    def send_to_motor(self, motor: int, cmd_name: str, data: bytes = b''):
        """Send a command to a specific motor address via the bridge."""
        addr = self._node_addr()
        if addr is None or self._send_fn is None:
            return
        cid   = _MOTOR_CMD[cmd_name]
        frame = _motor_frame(motor, cid, data)
        self._send_fn(addr, self.EP_DOWN, frame, self.EP_DOWN)
        self.append_log(f"TX → 0x{addr:08x} m{motor} [{cmd_name}] {frame.hex()}", "#27ae60")

    def _do_poll_all(self):
        self.send_to_motor(self._panelA.motor_addr(), "GET_STATUS")
        self.send_to_motor(self._panelB.motor_addr(), "GET_STATUS")

    def _on_poll_toggle(self, checked: bool):
        if checked:
            self._poll_timer.start(self._spn_poll_ms.value())
        else:
            self._poll_timer.stop()

    def append_log(self, msg: str, color: str = "#d4d4d4"):
        ts = time.strftime("%H:%M:%S")
        self._log.append(
            f"<span style='color:#555;'>[{ts}]</span> "
            f"<span style='color:{color};'>{msg}</span>")
        bar = self._log.verticalScrollBar()
        bar.setValue(bar.maximum())

    def closeEvent(self, event):
        self._poll_timer.stop()
        self.visibilityChanged.emit(False)
        super().closeEvent(event)


# ─── Sensor plot window (CTN EP11 / SP-110 EP12 / AEM10900 EP09) ───────────────
class SensorPlot(QWidget):
    """Matplotlib widget for one node, 3×2 grid (all share one time base):
       left   — CTN temperature (°C), CTN resistance (Ω), SP-110 irradiance (W/m²)
       right  — AEM voltages (vsto/vsrc, V), AEM power (µW), AEM temperature (°C)."""
    MAXLEN = 1800

    def __init__(self, parent=None):
        super().__init__(parent)
        lay = QVBoxLayout(self)
        lay.setContentsMargins(0, 0, 0, 0)

        if not _MATPLOTLIB_OK:
            lay.addWidget(QLabel("matplotlib not installed — pip install matplotlib"))
            self._ok = False
            return
        self._ok = True

        # 4×2 grid: left = CTN T / CTN R / SP-110 ; right = AEM vsto / vsrc / pwr / temp
        self._fig = Figure(facecolor="#1e1e1e", tight_layout=True)
        self._ax_t    = self._fig.add_subplot(4, 2, 1)
        self._ax_vsto = self._fig.add_subplot(4, 2, 2, sharex=self._ax_t)
        self._ax_r    = self._fig.add_subplot(4, 2, 3, sharex=self._ax_t)
        self._ax_vsrc = self._fig.add_subplot(4, 2, 4, sharex=self._ax_t)
        self._ax_i    = self._fig.add_subplot(4, 2, 5, sharex=self._ax_t)
        self._ax_p    = self._fig.add_subplot(4, 2, 6, sharex=self._ax_t)
        self._ax_at   = self._fig.add_subplot(4, 2, 8, sharex=self._ax_t)
        self._axes = (self._ax_t, self._ax_vsto, self._ax_r, self._ax_vsrc,
                      self._ax_i, self._ax_p, self._ax_at)

        for ax in self._axes:
            ax.set_facecolor("#252526")
            ax.tick_params(colors="#aaa", labelsize=7)
            ax.grid(True, alpha=0.25, lw=0.5)
            for spine in ax.spines.values():
                spine.set_edgecolor("#555")

        self._ax_t.set_title("CTN temperature", fontsize=8, color="#ccc")
        self._ax_t.set_ylabel("°C", fontsize=7, color="#aaa")
        self._ax_r.set_title("CTN resistance", fontsize=8, color="#ccc")
        self._ax_r.set_ylabel("Ω", fontsize=7, color="#aaa")
        self._ax_i.set_title("SP-110 irradiance", fontsize=8, color="#ccc")
        self._ax_i.set_ylabel("W/m²", fontsize=7, color="#aaa")
        self._ax_i.set_xlabel("t (s)", fontsize=7, color="#aaa")
        self._ax_vsto.set_title("AEM vsto (storage)", fontsize=8, color="#ccc")
        self._ax_vsto.set_ylabel("V", fontsize=7, color="#aaa")
        self._ax_vsrc.set_title("AEM vsrc (source)", fontsize=8, color="#ccc")
        self._ax_vsrc.set_ylabel("V", fontsize=7, color="#aaa")
        self._ax_p.set_title("AEM power", fontsize=8, color="#ccc")
        self._ax_p.set_ylabel("µW", fontsize=7, color="#aaa")
        self._ax_at.set_title("AEM temperature", fontsize=8, color="#ccc")
        self._ax_at.set_ylabel("°C", fontsize=7, color="#aaa")
        self._ax_at.set_xlabel("t (s)", fontsize=7, color="#aaa")

        self._ln_t,    = self._ax_t.plot([], [], "#5a9fd4", lw=1.4)
        self._ln_r,    = self._ax_r.plot([], [], "#27ae60", lw=1.2)
        self._ln_i,    = self._ax_i.plot([], [], "#e0a030", lw=1.4)
        self._ln_vsto, = self._ax_vsto.plot([], [], "#5a9fd4", lw=1.3)
        self._ln_vsrc, = self._ax_vsrc.plot([], [], "#e74c3c", lw=1.3)
        self._ln_p,    = self._ax_p.plot([], [], "#9b59b6", lw=1.3)
        self._ln_at,   = self._ax_at.plot([], [], "#1abc9c", lw=1.3)

        self._canvas = FigureCanvasQTAgg(self._fig)
        lay.addWidget(self._canvas)

    def redraw(self, d: dict):
        """All series share the per-node t0 time base."""
        if not self._ok:
            return
        tl = list(d["T"])
        self._ln_t.set_data(tl, list(d["TC"]))
        self._ln_r.set_data(tl, list(d["RT"]))
        self._ln_i.set_data(list(d["TI"]), list(d["IRR"]))
        ta = list(d["TA"])
        self._ln_vsto.set_data(ta, list(d["VSTO"]))
        self._ln_vsrc.set_data(ta, list(d["VSRC"]))
        self._ln_p.set_data(ta, list(d["PWR"]))
        self._ln_at.set_data(ta, list(d["ATEMP"]))
        for ax in self._axes:
            ax.relim(); ax.autoscale_view()
        self._canvas.draw_idle()

    def clear(self):
        if not self._ok:
            return
        for ln in (self._ln_t, self._ln_r, self._ln_i,
                   self._ln_vsto, self._ln_vsrc, self._ln_p, self._ln_at):
            ln.set_data([], [])
        self._canvas.draw_idle()


class SensorWindow(QDialog):
    """Floating per-node CTN sensor plot window.

    Receives EP 11 CBOR [T_C, R_T, diag] uplinks via feed() from the main window
    and plots temperature + resistance vs. time, one node at a time (selectable).
    """

    visibilityChanged = Signal(bool)

    EP_PMIC       =  9   # AEM10900 [vsto, vsrc, T, pwr, status, chg] CBOR uplink
    EP_SENSOR     = 11   # CTN [T_C, R_T, diag] CBOR uplink endpoint
    EP_IRRADIANCE = 12   # SP-110 [W/m2, mV, diag] CBOR uplink endpoint

    def __init__(self):
        super().__init__(None,
                         Qt.WindowType.Window |
                         Qt.WindowType.WindowTitleHint |
                         Qt.WindowType.WindowCloseButtonHint |
                         Qt.WindowType.WindowMinimizeButtonHint)
        self.setWindowTitle("Sensor — CTN / SP-110 / AEM10900")
        self.resize(1120, 840)

        # Per-node ring buffers:
        #   addr → {"T": deque, "TC": deque, "RT": deque, "t0": float}
        self._data: dict[int, dict] = {}

        root = QVBoxLayout(self)
        root.setSpacing(6)
        root.setContentsMargins(6, 6, 6, 6)

        # ── Toolbar: node selector + latest values ───────────────────────────────
        bar = QHBoxLayout()
        bar.addWidget(QLabel("Node:"))
        self._cbo_node = QComboBox()
        self._cbo_node.setMinimumWidth(140)
        self._cbo_node.setFont(QFont("Courier New", 11))
        self._cbo_node.currentIndexChanged.connect(self._on_node_changed)
        bar.addWidget(self._cbo_node)

        self._lbl_vals = QLabel("—")
        self._lbl_vals.setFont(QFont("Courier New", 11))
        self._lbl_vals.setStyleSheet("color:#5a9fd4;")
        bar.addWidget(self._lbl_vals, stretch=1)

        btn_clear = QPushButton("Clear")
        btn_clear.clicked.connect(self._on_clear)
        bar.addWidget(btn_clear)
        root.addLayout(bar)

        self._plot = SensorPlot()
        root.addWidget(self._plot, stretch=1)

    # ── Public API ────────────────────────────────────────────────────────────

    def _node(self, src_addr: int) -> dict:
        """Get/create the per-node buffer set and combo entry."""
        d = self._data.get(src_addr)
        if d is None:
            d = {"t0": time.time(),
                 "T":   deque(maxlen=SensorPlot.MAXLEN),   # CTN time base (EP11)
                 "TC":  deque(maxlen=SensorPlot.MAXLEN),
                 "RT":  deque(maxlen=SensorPlot.MAXLEN),
                 "TI":  deque(maxlen=SensorPlot.MAXLEN),   # irradiance time base (EP12)
                 "IRR": deque(maxlen=SensorPlot.MAXLEN),
                 "TA":  deque(maxlen=SensorPlot.MAXLEN),   # AEM time base (EP09)
                 "VSTO": deque(maxlen=SensorPlot.MAXLEN),
                 "VSRC": deque(maxlen=SensorPlot.MAXLEN),
                 "PWR":  deque(maxlen=SensorPlot.MAXLEN),
                 "ATEMP": deque(maxlen=SensorPlot.MAXLEN)}
            self._data[src_addr] = d
            self._cbo_node.addItem(f"0x{src_addr:08x}", src_addr)
            if self._cbo_node.count() == 1:
                self._cbo_node.setCurrentIndex(0)
        return d

    def feed_ctn(self, src_addr: int, t_c: float, r_t: float, diag: int):
        """EP 11 CTN packet. A probe fault is plotted as a gap (NaN)."""
        fault = bool(diag) or t_c <= -900.0 or r_t < 0.0
        d = self._node(src_addr)
        d["T"].append(time.time() - d["t0"])
        d["TC"].append(float("nan") if fault else t_c)
        d["RT"].append(float("nan") if fault else r_t)
        d["last_ctn"] = (t_c, r_t, diag, fault)
        if self._selected_addr() == src_addr:
            self._refresh()

    def feed_irr(self, src_addr: int, wm2: float, mv: float, diag: int):
        """EP 12 SP-110 irradiance packet. A sensor fault is a gap (NaN)."""
        fault = bool(diag)
        d = self._node(src_addr)
        d["TI"].append(time.time() - d["t0"])
        d["IRR"].append(float("nan") if fault else wm2)
        d["last_irr"] = (wm2, mv, diag, fault)
        if self._selected_addr() == src_addr:
            self._refresh()

    def feed_aem(self, src_addr: int, vsto: float, vsrc: float, temp: float,
                 pwr: float, status: int, charging: bool):
        """EP 09 AEM10900 PMIC packet."""
        d = self._node(src_addr)
        d["TA"].append(time.time() - d["t0"])
        d["VSTO"].append(vsto)
        d["VSRC"].append(vsrc)
        d["PWR"].append(pwr)
        d["ATEMP"].append(temp)
        d["last_aem"] = (vsto, vsrc, temp, pwr, status, charging)
        if self._selected_addr() == src_addr:
            self._refresh()

    # ── Internal ──────────────────────────────────────────────────────────────

    def _selected_addr(self):
        idx = self._cbo_node.currentIndex()
        return self._cbo_node.itemData(idx) if idx >= 0 else None

    def _refresh(self):
        addr = self._selected_addr()
        d = self._data.get(addr) if addr is not None else None
        if d is None:
            self._plot.clear()
            self._lbl_vals.setText("—")
            return
        self._plot.redraw(d)

        parts = []
        ctn = d.get("last_ctn")
        if ctn is not None:
            t_c, r_t, diag, fault = ctn
            parts.append(f"CTN FAULT(diag={diag})" if fault
                         else f"T={t_c:.2f}°C R={r_t:.0f}Ω")
        irr = d.get("last_irr")
        if irr is not None:
            wm2, mv, diag, fault = irr
            parts.append(f"SP110 FAULT(diag={diag})" if fault
                         else f"E={wm2:.1f}W/m² ({mv:.2f}mV)")
        aem = d.get("last_aem")
        if aem is not None:
            vsto, vsrc, atemp, pwr, status, charging = aem
            parts.append(f"AEM vsto={vsto:.2f}V vsrc={vsrc:.2f}V "
                         f"P={pwr:.0f}µW T={atemp:.1f}°C{' chg' if charging else ''}")
        any_fault = (ctn and ctn[3]) or (irr and irr[3])
        self._lbl_vals.setText("    ".join(parts) if parts else "—")
        self._lbl_vals.setStyleSheet(
            "color:#e74c3c; font-weight:bold;" if any_fault else "color:#5a9fd4;")

    def _on_node_changed(self, _idx: int):
        self._refresh()

    def _on_clear(self):
        addr = self._selected_addr()
        if addr is not None and addr in self._data:
            d = self._data[addr]
            for k in ("T", "TC", "RT", "TI", "IRR",
                      "TA", "VSTO", "VSRC", "PWR", "ATEMP"):
                d[k].clear()
            d.pop("last_ctn", None)
            d.pop("last_irr", None)
            d.pop("last_aem", None)
            d["t0"] = time.time()
        self._refresh()

    def closeEvent(self, event):
        self.visibilityChanged.emit(False)
        super().closeEvent(event)


# ─── nRF54L15-tag plot window (EP 20) ─────────────────────────────────────────
class TagPlot(QWidget):
    """Matplotlib widget for one nRF54L15-tag node (EP 20), 3×2 grid sharing one
    time base: BME688 temperature / pressure / humidity / gas, ADXL367 accel
    (x/y/z) and BMI270 gyro (x/y/z)."""
    MAXLEN = 1800

    def __init__(self, parent=None):
        super().__init__(parent)
        lay = QVBoxLayout(self)
        lay.setContentsMargins(0, 0, 0, 0)
        if not _MATPLOTLIB_OK:
            lay.addWidget(QLabel("matplotlib not installed — pip install matplotlib"))
            self._ok = False
            return
        self._ok = True

        self._fig = Figure(facecolor="#1e1e1e", tight_layout=True)
        self._ax_t = self._fig.add_subplot(3, 2, 1)
        self._ax_p = self._fig.add_subplot(3, 2, 2, sharex=self._ax_t)
        self._ax_h = self._fig.add_subplot(3, 2, 3, sharex=self._ax_t)
        self._ax_g = self._fig.add_subplot(3, 2, 4, sharex=self._ax_t)
        self._ax_a = self._fig.add_subplot(3, 2, 5, sharex=self._ax_t)
        self._ax_w = self._fig.add_subplot(3, 2, 6, sharex=self._ax_t)
        self._axes = (self._ax_t, self._ax_p, self._ax_h, self._ax_g,
                      self._ax_a, self._ax_w)
        for ax in self._axes:
            ax.set_facecolor("#252526")
            ax.tick_params(colors="#aaa", labelsize=7)
            ax.grid(True, alpha=0.25, lw=0.5)
            for spine in ax.spines.values():
                spine.set_edgecolor("#555")

        self._ax_t.set_title("BME688 temperature", fontsize=8, color="#ccc")
        self._ax_t.set_ylabel("°C", fontsize=7, color="#aaa")
        self._ax_p.set_title("BME688 pressure", fontsize=8, color="#ccc")
        self._ax_p.set_ylabel("Pa", fontsize=7, color="#aaa")
        self._ax_h.set_title("BME688 humidity", fontsize=8, color="#ccc")
        self._ax_h.set_ylabel("%", fontsize=7, color="#aaa")
        self._ax_g.set_title("BME688 gas", fontsize=8, color="#ccc")
        self._ax_g.set_ylabel("Ω", fontsize=7, color="#aaa")
        self._ax_a.set_title("ADXL367 accel", fontsize=8, color="#ccc")
        self._ax_a.set_ylabel("mg", fontsize=7, color="#aaa")
        self._ax_a.set_xlabel("t (s)", fontsize=7, color="#aaa")
        self._ax_w.set_title("BMI270 gyro", fontsize=8, color="#ccc")
        self._ax_w.set_ylabel("raw", fontsize=7, color="#aaa")
        self._ax_w.set_xlabel("t (s)", fontsize=7, color="#aaa")

        self._ln_t, = self._ax_t.plot([], [], "#5a9fd4", lw=1.4)
        self._ln_p, = self._ax_p.plot([], [], "#e0a030", lw=1.3)
        self._ln_h, = self._ax_h.plot([], [], "#1abc9c", lw=1.3)
        self._ln_g, = self._ax_g.plot([], [], "#9b59b6", lw=1.2)
        self._ln_ax, = self._ax_a.plot([], [], "#e74c3c", lw=1.1, label="x")
        self._ln_ay, = self._ax_a.plot([], [], "#27ae60", lw=1.1, label="y")
        self._ln_az, = self._ax_a.plot([], [], "#5a9fd4", lw=1.1, label="z")
        self._ln_gx, = self._ax_w.plot([], [], "#e74c3c", lw=1.1, label="x")
        self._ln_gy, = self._ax_w.plot([], [], "#27ae60", lw=1.1, label="y")
        self._ln_gz, = self._ax_w.plot([], [], "#5a9fd4", lw=1.1, label="z")
        for ax in (self._ax_a, self._ax_w):
            ax.legend(fontsize=6, labelcolor="#ccc", facecolor="#252526",
                      edgecolor="#555", loc="upper right")

        self._canvas = FigureCanvasQTAgg(self._fig)
        lay.addWidget(self._canvas)

    def redraw(self, d: dict):
        if not self._ok:
            return
        t = list(d["T"])
        self._ln_t.set_data(t, list(d["TC"]))
        self._ln_p.set_data(t, list(d["P"]))
        self._ln_h.set_data(t, list(d["H"]))
        self._ln_g.set_data(t, list(d["GAS"]))
        self._ln_ax.set_data(t, list(d["AX"]))
        self._ln_ay.set_data(t, list(d["AY"]))
        self._ln_az.set_data(t, list(d["AZ"]))
        self._ln_gx.set_data(t, list(d["GX"]))
        self._ln_gy.set_data(t, list(d["GY"]))
        self._ln_gz.set_data(t, list(d["GZ"]))
        for ax in self._axes:
            ax.relim(); ax.autoscale_view()
        self._canvas.draw_idle()

    def clear(self):
        if not self._ok:
            return
        for ln in (self._ln_t, self._ln_p, self._ln_h, self._ln_g,
                   self._ln_ax, self._ln_ay, self._ln_az,
                   self._ln_gx, self._ln_gy, self._ln_gz):
            ln.set_data([], [])
        self._canvas.draw_idle()


class TagWindow(QDialog):
    """Floating per-node plot window for nRF54L15-tag EP 20 CBOR uplinks
    [T, P, H, gas, ax, ay, az, gx, gy, gz]."""

    visibilityChanged = Signal(bool)
    EP_TAG = 20

    _KEYS = ("TC", "P", "H", "GAS", "AX", "AY", "AZ", "GX", "GY", "GZ")

    def __init__(self):
        super().__init__(None,
                         Qt.WindowType.Window |
                         Qt.WindowType.WindowTitleHint |
                         Qt.WindowType.WindowCloseButtonHint |
                         Qt.WindowType.WindowMinimizeButtonHint)
        self.setWindowTitle("nRF54L15-tag — BME688 / ADXL367 / BMI270 (EP 20)")
        self.resize(1120, 760)
        self._data: dict[int, dict] = {}

        root = QVBoxLayout(self)
        root.setSpacing(6)
        root.setContentsMargins(6, 6, 6, 6)

        bar = QHBoxLayout()
        bar.addWidget(QLabel("Node:"))
        self._cbo_node = QComboBox()
        self._cbo_node.setMinimumWidth(140)
        self._cbo_node.setFont(QFont("Courier New", 11))
        self._cbo_node.currentIndexChanged.connect(self._on_node_changed)
        bar.addWidget(self._cbo_node)
        self._lbl_vals = QLabel("—")
        self._lbl_vals.setFont(QFont("Courier New", 11))
        self._lbl_vals.setStyleSheet("color:#5a9fd4;")
        bar.addWidget(self._lbl_vals, stretch=1)
        btn_clear = QPushButton("Clear")
        btn_clear.clicked.connect(self._on_clear)
        bar.addWidget(btn_clear)
        root.addLayout(bar)

        self._plot = TagPlot()
        root.addWidget(self._plot, stretch=1)

    def _node(self, src_addr: int) -> dict:
        d = self._data.get(src_addr)
        if d is None:
            d = {"t0": time.time(), "T": deque(maxlen=TagPlot.MAXLEN)}
            for k in self._KEYS:
                d[k] = deque(maxlen=TagPlot.MAXLEN)
            self._data[src_addr] = d
            self._cbo_node.addItem(f"0x{src_addr:08x}", src_addr)
            if self._cbo_node.count() == 1:
                self._cbo_node.setCurrentIndex(0)
        return d

    def feed_tag(self, src_addr: int, vals: list):
        """EP 20 packet: [T, P, H, gas, ax, ay, az, gx, gy, gz]."""
        if len(vals) < 10:
            return
        d = self._node(src_addr)
        d["T"].append(time.time() - d["t0"])
        for k, v in zip(self._KEYS, vals[:10]):
            d[k].append(v)
        d["last"] = vals[:10]
        if self._selected_addr() == src_addr:
            self._refresh()

    def _selected_addr(self):
        idx = self._cbo_node.currentIndex()
        return self._cbo_node.itemData(idx) if idx >= 0 else None

    def _refresh(self):
        addr = self._selected_addr()
        d = self._data.get(addr) if addr is not None else None
        if d is None:
            self._plot.clear()
            self._lbl_vals.setText("—")
            return
        self._plot.redraw(d)
        v = d.get("last")
        if v:
            self._lbl_vals.setText(
                f"T={v[0]:.2f}°C  P={v[1]:.0f}Pa  H={v[2]:.1f}%  "
                f"gas={v[3]:.0f}Ω  acc=({v[4]:.0f},{v[5]:.0f},{v[6]:.0f})mg  "
                f"gyr=({v[7]:.0f},{v[8]:.0f},{v[9]:.0f})")

    def _on_node_changed(self, _idx: int):
        self._refresh()

    def _on_clear(self):
        addr = self._selected_addr()
        if addr is not None and addr in self._data:
            d = self._data[addr]
            d["T"].clear()
            for k in self._KEYS:
                d[k].clear()
            d.pop("last", None)
            d["t0"] = time.time()
        self._refresh()

    def closeEvent(self, event):
        self.visibilityChanged.emit(False)
        super().closeEvent(event)


# ─── Signal bridge (rx thread → GUI thread) ───────────────────────────────────
class _Signals(QObject):
    rx_packet     = Signal(dict)   # RX_IND decoded dict
    tx_ind        = Signal(dict)   # TX_IND decoded dict
    log_message   = Signal(str)    # plain log line
    node_info     = Signal(dict)   # CSAP attrs dict
    otap_progress = Signal(int)    # upload percentage
    otap_status   = Signal(dict)   # scratchpad status dict
    otap_done     = Signal(bool)   # upload finished (ok)
    appconfig     = Signal(dict)   # AppConfig read result
    otap_target   = Signal(dict)   # OTAP target read result
    node_cfg_result  = Signal(str, str)   # (message, css-color)
    # NOTE: src_addr is a 32-bit Wirepas address that can exceed 2**31, which
    # overflows PySide6's C++ `int`. Use `object` so the Python int passes through.
    remote_scratch   = Signal(object, object) # (src_addr, parsed status dict)
    net_uplink_done  = Signal(str, str)  # (summary, css-color) — batch send finished
    mqtt_discovered  = Signal(object, object)  # (conn, gateway-list) from bg thread


# ─── Main window ──────────────────────────────────────────────────────────────
class MainWindow(QMainWindow):
    def __init__(self, initial_port: str = "", initial_baud: int = 115200):
        super().__init__()
        self._conn: Optional[WapsConn] = None
        self._node_model = NodeTableModel()
        self._signals = _Signals()
        self._signals.rx_packet.connect(self._on_rx_packet)
        self._signals.tx_ind.connect(self._on_tx_ind)
        self._signals.log_message.connect(self._on_log)
        self._signals.node_info.connect(self._on_node_info)
        self._signals.otap_progress.connect(self._otap_progress_set)
        self._signals.otap_status.connect(self._on_otap_status_result)
        self._signals.otap_done.connect(self._on_otap_done)
        self._signals.appconfig.connect(self._on_appconfig_update)
        self._signals.otap_target.connect(self._on_otap_target_update)
        self._signals.node_cfg_result.connect(self._on_node_cfg_result)
        self._signals.remote_scratch.connect(self._on_remote_scratch_status)
        self._signals.net_uplink_done.connect(self._on_net_uplink_done)
        self._signals.mqtt_discovered.connect(self._on_mqtt_discovered)

        self._otap_processed_seq: int = 0  # last known processed scratchpad seq
        self._diag_role_seen: dict = {}    # addr → last logged diagnostic role_raw
        self._csap_role_nodes: set = set() # addrs with authoritative CSAP role read

        # Polling timer — sends INDICATION_POLL_REQ at ~500ms intervals
        self._poll_timer = QTimer()
        self._poll_timer.setInterval(500)
        self._poll_timer.timeout.connect(self._on_poll_timer)

        # Fragment reassembly for RX_FRAG_IND (dualmcu forces fragmented mode)
        self._reasm = FragReassembler()
        self._poll_busy = False
        self._poll_count = 0
        self._poll_last_ok = None
        self._upload_busy = False

        self._log_window = LogWindow()
        self._log_window.visibilityChanged.connect(self._on_log_window_closed)

        self._motor_window = MotorWindow()
        self._motor_window.set_send_fn(self._motor_send)
        self._motor_window.visibilityChanged.connect(self._on_motor_window_closed)

        self._sensor_window = SensorWindow()
        self._sensor_window.visibilityChanged.connect(self._on_sensor_window_closed)

        self._tag_window = TagWindow()
        self._tag_window.visibilityChanged.connect(self._on_tag_window_closed)

        self.setWindowTitle("Wirepas UART Console")
        self.resize(1280, 780)
        self._build_ui(initial_port, initial_baud)
        self._set_connected(False)

    # ── UI construction ──────────────────────────────────────────────────────

    def _build_ui(self, port: str, baud: int):
        central = QWidget()
        self.setCentralWidget(central)
        root = QVBoxLayout(central)
        root.setSpacing(0)
        root.setContentsMargins(8, 8, 8, 4)
        root.addWidget(self._build_main_tabs(port, baud), stretch=1)

        self._status_bar = QStatusBar()
        self.setStatusBar(self._status_bar)
        self._lbl_status = QLabel("Disconnected")
        self._lbl_status.setObjectName("lbl_status_err")
        self._status_bar.addWidget(self._lbl_status)
        self._lbl_status_node = QLabel("")
        self._lbl_status_node.setStyleSheet("color:#5a9fd4; font-family:'Courier New';")
        self._status_bar.addPermanentWidget(self._lbl_status_node)

    def _build_main_tabs(self, port: str = "", baud: int = 125000) -> QTabWidget:
        self._main_tabs = QTabWidget()
        self._main_tabs.addTab(self._build_traffic_tab(),           "Traffic")
        self._main_tabs.addTab(self._build_network_tab(),           "Network")
        self._main_tabs.addTab(self._build_remote_tab(),            "Remote Config")
        self._main_tabs.addTab(self._build_appconfig_tab(),         "AppConfig")
        self._main_tabs.addTab(self._build_otap_tab(),              "OTAP")
        self._main_tabs.addTab(self._build_config_tab(port, baud),  "Config")
        return self._main_tabs

    def _build_config_tab(self, port: str, baud: int) -> QWidget:
        w = QWidget()
        lay = QVBoxLayout(w)
        lay.setSpacing(10)

        # ── Transport selector ────────────────────────────────────────────────
        self._cbo_conn_mode = QComboBox()
        self._cbo_conn_mode.addItem("Serial (UART sink)", "serial")
        self._cbo_conn_mode.addItem("MQTT backend (broker)", "mqtt")
        self._cbo_conn_mode.currentIndexChanged.connect(self._on_conn_mode_changed)
        mode_grp = QGroupBox("Transport")
        mode_lay = QFormLayout(mode_grp)
        mode_lay.addRow("Connect via:", self._cbo_conn_mode)
        lay.addWidget(mode_grp)

        # ── UART connection ────────────────────────────────────────────────────
        conn_grp = QGroupBox("UART Connection")
        self._grp_serial = conn_grp
        conn_lay = QFormLayout(conn_grp)
        conn_lay.setSpacing(8)

        port_row = QHBoxLayout()
        self._cbo_port = QComboBox()
        self._cbo_port.setEditable(True)
        self._cbo_port.setMinimumWidth(200)
        self._refresh_ports()
        if port:
            self._cbo_port.setCurrentText(port)
        port_row.addWidget(self._cbo_port, stretch=1)
        btn_refresh = QPushButton("⟳")
        btn_refresh.setFixedWidth(30)
        btn_refresh.setToolTip("Refresh port list")
        btn_refresh.clicked.connect(self._refresh_ports)
        port_row.addWidget(btn_refresh)
        conn_lay.addRow("Port:", port_row)

        self._cbo_baud = QComboBox()
        for b in [9600, 19200, 38400, 57600, 115200, 125000, 230400, 460800, 921600]:
            self._cbo_baud.addItem(str(b))
        self._cbo_baud.setCurrentText(str(baud))
        self._cbo_baud.setMinimumWidth(110)
        conn_lay.addRow("Baud rate:", self._cbo_baud)

        timeout_row = QHBoxLayout()
        self._spn_timeout = QSpinBox()
        self._spn_timeout.setRange(1, 30)
        self._spn_timeout.setValue(3)
        self._spn_timeout.setSuffix(" s")
        self._spn_timeout.setFixedWidth(80)
        timeout_row.addWidget(self._spn_timeout)
        timeout_row.addSpacing(20)
        self._chk_debug = QCheckBox("Debug (log all unhandled frames)")
        self._chk_debug.setChecked(True)
        timeout_row.addWidget(self._chk_debug)
        timeout_row.addStretch()
        conn_lay.addRow("Timeout:", timeout_row)

        lay.addWidget(conn_grp)

        # ── MQTT backend connection ────────────────────────────────────────────
        mqtt_grp = QGroupBox("MQTT Backend Connection")
        self._grp_mqtt = mqtt_grp
        mqtt_lay = QFormLayout(mqtt_grp)
        mqtt_lay.setSpacing(8)

        self._txt_mqtt_host = QLineEdit()
        self._txt_mqtt_host.setPlaceholderText("broker.example.com")
        self._txt_mqtt_host.setText("wnt.dev.bienesis.fr")
        mqtt_lay.addRow("Host:", self._txt_mqtt_host)

        self._spn_mqtt_port = QSpinBox()
        self._spn_mqtt_port.setRange(1, 65535)
        self._spn_mqtt_port.setValue(8883)
        self._spn_mqtt_port.setFixedWidth(90)
        tls_row = QHBoxLayout()
        tls_row.addWidget(self._spn_mqtt_port)
        tls_row.addSpacing(16)
        self._chk_mqtt_tls = QCheckBox("TLS")
        self._chk_mqtt_tls.setChecked(True)
        self._chk_mqtt_tls.toggled.connect(
            lambda on: self._spn_mqtt_port.setValue(8883 if on else 1883))
        tls_row.addWidget(self._chk_mqtt_tls)
        self._chk_mqtt_insecure = QCheckBox("Skip cert verify")
        tls_row.addWidget(self._chk_mqtt_insecure)
        tls_row.addStretch()
        mqtt_lay.addRow("Port:", tls_row)

        self._txt_mqtt_user = QLineEdit()
        self._txt_mqtt_user.setPlaceholderText("username")
        self._txt_mqtt_user.setText("mqttmasteruser")
        mqtt_lay.addRow("Username:", self._txt_mqtt_user)

        self._txt_mqtt_pass = QLineEdit()
        self._txt_mqtt_pass.setEchoMode(QLineEdit.EchoMode.Password)
        self._txt_mqtt_pass.setPlaceholderText("password")
        mqtt_lay.addRow("Password:", self._txt_mqtt_pass)

        gw_row = QHBoxLayout()
        self._cbo_mqtt_gw = QComboBox()
        self._cbo_mqtt_gw.setEditable(True)
        self._cbo_mqtt_gw.setMinimumWidth(200)
        self._cbo_mqtt_gw.setToolTip("Gateway id (discovered on connect, or typed)")
        self._cbo_mqtt_gw.currentIndexChanged.connect(self._on_mqtt_gw_changed)
        gw_row.addWidget(self._cbo_mqtt_gw, stretch=1)
        self._btn_mqtt_refresh = QPushButton("⟳")
        self._btn_mqtt_refresh.setFixedWidth(30)
        self._btn_mqtt_refresh.setToolTip("Refresh gateways / sinks")
        self._btn_mqtt_refresh.clicked.connect(self._on_mqtt_refresh_sinks)
        gw_row.addWidget(self._btn_mqtt_refresh)
        self._btn_mqtt_sniff = QPushButton("Sniff")
        self._btn_mqtt_sniff.setFixedWidth(50)
        self._btn_mqtt_sniff.setToolTip("List topics actually published on the "
                                        "broker for 6 s (diagnostic)")
        self._btn_mqtt_sniff.clicked.connect(self._on_mqtt_sniff)
        gw_row.addWidget(self._btn_mqtt_sniff)
        mqtt_lay.addRow("Gateway:", gw_row)

        self._cbo_mqtt_sink = QComboBox()
        self._cbo_mqtt_sink.setEditable(True)
        self._cbo_mqtt_sink.setMinimumWidth(200)
        self._cbo_mqtt_sink.setEditText("sink0")
        self._cbo_mqtt_sink.setToolTip("Sink id (default sink0)")
        self._cbo_mqtt_sink.currentIndexChanged.connect(self._on_mqtt_sink_changed)
        mqtt_lay.addRow("Sink:", self._cbo_mqtt_sink)

        lay.addWidget(mqtt_grp)
        mqtt_grp.setVisible(False)   # serial is the default transport

        # ── Shared connection actions (visible in both transports) ─────────────
        act_grp = QGroupBox("Connection")
        act_lay = QVBoxLayout(act_grp)

        self._chk_show_log = QCheckBox("Show log window")
        self._chk_show_log.setChecked(False)
        self._chk_show_log.toggled.connect(self._on_log_toggle)
        act_lay.addWidget(self._chk_show_log)

        self._chk_show_motor = QCheckBox("Show motor control window")
        self._chk_show_motor.setChecked(False)
        self._chk_show_motor.toggled.connect(self._on_motor_toggle)
        act_lay.addWidget(self._chk_show_motor)

        self._chk_show_sensor = QCheckBox("Show sensor plot window (CTN EP 11)")
        self._chk_show_sensor.setChecked(False)
        self._chk_show_sensor.toggled.connect(self._on_sensor_toggle)
        act_lay.addWidget(self._chk_show_sensor)

        self._chk_show_tag = QCheckBox("Show nRF54L15-tag plot window (EP 20)")
        self._chk_show_tag.setChecked(False)
        self._chk_show_tag.toggled.connect(self._on_tag_toggle)
        act_lay.addWidget(self._chk_show_tag)

        btn_row = QHBoxLayout()
        self._btn_connect = QPushButton("Connect")
        self._btn_connect.setObjectName("btn_connect")
        self._btn_connect.clicked.connect(self._on_connect)
        btn_row.addWidget(self._btn_connect)
        self._btn_disconnect = QPushButton("Disconnect")
        self._btn_disconnect.setObjectName("btn_disconnect")
        self._btn_disconnect.clicked.connect(self._on_disconnect)
        btn_row.addWidget(self._btn_disconnect)
        btn_row.addStretch()
        act_lay.addLayout(btn_row)
        lay.addWidget(act_grp)

        # ── Node info (read-only) ─────────────────────────────────────────────
        info_grp = QGroupBox("Node info")
        info_lay = QFormLayout(info_grp)
        info_lay.setSpacing(6)

        def _info_lbl():
            lbl = QLabel("—")
            lbl.setStyleSheet("color: #5a9fd4; font-weight: bold; font-family: 'Courier New';")
            return lbl

        self._lbl_node_addr   = _info_lbl()
        self._lbl_net_addr    = _info_lbl()
        self._lbl_channel     = _info_lbl()
        self._lbl_role        = _info_lbl()
        self._lbl_fw          = _info_lbl()
        self._lbl_appconf_seq = _info_lbl()
        self._lbl_appconf_int = _info_lbl()
        self._lbl_stack_state = _info_lbl()

        info_lay.addRow("Node address:", self._lbl_node_addr)
        info_lay.addRow("Network address:", self._lbl_net_addr)
        info_lay.addRow("RF Channel:", self._lbl_channel)
        info_lay.addRow("Role:", self._lbl_role)
        info_lay.addRow("Firmware:", self._lbl_fw)
        info_lay.addRow("AppCfg seq:", self._lbl_appconf_seq)
        info_lay.addRow("AppCfg interval:", self._lbl_appconf_int)
        info_lay.addRow("Stack:", self._lbl_stack_state)

        info_btn_row = QHBoxLayout()
        btn_read_info = QPushButton("Read node info")
        btn_read_info.clicked.connect(self._on_read_node_info)
        info_btn_row.addWidget(btn_read_info)
        btn_read_ac = QPushButton("Read AppConfig")
        btn_read_ac.clicked.connect(self._on_read_appconfig)
        info_btn_row.addWidget(btn_read_ac)
        info_btn_row.addStretch()
        info_lay.addRow("", info_btn_row)
        lay.addWidget(info_grp)

        # ── Node parameters (write) ───────────────────────────────────────────
        cfg_grp = QGroupBox("Node parameters (CSAP write)")
        cfg_lay = QFormLayout(cfg_grp)
        cfg_lay.setSpacing(8)

        self._cbo_role = QComboBox()
        for name, val in [("Sink LE", NodeRole.SINK_LE),
                           ("Headnode LE", NodeRole.HEADNODE_LE),
                           ("Subnode LE", NodeRole.SUBNODE_LE),
                           ("Sink LL", NodeRole.SINK_LL),
                           ("Headnode LL", NodeRole.HEADNODE_LL),
                           ("Subnode LL", NodeRole.SUBNODE_LL)]:
            self._cbo_role.addItem(name, val)
        cfg_lay.addRow("Role:", self._cbo_role)

        self._txt_cfg_node_addr = QLineEdit()
        self._txt_cfg_node_addr.setPlaceholderText("0x00000001")
        self._txt_cfg_node_addr.setFont(QFont("Courier New", 12))
        cfg_lay.addRow("Node address:", self._txt_cfg_node_addr)

        self._txt_cfg_net_addr = QLineEdit()
        self._txt_cfg_net_addr.setPlaceholderText("0xABCDEF  (24-bit)")
        self._txt_cfg_net_addr.setFont(QFont("Courier New", 12))
        cfg_lay.addRow("Network address:", self._txt_cfg_net_addr)

        self._spn_cfg_channel = QSpinBox()
        self._spn_cfg_channel.setRange(1, 40)
        self._spn_cfg_channel.setValue(7)
        self._spn_cfg_channel.setFixedWidth(70)
        cfg_lay.addRow("RF Channel:", self._spn_cfg_channel)

        apply_row = QHBoxLayout()
        self._lbl_cfg_result = QLabel("")
        apply_row.addWidget(self._lbl_cfg_result, stretch=1)
        btn_apply = QPushButton("Apply config")
        btn_apply.clicked.connect(self._on_apply_node_config)
        apply_row.addWidget(btn_apply)
        cfg_lay.addRow("", apply_row)
        lay.addWidget(cfg_grp)

        # ── Stack control ─────────────────────────────────────────────────────
        stack_grp = QGroupBox("Stack control")
        stack_lay = QHBoxLayout(stack_grp)

        self._btn_stack_start = QPushButton("Start stack")
        self._btn_stack_start.setObjectName("btn_connect")
        self._btn_stack_start.clicked.connect(self._on_stack_start)
        stack_lay.addWidget(self._btn_stack_start)

        self._btn_stack_stop = QPushButton("Stop stack")
        self._btn_stack_stop.setObjectName("btn_disconnect")
        self._btn_stack_stop.clicked.connect(self._on_stack_stop)
        stack_lay.addWidget(self._btn_stack_stop)

        stack_lay.addSpacing(20)
        self._chk_poll = QCheckBox("Auto-poll indications (500 ms)")
        self._chk_poll.setChecked(True)
        self._chk_poll.toggled.connect(self._on_poll_toggle)
        stack_lay.addWidget(self._chk_poll)
        stack_lay.addStretch()
        lay.addWidget(stack_grp)

        lay.addStretch()
        return w

    def _build_traffic_tab(self) -> QWidget:
        """Merged downlink (RX) + uplink (TX) view."""
        splitter = QSplitter(Qt.Orientation.Vertical)
        splitter.addWidget(self._build_rx_tab())
        tx_w = self._build_tx_tab()
        splitter.addWidget(tx_w)
        splitter.setStretchFactor(0, 1)
        splitter.setStretchFactor(1, 0)
        splitter.setCollapsible(1, False)
        QTimer.singleShot(0, lambda: splitter.setSizes(
            [splitter.height() - tx_w.sizeHint().height() - splitter.handleWidth(),
             tx_w.sizeHint().height()]))
        return splitter

    def _build_network_tab(self) -> QWidget:
        w   = QWidget()
        lay = QVBoxLayout(w)
        lay.setContentsMargins(6, 6, 6, 6)
        lay.setSpacing(4)

        # ── Toolbar ──────────────────────────────────────────────────────────
        toolbar = QHBoxLayout()
        self._btn_net_scan = QPushButton("⟳ Scan (broadcast)")
        self._btn_net_scan.setObjectName("btn_send")
        self._btn_net_scan.clicked.connect(self._on_net_scan)
        toolbar.addWidget(self._btn_net_scan)

        self._btn_net_clear = QPushButton("Clear")
        self._btn_net_clear.clicked.connect(self._on_net_clear)
        toolbar.addWidget(self._btn_net_clear)

        toolbar.addWidget(QLabel("Filter:"))
        self._txt_net_filter = QLineEdit()
        self._txt_net_filter.setPlaceholderText("address / endpoint / fw…")
        self._txt_net_filter.textChanged.connect(self._on_net_filter)
        toolbar.addWidget(self._txt_net_filter, stretch=1)

        self._btn_net_csv = QPushButton("Export CSV")
        self._btn_net_csv.clicked.connect(self._on_net_export_csv)
        toolbar.addWidget(self._btn_net_csv)

        self._btn_net_json = QPushButton("Export JSON")
        self._btn_net_json.clicked.connect(self._on_net_export_json)
        toolbar.addWidget(self._btn_net_json)

        lay.addLayout(toolbar)

        # ── Node table ───────────────────────────────────────────────────────
        self._net_proxy = QSortFilterProxyModel()
        self._net_proxy.setSourceModel(self._node_model)
        self._net_proxy.setFilterCaseSensitivity(Qt.CaseInsensitive)
        self._net_proxy.setFilterKeyColumn(-1)  # search all columns

        self._net_table = QTableView()
        self._net_table.setModel(self._net_proxy)
        self._net_table.setSortingEnabled(True)
        self._net_table.setSelectionBehavior(QTableView.SelectRows)
        self._net_table.setSelectionMode(QTableView.ExtendedSelection)
        self._net_table.setAlternatingRowColors(True)
        self._net_table.horizontalHeader().setSectionResizeMode(QHeaderView.ResizeToContents)
        self._net_table.horizontalHeader().setStretchLastSection(True)
        self._net_table.verticalHeader().setVisible(False)
        self._net_table.setStyleSheet(
            f"QTableView {{ font-family:'Courier New'; font-size:12px; }}"
            f"QTableView::item:selected {{ background:{COL_SEL}; color:#ffffff; }}"
        )
        self._net_table.selectionModel().selectionChanged.connect(self._on_net_selection_changed)
        self._net_table.clicked.connect(self._on_net_cell_clicked)
        lay.addWidget(self._net_table, stretch=1)

        # ── Uplink panel (shown when ≥1 row selected) ────────────────────────
        self._net_uplink_grp = QGroupBox("Uplink to selected nodes")
        self._net_uplink_grp.setEnabled(False)
        ul_lay = QHBoxLayout(self._net_uplink_grp)
        ul_lay.setSpacing(6)

        self._lbl_net_ul_targets = QLabel("—")
        self._lbl_net_ul_targets.setStyleSheet(
            f"color:{COL_DIM}; font-family:'Courier New'; font-size:11px;")
        ul_lay.addWidget(self._lbl_net_ul_targets)

        ul_lay.addWidget(QLabel("src ep:"))
        self._spn_net_ul_src = QSpinBox()
        self._spn_net_ul_src.setRange(1, 254)
        self._spn_net_ul_src.setValue(1)
        self._spn_net_ul_src.setFixedWidth(55)
        ul_lay.addWidget(self._spn_net_ul_src)

        ul_lay.addWidget(QLabel("dst ep:"))
        self._spn_net_ul_dst = QSpinBox()
        self._spn_net_ul_dst.setRange(1, 254)
        self._spn_net_ul_dst.setValue(10)
        self._spn_net_ul_dst.setFixedWidth(55)
        ul_lay.addWidget(self._spn_net_ul_dst)

        ul_lay.addWidget(QLabel("Payload (hex):"))
        self._txt_net_ul_payload = QLineEdit()
        self._txt_net_ul_payload.setPlaceholderText("e.g. deadbeef")
        ul_lay.addWidget(self._txt_net_ul_payload, stretch=1)

        self._btn_net_ul_send = QPushButton("Send ▶")
        self._btn_net_ul_send.setObjectName("btn_send")
        self._btn_net_ul_send.clicked.connect(self._on_net_uplink_send)
        ul_lay.addWidget(self._btn_net_ul_send)

        self._lbl_net_ul_result = QLabel("")
        ul_lay.addWidget(self._lbl_net_ul_result)

        lay.addWidget(self._net_uplink_grp)
        return w

    def _build_otap_tab(self) -> QWidget:
        w = QWidget()
        lay = QVBoxLayout(w)
        lay.setSpacing(10)

        # File selection
        file_grp = QGroupBox("Scratchpad file")
        file_lay = QHBoxLayout(file_grp)
        self._txt_otap_file = QLineEdit()
        self._txt_otap_file.setPlaceholderText("Select a .otap scratchpad file…")
        self._txt_otap_file.setFont(QFont("Courier New", 11))
        file_lay.addWidget(self._txt_otap_file, stretch=1)
        btn_browse = QPushButton("Browse…")
        btn_browse.clicked.connect(self._on_otap_browse)
        file_lay.addWidget(btn_browse)
        lay.addWidget(file_grp)

        # Upload parameters
        param_grp = QGroupBox("Upload")
        param_lay = QVBoxLayout(param_grp)

        seq_row = QHBoxLayout()
        seq_row.addWidget(QLabel("Sequence:"))
        self._spn_otap_seq = QSpinBox()
        self._spn_otap_seq.setRange(1, 254)
        self._spn_otap_seq.setValue(1)
        self._spn_otap_seq.setFixedWidth(70)
        self._spn_otap_seq.valueChanged.connect(self._check_otap_seq_conflict)
        seq_row.addWidget(self._spn_otap_seq)
        self._lbl_seq_warn = QLabel("")
        self._lbl_seq_warn.setStyleSheet(f"color:{COL_ERR}; font-weight:bold;")
        seq_row.addWidget(self._lbl_seq_warn)
        seq_row.addSpacing(20)

        self._chk_otap_process = QCheckBox("Set target to process after upload")
        self._chk_otap_process.setChecked(True)
        self._chk_otap_process.setToolTip(
            "After upload, set the scratchpad processing target (propagate & process)")
        seq_row.addWidget(self._chk_otap_process)
        seq_row.addStretch()
        param_lay.addLayout(seq_row)

        self._otap_progress = QProgressBar()
        self._otap_progress.setRange(0, 100)
        self._otap_progress.setValue(0)
        self._otap_progress.setFormat("%p%")
        param_lay.addWidget(self._otap_progress)

        btn_row = QHBoxLayout()
        self._btn_otap_status = QPushButton("Read status")
        self._btn_otap_status.clicked.connect(self._on_otap_status)
        btn_row.addWidget(self._btn_otap_status)

        self._btn_otap_clear = QPushButton("Clear")
        self._btn_otap_clear.clicked.connect(self._on_otap_clear)
        btn_row.addWidget(self._btn_otap_clear)

        btn_row.addStretch()
        self._btn_otap_upload = QPushButton("  Upload ▶")
        self._btn_otap_upload.setObjectName("btn_send")
        self._btn_otap_upload.setMinimumWidth(120)
        self._btn_otap_upload.clicked.connect(self._on_otap_upload)
        btn_row.addWidget(self._btn_otap_upload)
        param_lay.addLayout(btn_row)
        lay.addWidget(param_grp)

        # Status display
        status_grp = QGroupBox("Scratchpad status (from node)")
        status_lay = QFormLayout(status_grp)
        self._lbl_otap_stored = QLabel("—")
        self._lbl_otap_processed = QLabel("—")
        self._lbl_otap_fw = QLabel("—")
        for lbl in (self._lbl_otap_stored, self._lbl_otap_processed, self._lbl_otap_fw):
            lbl.setStyleSheet("font-family: 'Courier New'; color: #5a9fd4;")
        status_lay.addRow("Stored:", self._lbl_otap_stored)
        status_lay.addRow("Processed:", self._lbl_otap_processed)
        status_lay.addRow("Firmware:", self._lbl_otap_fw)
        lay.addWidget(status_grp)

        # Target configuration
        target_grp = QGroupBox("Target (scratchpad processing)")
        target_lay = QVBoxLayout(target_grp)

        target_form = QHBoxLayout()

        # Seq
        target_form.addWidget(QLabel("Seq:"))
        self._spn_target_seq = QSpinBox()
        self._spn_target_seq.setRange(0, 254)
        self._spn_target_seq.setValue(1)
        self._spn_target_seq.setFixedWidth(60)
        target_form.addWidget(self._spn_target_seq)
        target_form.addSpacing(12)

        # CRC
        target_form.addWidget(QLabel("CRC:"))
        self._txt_target_crc = QLineEdit("0000")
        self._txt_target_crc.setFixedWidth(60)
        self._txt_target_crc.setFont(QFont("Courier New", 11))
        self._txt_target_crc.setPlaceholderText("hex")
        target_form.addWidget(self._txt_target_crc)
        target_form.addSpacing(12)

        # Action
        target_form.addWidget(QLabel("Action:"))
        self._cmb_target_action = QComboBox()
        _ACTION_LABELS = [
            (0, "0 — No OTAP"),
            (1, "1 — Propagate only"),
            (2, "2 — Propagate & process"),
            (3, "3 — Propagate & process with delay"),
            (5, "5 — Legacy"),
        ]
        for val, label in _ACTION_LABELS:
            self._cmb_target_action.addItem(label, userData=val)
        self._cmb_target_action.setCurrentIndex(2)  # default: propagate & process
        self._cmb_target_action.currentIndexChanged.connect(self._on_target_action_changed)
        target_form.addWidget(self._cmb_target_action)
        target_form.addSpacing(12)

        # Param (delay, only visible for action=3)
        self._lbl_target_param = QLabel("Delay (min):")
        self._spn_target_param = QSpinBox()
        self._spn_target_param.setRange(0, 255)
        self._spn_target_param.setValue(0)
        self._spn_target_param.setFixedWidth(60)
        target_form.addWidget(self._lbl_target_param)
        target_form.addWidget(self._spn_target_param)
        self._lbl_target_param.setVisible(False)
        self._spn_target_param.setVisible(False)
        target_form.addStretch()
        target_lay.addLayout(target_form)

        btn_target_row = QHBoxLayout()
        btn_read_target = QPushButton("Read target")
        btn_read_target.clicked.connect(self._on_otap_read_target)
        btn_target_row.addWidget(btn_read_target)
        btn_set_target = QPushButton("Set target")
        btn_set_target.setObjectName("btn_send")
        btn_set_target.clicked.connect(self._on_otap_set_target)
        btn_target_row.addWidget(btn_set_target)
        self._lbl_target_result = QLabel("")
        btn_target_row.addWidget(self._lbl_target_result)
        btn_target_row.addStretch()
        target_lay.addLayout(btn_target_row)
        lay.addWidget(target_grp)

        # Remote node OTAP status (received via Remote API, src_ep=240 dst_ep=255)
        remote_grp = QGroupBox("Remote node scratchpad status (Remote API)")
        remote_lay = QVBoxLayout(remote_grp)

        addr_row = QHBoxLayout()
        addr_row.addWidget(QLabel("Node address:"))
        self._txt_remote_addr = QLineEdit()
        self._txt_remote_addr.setPlaceholderText("e.g. 111 or 0x6F")
        self._txt_remote_addr.setFixedWidth(120)
        addr_row.addWidget(self._txt_remote_addr)
        self._btn_remote_poll = QPushButton("Poll status")
        self._btn_remote_poll.clicked.connect(self._on_remote_poll)
        addr_row.addWidget(self._btn_remote_poll)
        addr_row.addStretch()
        remote_lay.addLayout(addr_row)

        remote_form = QFormLayout()
        remote_form.setLabelAlignment(Qt.AlignRight)
        self._lbl_remote_stored   = QLabel("—")
        self._lbl_remote_proc     = QLabel("—")
        self._lbl_remote_action   = QLabel("—")
        self._lbl_remote_result   = QLabel("")
        for lbl in (self._lbl_remote_stored, self._lbl_remote_proc, self._lbl_remote_action):
            lbl.setStyleSheet("font-family:'Courier New'; color:#5a9fd4;")
        remote_form.addRow("Stored:",    self._lbl_remote_stored)
        remote_form.addRow("Processed:", self._lbl_remote_proc)
        remote_form.addRow("Action:",    self._lbl_remote_action)
        remote_form.addRow("",           self._lbl_remote_result)
        remote_lay.addLayout(remote_form)
        lay.addWidget(remote_grp)

        lay.addStretch()
        return w

    def _build_rx_tab(self) -> QWidget:
        w = QWidget()
        lay = QVBoxLayout(w)
        lay.setSpacing(4)

        toolbar = QHBoxLayout()
        lbl = QLabel("Received downlink messages")
        lbl.setStyleSheet("color: #888;")
        toolbar.addWidget(lbl)
        toolbar.addStretch()
        toolbar.addWidget(QLabel("Buffer:"))
        self._spn_rx_buf = QSpinBox()
        self._spn_rx_buf.setRange(50, 10000)
        self._spn_rx_buf.setValue(500)
        self._spn_rx_buf.setSingleStep(100)
        self._spn_rx_buf.setSuffix(" rows")
        self._spn_rx_buf.setFixedWidth(100)
        self._spn_rx_buf.setToolTip("Maximum rows kept in the downlink table")
        self._spn_rx_buf.valueChanged.connect(self._on_rx_buf_changed)
        toolbar.addWidget(self._spn_rx_buf)
        self._chk_autoscroll = QCheckBox("Auto-scroll")
        self._chk_autoscroll.setChecked(True)
        toolbar.addWidget(self._chk_autoscroll)
        btn_clear = QPushButton("Clear")
        btn_clear.clicked.connect(self._on_rx_clear)
        toolbar.addWidget(btn_clear)
        btn_copy = QPushButton("Copy hex")
        btn_copy.clicked.connect(self._on_rx_copy_hex)
        toolbar.addWidget(btn_copy)
        lay.addLayout(toolbar)

        # ── Filter row 1: Type / EP / Node / Hops ───────────────────────────
        fbar1 = QHBoxLayout()
        fbar1.setSpacing(6)

        fbar1.addWidget(QLabel("Type:"))
        self._cmb_rx_type = QComboBox()
        self._cmb_rx_type.addItems(["All", "UCAST", "BCAST", "MCAST"])
        self._cmb_rx_type.setFixedWidth(72)
        fbar1.addWidget(self._cmb_rx_type)

        fbar1.addWidget(QLabel("Src EP:"))
        self._edt_rx_src_ep = QLineEdit()
        self._edt_rx_src_ep.setPlaceholderText("247")
        self._edt_rx_src_ep.setFixedWidth(52)
        fbar1.addWidget(self._edt_rx_src_ep)

        fbar1.addWidget(QLabel("Dst EP:"))
        self._edt_rx_dst_ep = QLineEdit()
        self._edt_rx_dst_ep.setPlaceholderText("255")
        self._edt_rx_dst_ep.setFixedWidth(52)
        fbar1.addWidget(self._edt_rx_dst_ep)

        fbar1.addWidget(QLabel("Src addr:"))
        self._edt_rx_src = QLineEdit()
        self._edt_rx_src.setPlaceholderText("dec or 0x…")
        self._edt_rx_src.setFixedWidth(100)
        fbar1.addWidget(self._edt_rx_src)

        fbar1.addWidget(QLabel("Dst addr:"))
        self._edt_rx_dst = QLineEdit()
        self._edt_rx_dst.setPlaceholderText("dec or 0x…")
        self._edt_rx_dst.setFixedWidth(100)
        fbar1.addWidget(self._edt_rx_dst)

        fbar1.addWidget(QLabel("Hops ≤:"))
        self._spn_rx_hops = QSpinBox()
        self._spn_rx_hops.setRange(0, 15)
        self._spn_rx_hops.setValue(0)
        self._spn_rx_hops.setSpecialValueText("∞")
        self._spn_rx_hops.setFixedWidth(52)
        fbar1.addWidget(self._spn_rx_hops)

        fbar1.addStretch()
        lay.addLayout(fbar1)

        # ── Filter row 2: QoS / Delay / Bytes / Search ───────────────────────
        fbar2 = QHBoxLayout()
        fbar2.setSpacing(6)

        fbar2.addWidget(QLabel("QoS:"))
        self._cmb_rx_qos = QComboBox()
        self._cmb_rx_qos.addItems(["All", "0", "1", "2", "3"])
        self._cmb_rx_qos.setFixedWidth(56)
        fbar2.addWidget(self._cmb_rx_qos)

        fbar2.addWidget(QLabel("Delay ms ≥:"))
        self._spn_rx_delay_min = QSpinBox()
        self._spn_rx_delay_min.setRange(0, 999999)
        self._spn_rx_delay_min.setValue(0)
        self._spn_rx_delay_min.setSpecialValueText("∞")
        self._spn_rx_delay_min.setFixedWidth(72)
        fbar2.addWidget(self._spn_rx_delay_min)

        fbar2.addWidget(QLabel("≤"))
        self._spn_rx_delay_max = QSpinBox()
        self._spn_rx_delay_max.setRange(0, 999999)
        self._spn_rx_delay_max.setValue(0)
        self._spn_rx_delay_max.setSpecialValueText("∞")
        self._spn_rx_delay_max.setFixedWidth(72)
        fbar2.addWidget(self._spn_rx_delay_max)

        fbar2.addWidget(QLabel("Bytes ≥:"))
        self._spn_rx_bytes_min = QSpinBox()
        self._spn_rx_bytes_min.setRange(0, 65535)
        self._spn_rx_bytes_min.setValue(0)
        self._spn_rx_bytes_min.setSpecialValueText("∞")
        self._spn_rx_bytes_min.setFixedWidth(60)
        fbar2.addWidget(self._spn_rx_bytes_min)

        fbar2.addWidget(QLabel("≤"))
        self._spn_rx_bytes_max = QSpinBox()
        self._spn_rx_bytes_max.setRange(0, 65535)
        self._spn_rx_bytes_max.setValue(0)
        self._spn_rx_bytes_max.setSpecialValueText("∞")
        self._spn_rx_bytes_max.setFixedWidth(60)
        fbar2.addWidget(self._spn_rx_bytes_max)

        fbar2.addWidget(QLabel("Search:"))
        self._edt_rx_search = QLineEdit()
        self._edt_rx_search.setPlaceholderText("hex / CBOR / ASCII…")
        self._edt_rx_search.setToolTip("Case-insensitive search in payload hex, CBOR and ASCII")
        fbar2.addWidget(self._edt_rx_search, stretch=1)

        btn_rx_clr_flt = QPushButton("✕ Reset")
        btn_rx_clr_flt.setToolTip("Clear all filters")
        btn_rx_clr_flt.clicked.connect(self._on_rx_clear_filters)
        fbar2.addWidget(btn_rx_clr_flt)

        lay.addLayout(fbar2)

        self._rx_model = RxTableModel(max_rows=self._spn_rx_buf.value())
        self._rx_proxy = RxFilterProxy()
        self._rx_proxy.setSourceModel(self._rx_model)

        self._cmb_rx_type.currentTextChanged.connect(
            lambda t: self._rx_proxy.set_ptype("" if t == "All" else t))
        self._edt_rx_src_ep.textChanged.connect(self._rx_proxy.set_src_ep)
        self._edt_rx_dst_ep.textChanged.connect(self._rx_proxy.set_dst_ep)
        self._edt_rx_src.textChanged.connect(self._rx_proxy.set_src_node)
        self._edt_rx_dst.textChanged.connect(self._rx_proxy.set_dst_node)
        self._spn_rx_hops.valueChanged.connect(self._rx_proxy.set_max_hops)
        self._cmb_rx_qos.currentTextChanged.connect(self._rx_proxy.set_qos)
        self._spn_rx_delay_min.valueChanged.connect(self._rx_proxy.set_min_delay)
        self._spn_rx_delay_max.valueChanged.connect(self._rx_proxy.set_max_delay)
        self._spn_rx_bytes_min.valueChanged.connect(self._rx_proxy.set_min_bytes)
        self._spn_rx_bytes_max.valueChanged.connect(self._rx_proxy.set_max_bytes)
        self._edt_rx_search.textChanged.connect(self._rx_proxy.set_search)

        self._rx_table = QTableView()
        self._rx_table.setModel(self._rx_proxy)
        self._rx_table.setAlternatingRowColors(True)
        self._rx_table.setSelectionBehavior(QTableView.SelectionBehavior.SelectRows)
        self._rx_table.horizontalHeader().setStretchLastSection(False)
        self._rx_table.verticalHeader().setDefaultSectionSize(22)
        self._rx_table.verticalHeader().hide()
        self._rx_table.setShowGrid(True)

        hdr = self._rx_table.horizontalHeader()
        hdr.setSectionResizeMode(QHeaderView.ResizeMode.Interactive)
        # CBOR column (11) fills remaining space
        hdr.setSectionResizeMode(11, QHeaderView.ResizeMode.Stretch)
        col_widths = [70, 55, 95, 95, 55, 55, 40, 40, 70, 45, 220]
        for i, w_px in enumerate(col_widths):
            self._rx_table.setColumnWidth(i, w_px)

        self._rx_table.clicked.connect(self._on_rx_row_clicked)
        lay.addWidget(self._rx_table, stretch=1)

        # Payload detail panel
        detail_grp = QGroupBox("Payload detail")
        detail_lay = QHBoxLayout(detail_grp)

        self._rx_detail_hex = QTextEdit()
        self._rx_detail_hex.setReadOnly(True)
        self._rx_detail_hex.setFont(QFont("Courier New", 11))
        self._rx_detail_hex.setMaximumHeight(90)
        self._rx_detail_hex.setPlaceholderText("Hex dump…")
        detail_lay.addWidget(self._rx_detail_hex, stretch=2)

        self._rx_detail_cbor = QTextEdit()
        self._rx_detail_cbor.setReadOnly(True)
        self._rx_detail_cbor.setFont(QFont("Courier New", 11))
        self._rx_detail_cbor.setMaximumHeight(90)
        self._rx_detail_cbor.setPlaceholderText("CBOR decode…")
        detail_lay.addWidget(self._rx_detail_cbor, stretch=2)

        lay.addWidget(detail_grp)
        return w

    def _build_remote_tab(self) -> QWidget:
        """Remote API — over-the-air reconfiguration of a remote node."""
        w = QWidget()
        lay = QVBoxLayout(w)
        lay.setSpacing(10)

        warn = QLabel(
            "⚠  Changes are applied over the air via the Wirepas Remote API and "
            "trigger a reboot of the target node. Changing its network address or "
            "channel will move it to another network — you may lose contact with it.")
        warn.setWordWrap(True)
        warn.setStyleSheet("color:#d4a017;")
        lay.addWidget(warn)

        target_grp = QGroupBox("Target node")
        target_lay = QFormLayout(target_grp)
        self._txt_rmt_dst = QLineEdit()
        self._txt_rmt_dst.setPlaceholderText("current node address  e.g. 0x0000002A")
        self._txt_rmt_dst.setFont(QFont("Courier New", 12))
        target_lay.addRow("Destination addr:", self._txt_rmt_dst)
        lay.addWidget(target_grp)

        new_grp = QGroupBox("New settings (leave a field blank to keep unchanged)")
        new_lay = QFormLayout(new_grp)

        self._chk_rmt_node = QCheckBox("Set node address")
        self._txt_rmt_node = QLineEdit()
        self._txt_rmt_node.setPlaceholderText("0x00000001")
        self._txt_rmt_node.setFont(QFont("Courier New", 12))
        new_lay.addRow(self._chk_rmt_node, self._txt_rmt_node)

        self._chk_rmt_net = QCheckBox("Set network address")
        self._txt_rmt_net = QLineEdit()
        self._txt_rmt_net.setPlaceholderText("0xABCDEF  (24-bit)")
        self._txt_rmt_net.setFont(QFont("Courier New", 12))
        new_lay.addRow(self._chk_rmt_net, self._txt_rmt_net)

        self._chk_rmt_ch = QCheckBox("Set RF channel")
        self._spn_rmt_ch = QSpinBox()
        self._spn_rmt_ch.setRange(1, 40)
        self._spn_rmt_ch.setValue(7)
        self._spn_rmt_ch.setFixedWidth(70)
        new_lay.addRow(self._chk_rmt_ch, self._spn_rmt_ch)

        delay_row = QWidget()
        delay_lay = QHBoxLayout(delay_row)
        delay_lay.setContentsMargins(0, 0, 0, 0)
        delay_lay.addWidget(QLabel("Reboot delay:"))
        self._spn_rmt_delay = QSpinBox()
        self._spn_rmt_delay.setRange(10, 32767)  # WP-RM-117: min 10 s
        self._spn_rmt_delay.setValue(10)
        self._spn_rmt_delay.setSuffix(" s")
        self._spn_rmt_delay.setFixedWidth(90)
        delay_lay.addWidget(self._spn_rmt_delay)
        delay_lay.addStretch()
        new_lay.addRow("", delay_row)
        lay.addWidget(new_grp)

        send_row = QHBoxLayout()
        self._lbl_rmt_result = QLabel("")
        send_row.addWidget(self._lbl_rmt_result, stretch=1)
        btn_send = QPushButton("  Send remote config ▶")
        btn_send.setObjectName("btn_send")
        btn_send.clicked.connect(self._on_remote_send)
        send_row.addWidget(btn_send)
        lay.addLayout(send_row)

        lay.addStretch()
        return w

    def _build_tx_tab(self) -> QWidget:
        w = QWidget()
        w.setSizePolicy(QSizePolicy.Policy.Preferred, QSizePolicy.Policy.Maximum)
        lay = QVBoxLayout(w)
        lay.setSpacing(5)
        lay.setContentsMargins(4, 4, 4, 4)

        # ── Row 1: Destination | Endpoints & QoS ─────────────────────────────
        top = QHBoxLayout()
        top.setSpacing(8)

        dest_grp = QGroupBox("Destination")
        dest_lay = QFormLayout(dest_grp)
        dest_lay.setSpacing(4)
        dest_lay.setContentsMargins(8, 6, 8, 6)
        self._cbo_dst_type = QComboBox()
        self._cbo_dst_type.addItems(["Unicast", "Multicast", "Broadcast"])
        self._cbo_dst_type.currentTextChanged.connect(self._on_dst_type_changed)
        dest_lay.addRow("Type:", self._cbo_dst_type)
        self._txt_dst_addr = QLineEdit()
        self._txt_dst_addr.setPlaceholderText("0x00000001")
        dest_lay.addRow("Addr:", self._txt_dst_addr)
        top.addWidget(dest_grp)

        ep_grp = QGroupBox("Endpoints && QoS")
        ep_lay = QHBoxLayout(ep_grp)
        ep_lay.setSpacing(6)
        ep_lay.setContentsMargins(8, 6, 8, 6)

        def _ep_spin(label, val, mn, mx):
            ep_lay.addWidget(QLabel(label))
            spn = QSpinBox()
            spn.setRange(mn, mx)
            spn.setValue(val)
            spn.setFixedWidth(60)
            ep_lay.addWidget(spn)
            ep_lay.addSpacing(4)
            return spn

        self._spn_src_ep = _ep_spin("Src EP:", 1, 0, 254)
        self._spn_dst_ep = _ep_spin("Dst EP:", 1, 0, 254)
        self._spn_qos    = _ep_spin("QoS:", 0, 0, 1)
        ep_lay.addStretch()
        top.addWidget(ep_grp, stretch=1)
        lay.addLayout(top)

        # ── Row 2: Payload ────────────────────────────────────────────────────
        payload_grp = QGroupBox("Payload")
        payload_lay = QHBoxLayout(payload_grp)
        payload_lay.setSpacing(6)
        payload_lay.setContentsMargins(8, 6, 8, 6)

        self._chk_hex_payload = QCheckBox("Hex")
        self._chk_hex_payload.setChecked(True)
        payload_lay.addWidget(self._chk_hex_payload)

        self._txt_payload = QLineEdit()
        self._txt_payload.setPlaceholderText("01 02 AB CD …")
        self._txt_payload.setFont(QFont("Courier New", 12))
        payload_lay.addWidget(self._txt_payload, stretch=1)

        self._lbl_send_result = QLabel("")
        self._lbl_send_result.setMinimumWidth(60)
        payload_lay.addWidget(self._lbl_send_result)

        self._btn_send = QPushButton("Send ▶")
        self._btn_send.setObjectName("btn_send")
        self._btn_send.setMinimumWidth(80)
        self._btn_send.clicked.connect(self._on_send)
        payload_lay.addWidget(self._btn_send)
        lay.addWidget(payload_grp)

        return w

    def _build_appconfig_tab(self) -> QWidget:
        w = QWidget()
        lay = QVBoxLayout(w)
        lay.setSpacing(10)

        read_grp = QGroupBox("Current AppConfig (read from node)")
        read_lay = QFormLayout(read_grp)
        self._lbl_ac_seq      = QLabel("—")
        self._lbl_ac_interval = QLabel("—")
        self._txt_ac_data     = QTextEdit()
        self._txt_ac_data.setReadOnly(True)
        self._txt_ac_data.setFont(QFont("Courier New", 11))
        self._txt_ac_data.setMaximumHeight(80)
        read_lay.addRow("Seq:", self._lbl_ac_seq)
        read_lay.addRow("Interval:", self._lbl_ac_interval)
        read_lay.addRow("Data:", self._txt_ac_data)
        btn_read = QPushButton("Read")
        btn_read.clicked.connect(self._on_read_appconfig)
        read_lay.addRow("", btn_read)
        lay.addWidget(read_grp)

        write_grp = QGroupBox("Write AppConfig")
        write_lay = QFormLayout(write_grp)
        self._spn_ac_seq = QSpinBox()
        self._spn_ac_seq.setRange(0, 255)
        self._spn_ac_seq.setFixedWidth(70)
        write_lay.addRow("Seq:", self._spn_ac_seq)

        self._spn_ac_interval = QSpinBox()
        self._spn_ac_interval.setRange(1, 65535)
        self._spn_ac_interval.setValue(60)
        self._spn_ac_interval.setSuffix(" s")
        self._spn_ac_interval.setFixedWidth(100)
        write_lay.addRow("Interval:", self._spn_ac_interval)

        self._txt_ac_write = QLineEdit()
        self._txt_ac_write.setPlaceholderText("hex bytes (max 80 bytes) e.g. 01 02 03")
        self._txt_ac_write.setFont(QFont("Courier New", 11))
        write_lay.addRow("Data (hex):", self._txt_ac_write)

        write_row = QHBoxLayout()
        self._lbl_ac_write_result = QLabel("")
        write_row.addWidget(self._lbl_ac_write_result, stretch=1)
        btn_write = QPushButton("Write")
        btn_write.clicked.connect(self._on_write_appconfig)
        write_row.addWidget(btn_write)
        write_lay.addRow("", write_row)
        lay.addWidget(write_grp)

        lay.addStretch()
        return w


    # ── Port refresh ─────────────────────────────────────────────────────────

    def _refresh_ports(self):
        cur = self._cbo_port.currentText()
        self._cbo_port.clear()
        try:
            import serial.tools.list_ports
            ports = [p.device for p in serial.tools.list_ports.comports()]
        except Exception:
            ports = []
        self._cbo_port.addItems(ports)
        if cur:
            self._cbo_port.setCurrentText(cur)

    # ── Connect / disconnect ─────────────────────────────────────────────────

    def _conn_mode(self) -> str:
        return self._cbo_conn_mode.currentData() or "serial"

    def _on_conn_mode_changed(self, *_):
        mqtt = self._conn_mode() == "mqtt"
        self._grp_serial.setVisible(not mqtt)
        self._grp_mqtt.setVisible(mqtt)

    def _on_connect(self):
        if self._conn_mode() == "mqtt":
            self._connect_mqtt()
        else:
            self._connect_serial()

    def _connect_serial(self):
        port = self._cbo_port.currentText().strip()
        if not port:
            self._log_line("ERROR: no port selected")
            return
        baud    = int(self._cbo_baud.currentText())
        timeout = self._spn_timeout.value()
        try:
            conn = WapsConn(port, baudrate=baud, timeout=float(timeout))
            conn.open()
            conn.add_callback(self._waps_callback)
            self._conn = conn
        except Exception as e:
            self._log_line(f"Connect failed: {e}")
            return
        self._set_connected(True)
        self._log_line(f"Connected  {port}  {baud} baud")
        self._poll_count = 0
        self._poll_last_ok = None
        # Read node info first, then start polling
        QTimer.singleShot(100, self._on_read_node_info)
        if self._chk_poll.isChecked():
            QTimer.singleShot(600, self._poll_timer.start)

    def _connect_mqtt(self):
        ok, hint = mqtt_lib_available()
        if not ok:
            self._log_line(f"MQTT unavailable: {hint}")
            QMessageBox.warning(self, "MQTT backend", hint)
            return
        host = self._txt_mqtt_host.text().strip()
        if not host:
            self._log_line("ERROR: no MQTT host")
            return
        man_gw, man_sink = self._mqtt_gwsink()
        try:
            conn = MqttConn(
                host, port=self._spn_mqtt_port.value(),
                username=self._txt_mqtt_user.text(),
                password=self._txt_mqtt_pass.text(),
                tls=self._chk_mqtt_tls.isChecked(),
                insecure=self._chk_mqtt_insecure.isChecked(),
                gw=man_gw, sink=man_sink,
                timeout=float(self._spn_timeout.value()))
            conn.set_logger(lambda m: self._signals.log_message.emit(m))
            conn.open()
            conn.add_callback(self._waps_callback)
            self._conn = conn
        except Exception as e:
            self._log_line(f"MQTT connect failed: {e}")
            QMessageBox.critical(self, "MQTT backend", f"Connect failed:\n{e}")
            return
        self._set_connected(True)
        self._log_line(f"MQTT connected  {host}:{self._spn_mqtt_port.value()}  "
                       f"— discovering gateways…")
        # Gateway/sink status arrives asynchronously; poll in the background and
        # marshal the result to the GUI thread via a signal (QTimer.singleShot
        # does NOT fire from a plain worker thread — it has no Qt event loop).
        import threading
        def _discover():
            conn.discover(timeout=8.0)
            gws = conn.list_gateways()
            self._signals.mqtt_discovered.emit(conn, gws)
        threading.Thread(target=_discover, daemon=True).start()

    def _on_mqtt_discovered(self, conn, gws):
        if self._conn is not conn:
            return   # disconnected / switched meanwhile
        self._populate_mqtt_sinks(conn, gateways=gws)
        n = len(conn.list_sinks())
        self._log_line(f"MQTT discovery: {len(gws)} gateway(s), {n} sink(s)")
        # Push the current gateway/sink into the connection so downlink (scan,
        # remote API, TX) works even when no sink config was discovered.
        gw, sink = self._mqtt_gwsink()
        if gw:
            conn.select(gw, sink)
            self._log_line(f"MQTT active sink: {gw} / {sink}")
            QTimer.singleShot(100, self._on_read_node_info)
        elif n == 0:
            self._log_line(
                "No gateway selected — pick a gateway above, or type "
                "gateway/sink manually.")

    def _mqtt_gwsink(self):
        """Current (gateway, sink) from the two fields. sink defaults to sink0.
        Resolves the sink display label ("sink0  (0x…)") back to its clean id
        (stored as item data) so downlink topic + config lookup use the real id."""
        gw = self._cbo_mqtt_gw.currentText().strip() or None
        sc = self._cbo_mqtt_sink
        txt = sc.currentText().strip()
        idx = sc.findText(txt)
        if idx >= 0 and sc.itemData(idx):
            sink = str(sc.itemData(idx))
        else:
            sink = txt or "sink0"
        return gw, sink

    def _populate_mqtt_sinks(self, conn, gateways=None):
        """Fill the gateway list from discovery, then the sink list for it.
        `gateways` may be a list captured during discovery to avoid re-querying
        (get_gateways() can transiently return empty right after)."""
        sinks = conn.list_sinks()
        # Gateways come from get_gateways() (independent of sink config), plus any
        # gateway that already exposes a sink — union, preserving order.
        raw = list(gateways) if gateways is not None else list(conn.list_gateways())
        gws = [str(g) for g in raw]                    # coerce (objects → str)
        for gw, sink, cfg in sinks:
            s = str(gw)
            if s not in gws:
                gws.append(s)
        self._log_line(f"populate gateways: {gws!r}  (sinks={len(sinks)})")
        keep_gw = conn.gw or self._cbo_mqtt_gw.currentText().strip()
        self._cbo_mqtt_gw.blockSignals(True)
        self._cbo_mqtt_gw.clear()
        for g in gws:
            self._cbo_mqtt_gw.addItem(g)
        self._cbo_mqtt_gw.blockSignals(False)
        if keep_gw:
            self._cbo_mqtt_gw.setEditText(keep_gw)
        elif gws:
            self._cbo_mqtt_gw.setCurrentIndex(0)
        self._populate_mqtt_sink_list(conn)

    def _populate_mqtt_sink_list(self, conn):
        """Fill the sink list for the currently selected gateway."""
        gw = self._cbo_mqtt_gw.currentText().strip()
        keep_sink = conn.sink or self._cbo_mqtt_sink.currentText().strip() or "sink0"
        self._cbo_mqtt_sink.blockSignals(True)
        self._cbo_mqtt_sink.clear()
        for g, sink, cfg in conn.list_sinks():
            if g == gw:
                addr = cfg.get("node_address")
                label = sink
                if addr is not None:
                    try:
                        label = f"{sink}   (0x{int(addr):08x})"
                    except Exception:
                        pass
                self._cbo_mqtt_sink.addItem(label, sink)
        self._cbo_mqtt_sink.blockSignals(False)
        # Restore selection by sink id (data), else keep manual text
        idx = self._cbo_mqtt_sink.findData(keep_sink)
        if idx >= 0:
            self._cbo_mqtt_sink.setCurrentIndex(idx)
        else:
            self._cbo_mqtt_sink.setEditText(keep_sink)

    def _on_mqtt_refresh_sinks(self):
        conn = self._conn
        if not isinstance(conn, MqttConn):
            self._log_line("Connect to a broker first")
            return
        import threading
        def _do():
            conn.discover(timeout=8.0)
            self._signals.mqtt_discovered.emit(conn, conn.list_gateways())
        threading.Thread(target=_do, daemon=True).start()

    def _on_mqtt_sniff(self):
        conn = self._conn
        if not isinstance(conn, MqttConn):
            self._log_line("Connect to a broker first")
            return
        import threading
        threading.Thread(target=lambda: conn.sniff_topics(6.0), daemon=True).start()

    def _on_mqtt_gw_changed(self, _idx: int):
        conn = self._conn
        if isinstance(conn, MqttConn):
            self._populate_mqtt_sink_list(conn)
        self._apply_mqtt_selection()

    def _on_mqtt_sink_changed(self, _idx: int):
        self._apply_mqtt_selection()

    def _apply_mqtt_selection(self):
        conn = self._conn
        if not isinstance(conn, MqttConn):
            return
        gw, sink = self._mqtt_gwsink()
        if gw:
            conn.select(gw, sink)
            QTimer.singleShot(100, self._on_read_node_info)

    def _on_disconnect(self):
        self._poll_timer.stop()
        if self._conn:
            try:
                self._conn.close()
            except Exception:
                pass
            self._conn = None
        self._set_connected(False)
        self._log_line("Disconnected")

    def _set_connected(self, on: bool):
        self._btn_connect.setEnabled(not on)
        self._btn_disconnect.setEnabled(on)
        self._btn_send.setEnabled(on)
        self._btn_stack_start.setEnabled(on)
        self._btn_stack_stop.setEnabled(on)
        self._cbo_port.setEnabled(not on)
        self._cbo_baud.setEnabled(not on)
        self._spn_timeout.setEnabled(not on)
        self._cbo_conn_mode.setEnabled(not on)
        for wdg in (self._txt_mqtt_host, self._spn_mqtt_port, self._chk_mqtt_tls,
                    self._chk_mqtt_insecure, self._txt_mqtt_user, self._txt_mqtt_pass):
            wdg.setEnabled(not on)
        # gw/sink selectors are always editable (manual entry before connect,
        # dropdown selection after discovery). Refresh needs a live connection.
        self._cbo_mqtt_gw.setEnabled(True)
        self._cbo_mqtt_sink.setEnabled(True)
        self._btn_mqtt_refresh.setEnabled(on)
        self._btn_mqtt_sniff.setEnabled(on)
        if on:
            if self._conn_mode() == "mqtt":
                host = self._txt_mqtt_host.text()
                self._lbl_status.setText(f"Connected  MQTT {host}")
            else:
                port = self._cbo_port.currentText()
                baud = self._cbo_baud.currentText()
                self._lbl_status.setText(f"Connected  {port}  {baud} baud")
            self._lbl_status.setObjectName("lbl_status_ok")
        else:
            self._lbl_status.setText("Disconnected")
            self._lbl_status.setObjectName("lbl_status_err")
            self._lbl_status_node.setText("")
        self._lbl_status.style().unpolish(self._lbl_status)
        self._lbl_status.style().polish(self._lbl_status)

    # ── WAPS callback (RX thread) ─────────────────────────────────────────────

    def _waps_callback(self, frame: dict):
        func = frame["func"]
        fid  = frame["fid"]
        p    = frame["payload"]

        if func == FC.RX_IND:
            d = parse_rx_ind(p)
            if d is None:
                return
            self._emit_rx_row(d, d["apdu"], fid)
            self._ack(FC.RX_RSP, fid)

        elif func == FC.RX_FRAG_IND:
            d = parse_rx_frag_ind(p)
            if d is None:
                return
            full = self._reasm.add(d["src_addr"], d["packet_id"], d["offset"],
                                   d["last"], d["apdu"])
            if full is not None:
                self._emit_rx_row(d, full, fid)
            self._ack(FC.RX_FRAG_RSP, fid)

        elif func == FC.TX_IND:
            if len(p) < 14:
                return
            (queued, apdu_id, src_ep, dst_addr, dst_ep,
             queue_delay, result) = struct.unpack_from('<BHBIBIB', p)
            self._signals.tx_ind.emit(dict(
                apdu_id   = apdu_id,
                dst_addr  = f"0x{dst_addr:08x}",
                result    = result,
            ))

        elif func == FC.APP_CONFIG_RX_IND:
            self._ack(FC.APP_CONFIG_RX_RSP, fid)

        elif func == FC.CDD_IND:
            self._ack(FC.CDD_RSP, fid)

        elif func == FC.STACK_STATE_IND:
            # msap_state_ind_t: queued(B) result(B)  — result is stack state flags
            result = p[1] if len(p) >= 2 else 0xFF
            state = "STARTED" if result == 0 else f"flags=0x{result:02x}"
            self._signals.log_message.emit(f"Stack state changed: {state}")
            # MUST ack, otherwise it stays at head of queue and blocks all RX
            self._ack(FC.STACK_STATE_RSP, fid)

        elif self._chk_debug.isChecked() and func not in (
                FC.INDICATION_POLL_CNF,):
            # Surface any frame we don't explicitly handle, to aid debugging
            self._signals.log_message.emit(
                f"RX frame func=0x{func:02x} fid={fid} len={len(p)} "
                f"pld={' '.join(f'{b:02x}' for b in p[:16])}"
            )

    def _ack(self, func_rsp: int, fid: int):
        """Acknowledge an indication (pld[0]=1 to chain the next one)."""
        conn = self._conn
        if conn is not None:
            try:
                conn.respond(func_rsp, fid, struct.pack('<B', 1))
            except Exception:
                pass

    def _emit_rx_row(self, d: dict, apdu: bytes, fid: int):
        """Build a downlink table row from parsed RX data and emit it."""
        info     = d["info"]
        qos      = info & 0x03
        hops     = (info >> 2) & 0x3F
        delay_ms = d["delay"] * 1000 // 128
        dst_addr = d["dst_addr"]
        # Remote API responses (EP 240→255) are handled in _on_rx_packet
        if dst_addr == ADDR_BROADCAST:
            ptype = "BCAST"
        elif dst_addr & ADDR_MCAST_BIT:
            ptype = "MCAST"
        else:
            ptype = "UCAST"
        # EP 247 → labelled Wirepas diagnostics; otherwise plain CBOR
        if d["src_ep"] == DIAG_SRC_EP and d["dst_ep"] == DIAG_DST_EP:
            cbor = cbor_diag_to_str(bytes(apdu)) or cbor_to_str(bytes(apdu)) or "—"
        else:
            cbor = cbor_to_str(bytes(apdu)) or "—"
        self._signals.rx_packet.emit(dict(
            ts        = time.strftime("%H:%M:%S"),
            ptype     = ptype,
            src_addr  = str(d['src_addr']),
            dst_addr  = str(dst_addr),
            src_ep    = d["src_ep"],
            dst_ep    = d["dst_ep"],
            qos       = qos,
            hops      = hops,
            delay_ms  = delay_ms,
            apdu_len  = len(apdu),
            hex       = " ".join(f"{b:02x}" for b in apdu),
            cbor      = cbor,
            ascii     = "".join(chr(b) if 0x20 <= b < 0x7F else "." for b in apdu),
            raw       = bytes(apdu),
            fid       = fid,
        ))

    # ── GUI slots (main thread) ───────────────────────────────────────────────

    def _on_rx_packet(self, row: dict):
        self._rx_model.add_row(row)
        if self._chk_autoscroll.isChecked():
            last = self._rx_proxy.rowCount() - 1
            if last >= 0:
                self._rx_table.scrollTo(self._rx_proxy.index(last, 0))
        self._log_line(
            f"RX {row['ptype']}  {row['src_addr']} → {row['dst_addr']}"
            f"  ep={row['src_ep']}→{row['dst_ep']}"
            f"  hops={row['hops']}  {row['delay_ms']}ms  qos={row['qos']}"
            f"  {row['apdu_len']}B"
        )
        # Update network node registry (passive discovery)
        try:
            src = int(row['src_addr'])
            self._node_model.update_node(
                src,
                hops=row['hops'],
                delay_ms=row['delay_ms'],
                src_ep=row['src_ep'],
                incr_pkt=True,
            )
        except Exception as e:
            self._log_line(f"node registry update failed for "
                           f"{row.get('src_addr')!r}: {e}")
            return
        # Enrich with role/mode from Wirepas diagnostic packets (EP=247→255).
        # Skip if we already have the authoritative CSAP role for this node.
        if row['src_ep'] == DIAG_SRC_EP and row['dst_ep'] == DIAG_DST_EP:
            diag = parse_diag_packet(row['raw'])
            if diag:
                if src not in self._csap_role_nodes:
                    self._node_model.update_node(src, **{
                        k: diag[k] for k in ("role", "mode") if k in diag
                    })
                # Log the raw role byte once per (node, value) to help calibrate
                # the _DIAG_ROLE_MAP if a role/mode is mislabelled.
                if "role_raw" in diag:
                    seen = self._diag_role_seen.get(src)
                    if seen != diag["role_raw"]:
                        self._diag_role_seen[src] = diag["role_raw"]
                        rv = diag["role_raw"]
                        msg = (f"diag 0x{src:08x}  role_raw={rv} (0x{rv:02x} "
                               f"0b{rv:08b})  → {diag.get('role','?')}/{diag.get('mode','?')}")
                        if self._chk_debug.isChecked() and diag.get("raw_map"):
                            msg += "  fields=" + str(diag["raw_map"])
                        self._log_line(msg)
        # Remote API responses (src_ep=240 → dst_ep=255)
        if row['src_ep'] == REMOTE_API_RSP_SRC_EP and row['dst_ep'] == REMOTE_API_RSP_DST_EP:
            # Scratchpad status (0x99) → FW / area ID / OTAP info
            st = parse_remote_scratchpad_status(row['raw'])
            if st is not None:
                self._signals.remote_scratch.emit(src, st)
            else:
                # ReadCSAP response (0x8E) → authoritative configured role/mode
                csap = parse_remote_csap_read(row['raw'])
                if csap and csap["attr_id"] == CSAP.NODE_ROLE and csap["value"]:
                    role_byte = csap["value"][0]
                    role, mode = decode_csap_role(role_byte)
                    self._csap_role_nodes.add(src)
                    self._node_model.update_node(src, role=role, mode=mode)
                    self._log_line(
                        f"csap 0x{src:08x}  role=0x{role_byte:02x}  → {role}/{mode}")

        # Route EP 2 (RS485_UP) replies to motor window if open
        if row['src_ep'] == MotorWindow.EP_UP:
            if self._motor_window.isVisible():
                self._motor_window.feed_reply(row['raw'])

        # Route CTN (EP 11), SP-110 (EP 12) and AEM10900 (EP 9) to the sensor window
        if self._sensor_window.isVisible():
            if row['src_ep'] == SensorWindow.EP_PMIC:
                vals = parse_adc_cbor(row['raw'])
                if vals and len(vals) >= 4:
                    self._sensor_window.feed_aem(
                        src,
                        float(vals[0]), float(vals[1]), float(vals[2]), float(vals[3]),
                        int(vals[4]) if len(vals) > 4 else 0,
                        bool(vals[5]) if len(vals) > 5 else False)
            elif row['src_ep'] == SensorWindow.EP_SENSOR:
                vals = parse_adc_cbor(row['raw'])
                if vals:
                    t_c  = vals[0]
                    r_t  = vals[1] if len(vals) > 1 else float("nan")
                    diag = int(vals[2]) if len(vals) > 2 else 0
                    self._sensor_window.feed_ctn(src, t_c, r_t, diag)
            elif row['src_ep'] == SensorWindow.EP_IRRADIANCE:
                vals = parse_adc_cbor(row['raw'])
                if vals:
                    wm2  = vals[0]
                    mv   = vals[1] if len(vals) > 1 else float("nan")
                    diag = int(vals[2]) if len(vals) > 2 else 0
                    self._sensor_window.feed_irr(src, wm2, mv, diag)

        # Route nRF54L15-tag EP 20 uplinks to the tag plot window
        if self._tag_window.isVisible() and row['src_ep'] == TagWindow.EP_TAG:
            vals = parse_adc_cbor(row['raw'])
            if vals and len(vals) >= 10:
                self._tag_window.feed_tag(src, vals)

    def _motor_send(self, dst_addr: int, dst_ep: int, payload: bytes, src_ep: int):
        conn = self._conn
        if conn is None:
            return
        import threading
        threading.Thread(
            target=lambda: cmd_send(conn, dst_addr, dst_ep, payload, src_ep=src_ep),
            daemon=True,
        ).start()

    def _on_motor_toggle(self, checked: bool):
        if checked:
            self._motor_window.show()
            self._motor_window.raise_()
        else:
            self._motor_window.hide()

    def _on_motor_window_closed(self, visible: bool):
        if not visible:
            self._chk_show_motor.setChecked(False)

    def _on_sensor_toggle(self, checked: bool):
        if checked:
            self._sensor_window.show()
            self._sensor_window.raise_()
        else:
            self._sensor_window.hide()

    def _on_sensor_window_closed(self, visible: bool):
        if not visible:
            self._chk_show_sensor.setChecked(False)

    def _on_tag_toggle(self, checked: bool):
        if checked:
            self._tag_window.show()
            self._tag_window.raise_()
        else:
            self._tag_window.hide()

    def _on_tag_window_closed(self, visible: bool):
        if not visible:
            self._chk_show_tag.setChecked(False)

    def _on_tx_ind(self, info: dict):
        result = info["result"]
        ok = result == 0
        msg = f"TX-IND apdu={info['apdu_id']}  dst={info['dst_addr']}  {'OK' if ok else f'ERR({result})'}"
        self._log_line(msg)
        # Update the Send-tab result label (was stuck on "Sending…").
        self._lbl_send_result.setText("OK" if ok else "FAIL")
        self._lbl_send_result.setStyleSheet(f"color:{COL_TX if ok else COL_ERR};")
        self._btn_send.setEnabled(True)

    def _on_log(self, msg: str):
        self._log_line(msg)

    def _on_rx_clear(self):
        self._rx_model.clear()
        self._rx_detail_hex.clear()
        self._rx_detail_cbor.clear()

    def _on_rx_clear_filters(self):
        for w in (self._edt_rx_src_ep, self._edt_rx_dst_ep,
                  self._edt_rx_src, self._edt_rx_dst, self._edt_rx_search):
            w.blockSignals(True)
            w.clear()
            w.blockSignals(False)
        for spn in (self._spn_rx_hops, self._spn_rx_delay_min, self._spn_rx_delay_max,
                    self._spn_rx_bytes_min, self._spn_rx_bytes_max):
            spn.blockSignals(True)
            spn.setValue(0)
            spn.blockSignals(False)
        self._cmb_rx_type.blockSignals(True)
        self._cmb_rx_type.setCurrentIndex(0)
        self._cmb_rx_type.blockSignals(False)
        self._cmb_rx_qos.blockSignals(True)
        self._cmb_rx_qos.setCurrentIndex(0)
        self._cmb_rx_qos.blockSignals(False)
        self._rx_proxy.clear_filters()

    def _on_rx_copy_hex(self):
        idx = self._rx_table.currentIndex()
        if not idx.isValid():
            return
        src_row = self._rx_proxy.mapToSource(idx).row()
        row = self._rx_model.get_row(src_row)
        if row:
            QApplication.clipboard().setText(row.get("hex", ""))

    def _on_rx_row_clicked(self, idx):
        src_row = self._rx_proxy.mapToSource(idx).row()
        row = self._rx_model.get_row(src_row)
        if not row:
            return
        raw = row.get("raw", b"")
        lines = []
        for off in range(0, max(len(raw), 1), 16):
            chunk = raw[off:off + 16]
            lines.append(
                f"{off:03d}  "
                + " ".join(f"{b:02x}" for b in chunk).ljust(47)
                + "  "
                + "".join(chr(b) if 0x20 <= b < 0x7F else "." for b in chunk)
            )
        self._rx_detail_hex.setPlainText("\n".join(lines))
        if row.get("src_ep") == DIAG_SRC_EP and row.get("dst_ep") == DIAG_DST_EP:
            cbor = cbor_diag_to_str(bytes(raw)) or cbor_to_str(bytes(raw))
        else:
            cbor = cbor_to_str(bytes(raw))
        if cbor:
            self._rx_detail_cbor.setPlainText(cbor)
        else:
            # Not CBOR — fall back to showing the ASCII rendering
            self._rx_detail_cbor.setPlainText(
                "(not CBOR)  ascii: " + row.get("ascii", ""))

    def _on_dst_type_changed(self, text: str):
        self._txt_dst_addr.setEnabled(text != "Broadcast")

    def _on_send(self):
        if not self._conn:
            return
        dst_type = self._cbo_dst_type.currentText()
        if dst_type == "Broadcast":
            dst = ADDR_BROADCAST
        else:
            txt = self._txt_dst_addr.text().strip()
            try:
                dst = int(txt, 0)
            except ValueError:
                self._lbl_send_result.setText("Invalid address")
                self._lbl_send_result.setStyleSheet(f"color:{COL_ERR};")
                return
            if dst_type == "Multicast":
                dst |= ADDR_MCAST_BIT

        if self._chk_hex_payload.isChecked():
            try:
                payload = bytes.fromhex(self._txt_payload.text().replace(" ", ""))
            except ValueError:
                self._lbl_send_result.setText("Invalid hex")
                self._lbl_send_result.setStyleSheet(f"color:{COL_ERR};")
                return
        else:
            payload = self._txt_payload.text().encode("utf-8")

        src_ep = self._spn_src_ep.value()
        dst_ep = self._spn_dst_ep.value()
        qos    = self._spn_qos.value()

        self._btn_send.setEnabled(False)
        self._lbl_send_result.setText("Sending…")
        self._lbl_send_result.setStyleSheet(f"color:{COL_DIM};")

        # Run in thread so GUI doesn't freeze during WAPS round-trip
        import threading
        def _do_send():
            ok = cmd_send(self._conn, dst, dst_ep, payload,
                          src_ep=src_ep, qos=qos)
            self._signals.log_message.emit(
                f"TX {'OK' if ok else 'FAIL'}  dst=0x{dst:08x}"
                f"  ep={src_ep}→{dst_ep}  {len(payload)}B"
            )
            # Update UI from main thread via signal
            self._signals.tx_ind.emit({"apdu_id": 0, "dst_addr": f"0x{dst:08x}",
                                       "result": 0 if ok else 1})

        def _after():
            self._btn_send.setEnabled(True)
            col = COL_TX if self._lbl_send_result.text() != "FAIL" else COL_ERR

        threading.Thread(target=_do_send, daemon=True).start()
        QTimer.singleShot(200, lambda: self._btn_send.setEnabled(True))

    def _on_read_appconfig(self):
        conn = self._conn
        if not conn:
            self._log_line("Not connected")
            return
        import threading
        def _do():
            cnf = conn.request(FC.APP_CONFIG_READ_REQ)
            if cnf is None:
                self._signals.log_message.emit("AppConfig read: timeout")
                self._signals.appconfig.emit({"error": "timeout"})
                return
            p = cnf["payload"]
            if len(p) < 4:
                self._signals.log_message.emit(f"AppConfig read: short frame ({len(p)}B)")
                return
            result, seq, interval = struct.unpack_from('<BBH', p)
            config = p[4:] if len(p) > 4 else b""
            config = config.rstrip(b"\xff")  # strip uninitialised trailing bytes
            hex_str = " ".join(f"{b:02x}" for b in config)

            _RESULT_NAMES = {0: "OK", 1: "not configured", 2: "access denied"}
            result_str = _RESULT_NAMES.get(result, f"ERR({result})")
            self._signals.log_message.emit(
                f"AppConfig: {result_str}  seq={seq}  interval={interval}s  {len(config)}B"
            )
            self._signals.appconfig.emit({
                "result": result, "result_str": result_str,
                "seq": seq, "interval": interval, "hex": hex_str,
            })
        threading.Thread(target=_do, daemon=True).start()

    def _on_appconfig_update(self, data: dict):
        if "error" in data:
            self._lbl_ac_seq.setText(data["error"])
            self._lbl_ac_interval.setText("—")
            self._txt_ac_data.setPlainText("")
            return
        result   = data["result"]
        result_str = data["result_str"]
        seq      = data["seq"]
        interval = data["interval"]
        hex_str  = data["hex"]
        ok = result == 0
        self._lbl_ac_seq.setText(str(seq) if ok else f"— ({result_str})")
        self._lbl_ac_interval.setText(f"{interval} s" if interval else "—")
        self._txt_ac_data.setPlainText(hex_str if hex_str else "(empty)")
        if ok:
            self._spn_ac_seq.setValue(seq)
            if interval:
                self._spn_ac_interval.setValue(interval)
            self._txt_ac_write.setText(hex_str)
        self._lbl_appconf_seq.setText(str(seq))
        self._lbl_appconf_int.setText(f"{interval}s" if interval else "—")

    def _on_write_appconfig(self):
        if not self._conn:
            self._log_line("Not connected")
            return
        try:
            data = bytes.fromhex(self._txt_ac_write.text().replace(" ", ""))
        except ValueError:
            self._lbl_ac_write_result.setText("Invalid hex")
            self._lbl_ac_write_result.setStyleSheet(f"color:{COL_ERR};")
            return
        if len(data) > 80:
            self._lbl_ac_write_result.setText("Max 80 bytes")
            self._lbl_ac_write_result.setStyleSheet(f"color:{COL_ERR};")
            return
        seq      = self._spn_ac_seq.value()
        interval = self._spn_ac_interval.value()
        import threading
        def _do():
            cmd_appconfig_write(self._conn, seq, interval, data)
            self._signals.log_message.emit(
                f"AppConfig written: seq={seq} interval={interval}s {len(data)}B"
            )
        threading.Thread(target=_do, daemon=True).start()

    def _on_read_node_info(self):
        conn = self._conn
        if conn is None:
            return
        import threading
        def _do():
            info = {}
            raw = csap_attr_read(conn, CSAP.NODE_ID)
            if raw and len(raw) >= 4:
                info["node_addr"] = struct.unpack_from('<I', raw)[0]
            raw = csap_attr_read(conn, CSAP.NETWORK_ADDR)
            if raw and len(raw) >= 3:
                info["net_addr"] = raw[0] | (raw[1] << 8) | (raw[2] << 16)
            raw = csap_attr_read(conn, CSAP.NETWORK_CHANNEL)
            if raw:
                info["channel"] = raw[0]
            raw = csap_attr_read(conn, CSAP.NODE_ROLE)
            if raw:
                info["role"] = raw[0]
            fw_parts = []
            for attr in (CSAP.FIRMWARE_MAJOR, CSAP.FIRMWARE_MINOR,
                         CSAP.FIRMWARE_MAINT, CSAP.FIRMWARE_DEV):
                raw = csap_attr_read(conn, attr)
                fw_parts.append(struct.unpack_from('<H', raw)[0] if raw and len(raw) >= 2 else 0)
            info["fw"] = ".".join(str(x) for x in fw_parts)
            self._signals.node_info.emit(info)
            self._signals.log_message.emit(
                f"Node: addr=0x{info.get('node_addr',0):08x}"
                f"  net=0x{info.get('net_addr',0):06x}"
                f"  ch={info.get('channel','?')}"
                f"  role={_ROLE_NAMES.get(info.get('role',0),'?')}"
                f"  fw={info.get('fw','?')}"
            )
        threading.Thread(target=_do, daemon=True).start()

    def _on_node_info(self, info: dict):
        if "node_addr" in info:
            self._lbl_node_addr.setText(f"0x{info['node_addr']:08x}")
            self._txt_cfg_node_addr.setText(f"0x{info['node_addr']:08x}")
        if "net_addr" in info:
            self._lbl_net_addr.setText(f"0x{info['net_addr']:06x}")
            self._txt_cfg_net_addr.setText(f"0x{info['net_addr']:06x}")
        if "channel" in info:
            self._lbl_channel.setText(str(info["channel"]))
            self._spn_cfg_channel.setValue(info["channel"])
        if "role" in info:
            role_name = _ROLE_NAMES.get(info["role"], f"0x{info['role']:02x}")
            self._lbl_role.setText(role_name)
            for i in range(self._cbo_role.count()):
                if self._cbo_role.itemData(i) == info["role"]:
                    self._cbo_role.setCurrentIndex(i)
                    break
        if "fw" in info:
            self._lbl_fw.setText(info["fw"])
        # Status bar compact summary
        addr = info.get("node_addr", None)
        ch   = info.get("channel", None)
        role = _ROLE_NAMES.get(info.get("role"), "") if "role" in info else ""
        parts = []
        if addr is not None: parts.append(f"0x{addr:08x}")
        if ch   is not None: parts.append(f"ch={ch}")
        if role:             parts.append(role)
        if parts:
            self._lbl_status_node.setText("  |  " + "  ".join(parts))

    def _on_apply_node_config(self):
        conn = self._conn
        if conn is None:
            return
        try:
            node_addr = int(self._txt_cfg_node_addr.text(), 0)
        except ValueError:
            self._lbl_cfg_result.setText("Invalid node address")
            self._lbl_cfg_result.setStyleSheet(f"color:{COL_ERR};")
            return
        try:
            net_addr = int(self._txt_cfg_net_addr.text(), 0)
        except ValueError:
            self._lbl_cfg_result.setText("Invalid network address")
            self._lbl_cfg_result.setStyleSheet(f"color:{COL_ERR};")
            return
        role    = self._cbo_role.currentData()
        channel = self._spn_cfg_channel.value()
        # cmd_node_configure closes/reopens the serial port (UART reset on this
        # node); pause auto-poll so it doesn't hit the port mid-reopen.
        self._cfg_was_polling = self._poll_timer.isActive()
        self._poll_timer.stop()
        self._lbl_cfg_result.setText("Configuring… (node UART resets, ~10 s)")
        self._lbl_cfg_result.setStyleSheet(f"color:{COL_DIM};")
        import threading
        def _do():
            ok = cmd_node_configure(
                conn, role, node_addr, net_addr, channel,
                log_cb=lambda m: self._signals.log_message.emit(m))
            msg = "Config OK — stack restarted" if ok else "Config FAILED"
            col = COL_TX if ok else COL_ERR
            self._signals.node_cfg_result.emit(msg, col)
            self._signals.log_message.emit(
                f"Node config {'OK' if ok else 'FAIL'}:"
                f"  role={_ROLE_NAMES.get(role,'?')}"
                f"  addr=0x{node_addr:08x}"
                f"  net=0x{net_addr:06x}"
                f"  ch={channel}"
            )
        threading.Thread(target=_do, daemon=True).start()

    def _on_node_cfg_result(self, msg: str, col: str):
        self._lbl_cfg_result.setText(msg)
        self._lbl_cfg_result.setStyleSheet(f"color:{col};")
        # Resume auto-poll (was paused during the config's port reopen cycles).
        if getattr(self, "_cfg_was_polling", False):
            self._poll_timer.start()
        if "OK" in msg:
            QTimer.singleShot(500, self._on_read_node_info)

    def _on_stack_start(self):
        conn = self._conn
        if conn is None:
            return
        import threading
        def _do():
            ok = cmd_stack_start(conn)
            self._signals.log_message.emit(f"Stack start: {'OK' if ok else 'FAIL'}")
            def _upd():
                self._lbl_stack_state.setText("STARTED" if ok else "ERR")
                self._lbl_stack_state.setStyleSheet(
                    f"color:{'#27ae60' if ok else COL_ERR}; font-weight:bold; font-family:'Courier New';")
            QTimer.singleShot(0, _upd)
        threading.Thread(target=_do, daemon=True).start()

    def _on_stack_stop(self):
        conn = self._conn
        if conn is None:
            return
        import threading
        def _do():
            ok = cmd_stack_stop(conn)
            self._signals.log_message.emit(f"Stack stop: {'OK' if ok else 'FAIL'}")
            def _upd():
                self._lbl_stack_state.setText("STOPPED" if ok else "ERR")
                self._lbl_stack_state.setStyleSheet(
                    f"color:{COL_DIM}; font-weight:bold; font-family:'Courier New';")
            QTimer.singleShot(0, _upd)
        threading.Thread(target=_do, daemon=True).start()

    # ── Remote API ────────────────────────────────────────────────────────────

    def _on_remote_send(self):
        conn = self._conn
        if conn is None:
            self._log_line("Not connected")
            return
        try:
            dst = int(self._txt_rmt_dst.text(), 0)
        except ValueError:
            self._lbl_rmt_result.setText("Invalid destination address")
            self._lbl_rmt_result.setStyleSheet(f"color:{COL_ERR};")
            return

        node_addr = net_addr = channel = None
        try:
            if self._chk_rmt_node.isChecked():
                node_addr = int(self._txt_rmt_node.text(), 0)
            if self._chk_rmt_net.isChecked():
                net_addr = int(self._txt_rmt_net.text(), 0)
            if self._chk_rmt_ch.isChecked():
                channel = self._spn_rmt_ch.value()
        except ValueError:
            self._lbl_rmt_result.setText("Invalid value")
            self._lbl_rmt_result.setStyleSheet(f"color:{COL_ERR};")
            return

        if node_addr is None and net_addr is None and channel is None:
            self._lbl_rmt_result.setText("Nothing selected to change")
            self._lbl_rmt_result.setStyleSheet(f"color:{COL_ERR};")
            return

        delay = self._spn_rmt_delay.value()
        import threading
        def _do():
            ok = cmd_remote_configure(conn, dst, node_addr=node_addr,
                                      net_addr=net_addr, channel=channel,
                                      reboot_delay_s=delay)
            msg = "Remote config sent" if ok else "Remote config TX failed"
            col = COL_TX if ok else COL_ERR
            def _upd():
                self._lbl_rmt_result.setText(msg)
                self._lbl_rmt_result.setStyleSheet(f"color:{col};")
            QTimer.singleShot(0, _upd)
            parts = []
            if node_addr is not None: parts.append(f"addr=0x{node_addr:08x}")
            if net_addr is not None:  parts.append(f"net=0x{net_addr:06x}")
            if channel is not None:   parts.append(f"ch={channel}")
            self._signals.log_message.emit(
                f"Remote API → 0x{dst:08x}: {' '.join(parts)}  "
                f"reboot in {delay}s  [{'OK' if ok else 'FAIL'}]")
        threading.Thread(target=_do, daemon=True).start()

    # ── OTAP ──────────────────────────────────────────────────────────────────

    def _on_otap_browse(self):
        path, _ = QFileDialog.getOpenFileName(
            self, "Select scratchpad file", "",
            "OTAP files (*.otap *.bin);;All files (*)")
        if path:
            self._txt_otap_file.setText(path)

    # ── Network tab slots ─────────────────────────────────────────────────────
    def _on_net_scan(self):
        conn = self._conn
        if conn is None:
            self._log_line("Not connected")
            return
        self._log_line("Network scan: Remote API broadcast to 0xffffffff "
                       "(scratchpad status + CSAP role)…")
        import threading

        def _do():
            log = lambda m: self._signals.log_message.emit(m)
            # Scratchpad status → FW / area ID / OTAP info
            cmd_remote_scratchpad_status_req(conn, 0xFFFFFFFF, log_cb=log)
            # CSAP NODE_ROLE → authoritative configured role/mode (incl. autorole)
            cmd_remote_read_csap(conn, 0xFFFFFFFF, CSAP.NODE_ROLE, log_cb=log)

        threading.Thread(target=_do, daemon=True).start()

    def _on_net_clear(self):
        self._node_model.clear()
        self._csap_role_nodes.clear()
        self._diag_role_seen.clear()

    def _on_net_filter(self, text: str):
        self._net_proxy.setFilterFixedString(text)

    def _net_selected_addrs(self) -> list[int]:
        """Return addresses of all currently selected rows, in display order."""
        addrs = []
        for idx in self._net_table.selectionModel().selectedRows():
            addr = self._node_model.addr_at(idx.row(), self._net_proxy)
            if addr is not None:
                addrs.append(addr)
        return addrs

    def _on_net_cell_clicked(self, idx):
        """Clicking the address column copies that node's address to clipboard."""
        # Columns 0 = "Addr (hex)", 1 = "Addr (dec)"
        if idx.column() not in (0, 1):
            return
        text = idx.data()
        if not text:
            return
        QApplication.clipboard().setText(str(text))
        self._log_line(f"Copied to clipboard: {text}")
        self._status_bar.showMessage(f"Copied {text}", 2000)

    def _on_net_selection_changed(self, *_):
        addrs = self._net_selected_addrs()
        if not addrs:
            self._net_uplink_grp.setEnabled(False)
            self._lbl_net_ul_targets.setText("—")
            return
        self._net_uplink_grp.setEnabled(True)
        self._lbl_net_ul_result.setText("")
        n = len(addrs)
        if n == 1:
            label = f"0x{addrs[0]:08x}"
        elif n <= 4:
            label = "  ".join(f"0x{a:08x}" for a in addrs)
        else:
            preview = "  ".join(f"0x{a:08x}" for a in addrs[:3])
            label = f"{preview}  … (+{n - 3} more)   [{n} nodes]"
        self._lbl_net_ul_targets.setText(label)
        title = "Uplink to selected node" if n == 1 else f"Uplink to {n} selected nodes (unicast sequential)"
        self._net_uplink_grp.setTitle(title)

    def _on_net_uplink_send(self):
        conn = self._conn
        if conn is None:
            self._log_line("Not connected")
            return
        addrs = self._net_selected_addrs()
        if not addrs:
            return
        src_ep = self._spn_net_ul_src.value()
        dst_ep = self._spn_net_ul_dst.value()
        hex_str = self._txt_net_ul_payload.text().replace(" ", "")
        try:
            payload = bytes.fromhex(hex_str) if hex_str else b""
        except ValueError:
            self._lbl_net_ul_result.setText("Invalid hex payload")
            return
        n = len(addrs)
        self._lbl_net_ul_result.setText(f"Sending 0/{n}…")
        self._btn_net_ul_send.setEnabled(False)

        import threading
        sig = self._signals

        def _do():
            ok_count = 0
            for i, addr in enumerate(addrs):
                sig.log_message.emit(
                    f"Uplink [{i+1}/{n}] → 0x{addr:08x}"
                    f"  ep={src_ep}→{dst_ep}  {len(payload)}B")
                ok = cmd_send(conn, dst=addr, dst_ep=dst_ep,
                              src_ep=src_ep, payload=payload)
                ok_count += ok
                sig.log_message.emit(
                    f"  └─ {'OK' if ok else 'FAILED'}")
                if i < n - 1:
                    time.sleep(0.15)  # brief gap between unicasts
            summary = f"Done: {ok_count}/{n} OK"
            sig.log_message.emit(f"Uplink batch — {summary}")
            sig.net_uplink_done.emit(summary, COL_TX if ok_count == n else COL_ERR)

        threading.Thread(target=_do, daemon=True).start()

    def _on_net_uplink_done(self, summary: str, color: str):
        self._lbl_net_ul_result.setText(summary)
        self._lbl_net_ul_result.setStyleSheet(f"color:{color};")
        self._btn_net_ul_send.setEnabled(True)

    def _on_net_export_csv(self):
        path, _ = QFileDialog.getSaveFileName(
            self, "Export CSV", "wirepas_nodes.csv", "CSV (*.csv)")
        if not path:
            return
        nodes = self._node_model.get_all()
        with open(path, "w", newline="", encoding="utf-8") as f:
            writer = csv.DictWriter(f, fieldnames=[
                "addr_hex", "addr_dec", "role", "mode", "hops", "delay_ms", "pkt_count",
                "first_seen", "last_seen", "src_eps",
                "stack_fw", "app_fw", "app_area_id", "proc_seq", "action_name"])
            writer.writeheader()
            for n in nodes:
                writer.writerow({
                    "addr_hex":    f"0x{n['addr']:08x}",
                    "addr_dec":    str(n["addr"]),
                    "role":        n.get("role", ""),
                    "mode":        n.get("mode", ""),
                    "hops":        n["hops"],
                    "delay_ms":    n["delay_ms"],
                    "pkt_count":   n["pkt_count"],
                    "first_seen":  n["first_seen"],
                    "last_seen":   n["last_seen"],
                    "src_eps":     ";".join(str(e) for e in sorted(n["src_eps"])),
                    "stack_fw":    "v" + ".".join(str(x) for x in n["fw"])     if n.get("fw")     else "",
                    "app_fw":      "v" + ".".join(str(x) for x in n["app_fw"]) if n.get("app_fw") else "",
                    "app_area_id": f"0x{n['app_area_id']:08x}" if n.get("app_area_id") else "",
                    "proc_seq":    n["proc_seq"],
                    "action_name": n.get("action_name", ""),
                })
        self._log_line(f"Network export → {path}  ({len(nodes)} nodes)")

    def _on_net_export_json(self):
        path, _ = QFileDialog.getSaveFileName(
            self, "Export JSON", "wirepas_nodes.json", "JSON (*.json)")
        if not path:
            return
        nodes = self._node_model.get_all()
        out = []
        for n in nodes:
            out.append({
                "addr_hex":    f"0x{n['addr']:08x}",
                "addr_dec":    n["addr"],
                "hops":        n["hops"],
                "delay_ms":    n["delay_ms"],
                "pkt_count":   n["pkt_count"],
                "first_seen":  n["first_seen"],
                "last_seen":   n["last_seen"],
                "src_eps":     sorted(n["src_eps"]),
                "role":        n.get("role", ""),
                "mode":        n.get("mode", ""),
                "stack_fw":    list(n["fw"])     if n.get("fw")     else None,
                "app_fw":      list(n["app_fw"]) if n.get("app_fw") else None,
                "app_area_id": f"0x{n['app_area_id']:08x}" if n.get("app_area_id") else None,
                "proc_seq":    n["proc_seq"],
                "action_name": n.get("action_name", ""),
            })
        with open(path, "w", encoding="utf-8") as f:
            json.dump(out, f, indent=2)
        self._log_line(f"Network export → {path}  ({len(nodes)} nodes)")

    def _on_remote_poll(self):
        conn = self._conn
        if conn is None:
            self._log_line("Not connected")
            return
        try:
            addr = int(self._txt_remote_addr.text().strip(), 0)
        except ValueError:
            self._lbl_remote_result.setText("Invalid address")
            self._lbl_remote_result.setStyleSheet(f"color:{COL_ERR};")
            return
        import threading
        threading.Thread(
            target=lambda: cmd_remote_scratchpad_status_req(
                conn, addr, log_cb=lambda m: self._signals.log_message.emit(m)),
            daemon=True).start()

    def _on_remote_scratch_status(self, src_addr: int, st: dict):
        # Enrich network registry with OTAP / firmware data
        self._node_model.update_node(
            src_addr,
            fw=st.get("fw", ()),
            app_fw=st.get("app_fw", ()),
            app_area_id=st.get("app_area_id", 0),
            proc_seq=st.get("proc_seq", 255),
            stored_seq=st.get("stored_seq", 0),
            action_name=st.get("action_name", ""),
        )

        # Auto-fill address field from the sender if empty
        if not self._txt_remote_addr.text().strip():
            self._txt_remote_addr.setText(str(src_addr))

        stored_type_names = {0: "blank", 1: "present", 2: "to-be-processed"}
        stored_status_names = {0: "OK", 255: "new (not validated)"}

        self._lbl_remote_stored.setText(
            f"{st['stored_bytes']}B  crc=0x{st['stored_crc']:04x}"
            f"  seq={st['stored_seq']}"
            f"  {stored_type_names.get(st['stored_type'], str(st['stored_type']))}"
            f"  status={stored_status_names.get(st['stored_status'], str(st['stored_status']))}")
        stack_fw = "v" + ".".join(str(x) for x in st["fw"]) if st.get("fw") else "—"
        app_fw   = "v" + ".".join(str(x) for x in st["app_fw"]) if st.get("app_fw") else "—"
        self._lbl_remote_proc.setText(
            f"{st['proc_bytes']}B  crc=0x{st['proc_crc']:04x}"
            f"  seq={st['proc_seq']}"
            f"  stack={stack_fw}  app={app_fw}")

        action_str = st.get('action_name', '—')
        target_seq = st.get('target_seq', '—')
        target_crc = st.get('target_crc', None)
        remaining  = st.get('remaining_min', 0)
        crc_str = f"crc=0x{target_crc:04x}" if target_crc is not None else ""
        self._lbl_remote_action.setText(
            f"{action_str}  target_seq={target_seq} {crc_str}"
            + (f"  remaining={remaining}min" if remaining else ""))

        # Deduplicate: only log and trigger detection when something changed
        status_key = (src_addr, st['stored_seq'], st['proc_seq'], st.get('target_seq'))
        prev_key = getattr(self, '_remote_status_key', None)
        changed = (status_key != prev_key)
        self._remote_status_key = status_key

        # Detect OTAP completion:
        #   1. proc_seq == watch_seq (exact match with uploaded seq)
        #   2. OR proc_seq changed from its previous value (any OTAP processed)
        watch_seq   = getattr(self, "_otap_watch_seq", None)
        prev_proc   = getattr(self, '_remote_proc_seqs', {}).get(src_addr)
        proc_changed = (prev_proc is not None and prev_proc != st['proc_seq'])
        if not hasattr(self, '_remote_proc_seqs'):
            self._remote_proc_seqs = {}
        self._remote_proc_seqs[src_addr] = st['proc_seq']

        otap_done = (watch_seq is not None and st['proc_seq'] == watch_seq) or proc_changed
        if otap_done:
            detail = f"seq={st['proc_seq']}"
            if proc_changed:
                detail = f"proc_seq {prev_proc} → {st['proc_seq']}"
            self._lbl_remote_result.setText(
                f"✓ OTAP OK — node 0x{src_addr:08x} processed  {detail}")
            self._lbl_remote_result.setStyleSheet(f"color:{COL_TX}; font-weight:bold;")
            timer = getattr(self, "_otap_watch_timer", None)
            if timer:
                timer.stop()
            self._otap_watch_seq = None
        else:
            self._lbl_remote_result.setText(
                f"Last update from 0x{src_addr:08x}  proc_seq={st['proc_seq']}")
            self._lbl_remote_result.setStyleSheet(f"color:{COL_DIM};")

        if changed:
            self._log_line(
                f"Remote API ← 0x{src_addr:08x}  stored_seq={st['stored_seq']}"
                f"  proc_seq={st['proc_seq']}  action={action_str}"
                + (f"  ✓ OTAP OK" if otap_done else ""))

    def _on_otap_watch_tick(self):
        """Called every 5s after upload: polls remote node via Remote API.
        Every 6th tick (30s) also polls SINK status."""
        conn = self._conn
        if conn is None:
            self._otap_watch_timer.stop()
            return
        self._otap_watch_tick_count = getattr(self, "_otap_watch_tick_count", 0) + 1

        # Poll remote node scratchpad status via Remote API
        raw_addr = self._txt_remote_addr.text().strip()
        if raw_addr:
            try:
                remote_addr = int(raw_addr, 0)
            except ValueError:
                remote_addr = None
            if remote_addr is not None:
                import threading
                threading.Thread(
                    target=lambda: cmd_remote_scratchpad_status_req(conn, remote_addr),
                    daemon=True).start()

        # Poll SINK status every 30s (every 6th tick)
        if self._otap_watch_tick_count % 6 == 0:
            import threading
            def _do():
                st = cmd_otap_status(conn, log_cb=None)
                if st is not None:
                    self._signals.otap_status.emit(st)
            threading.Thread(target=_do, daemon=True).start()

    def _check_otap_seq_conflict(self, *_):
        seq = self._spn_otap_seq.value()
        if seq == self._otap_processed_seq:
            self._lbl_seq_warn.setText(f"⚠ seq={seq} already processed — node won't reboot!")
        else:
            self._lbl_seq_warn.setText("")

    def _on_otap_status(self):
        conn = self._conn
        if conn is None:
            self._log_line("Not connected")
            return
        import threading
        def _do():
            if isinstance(conn, MqttConn):
                st = conn.otap_status()
            else:
                st = cmd_otap_status(
                    conn, log_cb=lambda m: self._signals.log_message.emit(m))
            if st is not None:
                self._signals.otap_status.emit(st)
            else:
                self._signals.log_message.emit("OTAP status: no response")
        threading.Thread(target=_do, daemon=True).start()

    def _on_otap_status_result(self, st: dict):
        self._lbl_otap_stored.setText(
            f"{st['num_bytes']} B  crc=0x{st['crc']:04x}  seq={st['seq']}  "
            f"type={st['type']}  status={st['status']}")
        self._lbl_otap_processed.setText(
            f"{st['proc_bytes']} B  crc=0x{st['proc_crc']:04x}  "
            f"seq={st['proc_seq']}  area=0x{st['area_id']:08x}")
        self._lbl_otap_fw.setText("v" + ".".join(str(x) for x in st["fw"]))
        new_proc_seq = st.get("proc_seq", 0)
        if new_proc_seq != self._otap_processed_seq:
            watch_seq = getattr(self, "_otap_watch_seq", None)
            if watch_seq is not None and new_proc_seq != watch_seq:
                self._log_line(f"Node rebooted — processed seq changed: {watch_seq} → {new_proc_seq}")
                timer = getattr(self, "_otap_watch_timer", None)
                if timer:
                    timer.stop()
        self._otap_processed_seq = new_proc_seq
        self._check_otap_seq_conflict()

    def _on_otap_clear(self):
        conn = self._conn
        if conn is None:
            self._log_line("Not connected")
            return
        import threading
        def _do():
            ok = conn.otap_clear() if isinstance(conn, MqttConn) else cmd_otap_clear(conn)
            self._signals.log_message.emit(f"OTAP clear: {'OK' if ok else 'FAIL'}")
        threading.Thread(target=_do, daemon=True).start()

    def _on_otap_upload(self):
        if self._upload_busy:
            return
        conn = self._conn
        if conn is None:
            self._log_line("Not connected")
            return
        path = self._txt_otap_file.text().strip()
        if not path or not os.path.isfile(path):
            self._log_line("OTAP: select a valid file first")
            return
        seq     = self._spn_otap_seq.value()
        self._last_upload_seq = seq   # saved for OTAP confirmation check
        process = self._chk_otap_process.isChecked()

        self._upload_busy = True
        self._btn_otap_upload.setEnabled(False)
        self._otap_progress.setValue(0)
        self._otap_was_polling = self._poll_timer.isActive()
        self._poll_timer.stop()

        import threading
        def _do():
            try:
                if isinstance(conn, MqttConn):
                    ok = self._otap_upload_mqtt(conn, path, seq, process)
                else:
                    # target_action is set INSIDE cmd_otap_upload, BEFORE stack restart,
                    # so Wirepas sees seq match at startup and triggers reboot immediately.
                    ok = cmd_otap_upload(
                        conn, path, seq=seq,
                        progress_cb=lambda pct, *_: self._signals.otap_progress.emit(pct),
                        log_cb=lambda msg: self._signals.log_message.emit(msg),
                        target_action=2 if process else None)
                self._signals.otap_done.emit(ok)
            finally:
                self._upload_busy = False
        threading.Thread(target=_do, daemon=True).start()

    def _otap_upload_mqtt(self, conn, path, seq, process) -> bool:
        """Upload a scratchpad file to the sink over the backend, then (optionally)
        set the target action so nodes propagate + process it."""
        log = lambda m: self._signals.log_message.emit(m)
        try:
            with open(path, "rb") as f:
                data = f.read()
        except OSError as e:
            log(f"OTAP: cannot read file: {e}")
            return False
        self._signals.otap_progress.emit(10)
        log(f"OTAP upload (MQTT): {len(data)} B seq={seq} → {conn.gw}/{conn.sink} …")
        ok = conn.otap_upload(data, seq)
        self._signals.otap_progress.emit(80)
        if not ok:
            log("OTAP upload: FAIL")
            return False
        if process:
            st = conn.otap_status()
            crc = st["crc"] if st else 0
            tok = conn.otap_set_target(2, seq, crc)      # 2 = propagate + process
            log(f"OTAP target (propagate+process) seq={seq} "
                f"crc=0x{crc:04x}: {'OK' if tok else 'FAIL'}")
        self._signals.otap_progress.emit(100)
        log("OTAP upload: OK")
        return True

    def _otap_progress_set(self, pct: int):
        self._otap_progress.setValue(pct)

    def _on_target_action_changed(self, __=None):
        action = self._cmb_target_action.currentData()
        visible = (action == 3)
        self._lbl_target_param.setVisible(visible)
        self._spn_target_param.setVisible(visible)

    def _on_otap_read_target(self):
        conn = self._conn
        if conn is None:
            self._log_line("Not connected")
            return
        import threading
        def _do():
            if isinstance(conn, MqttConn):
                d = conn.otap_read_target()
            else:
                d = cmd_otap_target_read(
                    conn, log_cb=lambda m: self._signals.log_message.emit(m))
            if d is not None:
                self._signals.otap_target.emit(d)
        threading.Thread(target=_do, daemon=True).start()

    def _on_otap_target_update(self, d: dict):
        self._spn_target_seq.setValue(d["seq"])
        self._txt_target_crc.setText(f"{d['crc']:04x}")
        action = d["action"]
        for i in range(self._cmb_target_action.count()):
            if self._cmb_target_action.itemData(i) == action:
                self._cmb_target_action.setCurrentIndex(i)
                break
        self._spn_target_param.setValue(d["param"])
        if "ok" in d:
            ok = d["ok"]
            self._lbl_target_result.setText("OK" if ok else "ERR")
            self._lbl_target_result.setStyleSheet(
                f"color:{COL_TX};" if ok else f"color:{COL_ERR};"
            )
        else:
            self._lbl_target_result.setText("")

    def _on_otap_set_target(self):
        conn = self._conn
        if conn is None:
            self._log_line("Not connected")
            return
        try:
            crc = int(self._txt_target_crc.text().strip() or "0", 16)
        except ValueError:
            self._lbl_target_result.setText("CRC invalide")
            self._lbl_target_result.setStyleSheet(f"color:{COL_ERR};")
            return
        seq    = self._spn_target_seq.value()
        action = self._cmb_target_action.currentData()
        param  = self._spn_target_param.value() if action == 3 else 0
        import threading
        def _do():
            if isinstance(conn, MqttConn):
                ok = conn.otap_set_target(action, seq, crc, param)
            else:
                ok = cmd_otap_target(conn, seq, crc=crc, action=action, param=param,
                                     log_cb=lambda m: self._signals.log_message.emit(m))
            action_name = _SCRATCH_ACTION_NAMES.get(action, str(action))
            self._signals.log_message.emit(
                f"OTAP target set: seq={seq} crc=0x{crc:04x} action={action_name}"
                + (f" delay={param}min" if action == 3 else "")
            )
            # Pass ok flag through the signal so the slot can update the result label
            self._signals.otap_target.emit(
                {"seq": seq, "crc": crc, "action": action, "param": param, "ok": ok}
            )
        threading.Thread(target=_do, daemon=True).start()

    def _on_otap_done(self, ok: bool):
        self._btn_otap_upload.setEnabled(True)
        # Restart poll timer here (main-thread slot) — QTimer.singleShot from
        # a worker thread does NOT deliver to the main event loop reliably.
        if getattr(self, "_otap_was_polling", True):
            self._poll_timer.start()
        if ok:
            self._otap_progress.setValue(100)
            self._log_line("OTAP upload finished OK")
            self._log_line("Waiting for node reboot (Wirepas propagation timer ~1-5 min)…")
            # Auto-increment seq so the next upload has a different seq (avoids
            # the bootloader skipping the update because seq == already-processed seq)
            next_seq = (self._spn_otap_seq.value() % 254) + 1
            self._spn_otap_seq.setValue(next_seq)
            self._on_otap_status()  # refresh status display
            # Monitor for reboot: poll remote node every 5s + SINK status every 30s
            self._otap_watch_seq = getattr(self, '_last_upload_seq', None)
            timer = getattr(self, "_otap_watch_timer", None)
            if timer:
                timer.stop()
            self._otap_watch_timer = QTimer()
            self._otap_watch_timer.setInterval(5_000)
            self._otap_watch_timer.timeout.connect(self._on_otap_watch_tick)
            self._otap_watch_timer.start()
            self._otap_watch_tick_count = 0
        else:
            self._log_line("OTAP upload FAILED")

    def _on_poll_toggle(self, checked: bool):
        if checked and self._conn:
            self._poll_timer.start()
        else:
            self._poll_timer.stop()

    def _on_poll_timer(self):
        conn = self._conn
        if conn is None:
            self._poll_timer.stop()
            return
        if self._poll_busy:
            return  # previous poll still in flight; skip this tick
        self._poll_busy = True
        import threading
        def _do():
            try:
                queued = cmd_poll(conn)
                self._poll_count += 1
                debug = self._chk_debug.isChecked()
                if queued is None:
                    # timeout — log on transition and throttled afterwards
                    if self._poll_last_ok is not False or self._poll_count % 20 == 0:
                        self._signals.log_message.emit("poll: no response (timeout)")
                    self._poll_last_ok = False
                else:
                    if self._poll_last_ok is not True:
                        self._signals.log_message.emit("poll: alive")
                    self._poll_last_ok = True
                    if queued and debug:
                        self._signals.log_message.emit(f"poll: {queued} indication(s) pending")
                    elif debug and self._poll_count % 20 == 0:
                        self._signals.log_message.emit("poll: heartbeat (queued=0)")
            finally:
                self._poll_busy = False
        threading.Thread(target=_do, daemon=True).start()

    def _on_rx_buf_changed(self, value: int):
        self._rx_model.set_max_rows(value)

    def _on_log_window_closed(self, *_):
        self._chk_show_log.setChecked(False)

    def _on_log_toggle(self, checked: bool):
        if checked:
            self._log_window.show()
            self._log_window.raise_()
            self._log_window.activateWindow()
        else:
            self._log_window.hide()

    # ── Log helpers ──────────────────────────────────────────────────────────

    def _log_line(self, msg: str):
        ts = time.strftime("%H:%M:%S")
        self._log_window.append(f"<span style='color:#555;'>[{ts}]</span> {msg}")

    def closeEvent(self, event):
        self._on_disconnect()
        # Close every auxiliary window so the app fully exits with the main one.
        for win in (self._log_window, self._motor_window, self._sensor_window,
                    self._tag_window):
            try:
                win.close()
            except Exception:
                pass
        super().closeEvent(event)


# ─── Entry point ──────────────────────────────────────────────────────────────
def main():
    ap = argparse.ArgumentParser(description="Wirepas UART GUI")
    ap.add_argument("-p", "--port",
                    default="/dev/cu.usbmodem0010577746453",
                    help="Serial port")
    ap.add_argument("-b", "--baudrate", default=125000,  type=int)
    args = ap.parse_args()

    app = QApplication(sys.argv)
    app.setStyleSheet(STYLESHEET)
    win = MainWindow(initial_port=args.port, initial_baud=args.baudrate)
    win.show()
    sys.exit(app.exec())


if __name__ == "__main__":
    main()
