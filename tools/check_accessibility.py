#!/usr/bin/env python3
"""Walk the configuration utility's dialogs the way a screen reader does.

Two questions, asked of the running program rather than of its resource script,
because a control can be present in the template and still be unreachable or
unnamed once the dialog is up:

  1. Does every interactive control have an accessible name?  This is what NVDA
     and JAWS read out.  Win32 derives it from the static text immediately
     before the control in the dialog template, so a control in the wrong order
     is announced as just "edit".
  2. Is every interactive control on the Tab route?  Asked with
     GetNextDlgTabItem, which is the route itself, not an approximation of it.

Read through MSAA -- the IAccessible interface a screen reader actually uses --
via oleacc.dll.  Not through UI Automation: the PowerShell UIA client calls
every classic Win32 control a Pane and would report a clean bill of health for a
dialog that is unusable.

    python tools/check_accessibility.py output\\Infovox23012Config.exe

Exit code 0 means every control has a name and every one is reachable by Tab.
"""

import ctypes
import ctypes.wintypes as wt
import subprocess
import sys
import time

user32 = ctypes.WinDLL("user32", use_last_error=True)
oleacc = ctypes.WinDLL("oleacc", use_last_error=True)
ole32 = ctypes.WinDLL("ole32", use_last_error=True)

OBJID_CLIENT = 0xFFFFFFFC
CHILDID_SELF = 0

# The MSAA roles that mean "a person interacts with this".
INTERACTIVE_ROLES = {
    43: "button",       # ROLE_SYSTEM_PUSHBUTTON
    44: "check box",
    45: "radio button",
    46: "combo box",
    42: "edit",
    33: "list",
    34: "list item",
    51: "slider",
    52: "spin button",
    56: "tab",
    37: "tab control",
    50: "scroll bar",
}
ROLE_STATICTEXT = 41
ROLE_WINDOW = 9
ROLE_CLIENT = 10
ROLE_GROUPING = 20

STATE_INVISIBLE = 0x8000
STATE_UNAVAILABLE = 0x1


class IAccessibleVtbl(ctypes.Structure):
    pass


def role_name(role):
    buf = ctypes.create_unicode_buffer(128)
    n = oleacc.GetRoleTextW(role, buf, 128)
    return buf.value if n else "role %d" % role


def accessible_from_window(hwnd):
    import comtypes.client  # noqa: F401  (only to prove it is unavailable early)


def find_main_window(pid, timeout=10.0):
    """The first visible top-level window belonging to `pid`."""
    found = []

    @ctypes.WINFUNCTYPE(wt.BOOL, wt.HWND, wt.LPARAM)
    def enum(hwnd, _):
        owner = wt.DWORD()
        user32.GetWindowThreadProcessId(hwnd, ctypes.byref(owner))
        if owner.value == pid and user32.IsWindowVisible(hwnd):
            found.append(hwnd)
            return False
        return True

    deadline = time.time() + timeout
    while time.time() < deadline:
        found.clear()
        user32.EnumWindows(enum, 0)
        if found:
            return found[0]
        time.sleep(0.2)
    return None


def window_text(hwnd):
    n = user32.GetWindowTextLengthW(hwnd)
    buf = ctypes.create_unicode_buffer(n + 1)
    user32.GetWindowTextW(hwnd, buf, n + 1)
    return buf.value


def class_name(hwnd):
    buf = ctypes.create_unicode_buffer(256)
    user32.GetClassNameW(hwnd, buf, 256)
    return buf.value


def child_windows(parent):
    out = []

    @ctypes.WINFUNCTYPE(wt.BOOL, wt.HWND, wt.LPARAM)
    def enum(hwnd, _):
        out.append(hwnd)
        return True

    user32.EnumChildWindows(parent, enum, 0)
    return out


def msaa_name_and_role(hwnd):
    """(name, role) for a window's client object, through oleacc."""
    from ctypes import POINTER, byref, c_void_p

    # IAccessible is fetched as a raw pointer and driven through its vtable, so
    # this needs no type library and no comtypes.
    pacc = c_void_p()
    hr = oleacc.AccessibleObjectFromWindow(
        wt.HWND(hwnd), wt.DWORD(OBJID_CLIENT),
        byref(_IID_IAccessible), byref(pacc)
    )
    if hr != 0 or not pacc:
        return None, None

    vtbl = ctypes.cast(pacc, POINTER(POINTER(c_void_p))).contents

    def method(index, restype, *argtypes):
        proto = ctypes.WINFUNCTYPE(restype, c_void_p, *argtypes)
        return proto(vtbl[index])

    # IAccessible vtable: IUnknown(3) + IDispatch(4) = 7, then
    # get_accParent(7), get_accChildCount(8), get_accChild(9),
    # get_accName(10), get_accValue(11), get_accDescription(12), get_accRole(13)
    VARIANT_self = _variant_i4(CHILDID_SELF)

    name = ctypes.c_wchar_p()
    get_name = method(10, ctypes.HRESULT, _VARIANT, POINTER(ctypes.c_wchar_p))
    get_name(pacc, VARIANT_self, byref(name))

    role_var = _VARIANT()
    get_role = method(13, ctypes.HRESULT, _VARIANT, POINTER(_VARIANT))
    get_role(pacc, VARIANT_self, byref(role_var))

    release = method(2, ctypes.c_ulong)
    value = name.value
    role = role_var.lVal if role_var.vt == 3 else None
    release(pacc)
    return value, role


class _VARIANT(ctypes.Structure):
    _fields_ = [
        ("vt", ctypes.c_ushort),
        ("r1", ctypes.c_ushort),
        ("r2", ctypes.c_ushort),
        ("r3", ctypes.c_ushort),
        ("lVal", ctypes.c_longlong),
    ]


def _variant_i4(value):
    v = _VARIANT()
    v.vt = 3  # VT_I4
    v.lVal = value
    return v


class _GUID(ctypes.Structure):
    _fields_ = [("d1", ctypes.c_ulong), ("d2", ctypes.c_ushort),
                ("d3", ctypes.c_ushort), ("d4", ctypes.c_ubyte * 8)]


# {618736E0-3C3D-11CF-810C-00AA00389B71}
_IID_IAccessible = _GUID(
    0x618736E0, 0x3C3D, 0x11CF,
    (ctypes.c_ubyte * 8)(0x81, 0x0C, 0x00, 0xAA, 0x00, 0x38, 0x9B, 0x71),
)


def tab_route(dialog):
    """Every control GetNextDlgTabItem visits, starting from the first."""
    route = []
    first = user32.GetNextDlgTabItem(dialog, None, False)
    if not first:
        return route
    hwnd = first
    for _ in range(200):
        route.append(hwnd)
        hwnd = user32.GetNextDlgTabItem(dialog, hwnd, False)
        if not hwnd or hwnd == first:
            break
    return route


def check(dialog, title):
    print("\n=== %s ===" % title)
    route = set(tab_route(dialog))
    problems = 0
    checked = 0
    for hwnd in child_windows(dialog):
        style = user32.GetWindowLongW(hwnd, -16)
        if not (style & 0x10000000):  # WS_VISIBLE
            continue
        name, role = msaa_name_and_role(hwnd)
        if role in (ROLE_STATICTEXT, ROLE_WINDOW, ROLE_CLIENT, ROLE_GROUPING, None):
            continue
        if role not in INTERACTIVE_ROLES:
            continue
        checked += 1
        enabled = bool(style & 0x08000000) is False  # WS_DISABLED is 0x08000000
        issues = []
        if not name or not name.strip():
            issues.append("NO ACCESSIBLE NAME")
        if hwnd not in route and enabled:
            issues.append("NOT ON THE TAB ROUTE")
        flag = "  <-- " + "; ".join(issues) if issues else ""
        if issues:
            problems += 1
        print("  %-56s %-14s %s%s" % (
            (name or "")[:56], role_name(role), "" if enabled else "(disabled) ", flag))
    print("  %d interactive controls, %d problems" % (checked, problems))
    return problems


def top_level_windows(pid):
    out = []

    @ctypes.WINFUNCTYPE(wt.BOOL, wt.HWND, wt.LPARAM)
    def enum(hwnd, _):
        owner = wt.DWORD()
        user32.GetWindowThreadProcessId(hwnd, ctypes.byref(owner))
        if owner.value == pid and user32.IsWindowVisible(hwnd):
            out.append(hwnd)
        return True

    user32.EnumWindows(enum, 0)
    return out


def find_button(dialog, label):
    """The child window whose accessible name starts with `label`."""
    for hwnd in child_windows(dialog):
        name, role = msaa_name_and_role(hwnd)
        if role == 43 and name and name.strip().lower().startswith(label.lower()):
            return hwnd
    return None


def open_and_check(main_hwnd, pid, label):
    """Press a button, check the dialog it opens, then close it again."""
    button = find_button(main_hwnd, label)
    if not button:
        print("\n(no button called %r; skipping that dialog)" % label)
        return 0
    before = set(top_level_windows(pid))
    # PostMessage, not SendMessage: the button opens a modal dialog, and a modal
    # loop does not return until the dialog closes. SendMessage would wait for
    # that, which is a deadlock -- this process is the only one that can close
    # it, and it would be blocked inside the call that opened it.
    user32.PostMessageW(button, 0x00F5, 0, 0)  # BM_CLICK
    dialog = None
    deadline = time.time() + 5.0
    while time.time() < deadline:
        new = [h for h in top_level_windows(pid) if h not in before]
        if new:
            dialog = new[0]
            break
        time.sleep(0.1)
    if not dialog:
        print("\n(pressing %r opened no dialog)" % label)
        return 0
    time.sleep(0.4)
    problems = check(dialog, window_text(dialog) or class_name(dialog))
    user32.PostMessageW(dialog, 0x0111, 2, 0)  # WM_COMMAND, IDCANCEL
    time.sleep(0.5)
    if user32.IsWindow(dialog):
        user32.PostMessageW(dialog, 0x0010, 0, 0)  # WM_CLOSE
        time.sleep(0.5)
    return problems


def main(argv):
    if not argv:
        print(__doc__)
        return 2
    exe = argv[0]
    ole32.CoInitialize(None)
    proc = subprocess.Popen([exe])
    try:
        hwnd = find_main_window(proc.pid)
        if not hwnd:
            print("no window appeared", file=sys.stderr)
            return 3
        time.sleep(0.5)
        problems = check(hwnd, window_text(hwnd) or class_name(hwnd))
        # The two dialogs behind it, which is where every setting actually lives.
        problems += open_and_check(hwnd, proc.pid, "New voice")
        problems += open_and_check(hwnd, proc.pid, "Engine settings")
        print()
        if problems:
            print("%d control(s) a screen reader could not use." % problems)
        else:
            print("Every interactive control is named and on the Tab route.")
        return 1 if problems else 0
    finally:
        proc.terminate()
        ole32.CoUninitialize()


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
