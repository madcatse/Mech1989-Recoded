#!/usr/bin/env python3
"""
GPU viewport for the MechWarrior 1989 model viewer.

The module has two halves:

* pure geometry preparation that can be imported and tested without a GL
  context;
* a small Windows WGL/Tk renderer that uploads triangulated records to
  VBO/EBO buffers and draws them with the fixed-function OpenGL pipeline.

Keeping OpenGL optional at import time lets parser/triangulation/color tests run
on machines that cannot create a window.
"""
from __future__ import annotations

import ctypes
import math
import sys
import time
from dataclasses import dataclass
from typing import Dict, Iterable, List, Optional, Sequence, Tuple

import numpy as np
import tkinter as tk

Vec3 = Tuple[float, float, float]
Mat4 = Tuple[float, ...]


@dataclass
class GpuBatch:
    vertices: np.ndarray
    triangle_indices: np.ndarray
    line_indices: np.ndarray
    group_matrices: Dict[int, np.ndarray]
    triangle_count: int
    line_count: int
    polygon_count: int
    color_count: int


def sub(a: Vec3, b: Vec3) -> Vec3:
    return (a[0] - b[0], a[1] - b[1], a[2] - b[2])


def cross(a: Vec3, b: Vec3) -> Vec3:
    return (
        a[1] * b[2] - a[2] * b[1],
        a[2] * b[0] - a[0] * b[2],
        a[0] * b[1] - a[1] * b[0],
    )


def dot(a: Vec3, b: Vec3) -> float:
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]


def normalize(v: Vec3) -> Vec3:
    d = math.sqrt(dot(v, v))
    if d <= 1e-9:
        return (0.0, 0.0, 1.0)
    return (v[0] / d, v[1] / d, v[2] / d)


def rotate_xyz(v: Vec3, yaw: float, pitch: float, roll: float) -> Vec3:
    x, y, z = v
    cy, sy = math.cos(yaw), math.sin(yaw)
    x, z = x * cy + z * sy, -x * sy + z * cy
    cp, sp = math.cos(pitch), math.sin(pitch)
    y, z = y * cp - z * sp, y * sp + z * cp
    cr, sr = math.cos(roll), math.sin(roll)
    x, y = x * cr - y * sr, x * sr + y * cr
    return x, y, z


def _rgb01_from_hex(color: str) -> Tuple[float, float, float]:
    color = color.strip()
    if color.startswith("#") and len(color) == 7:
        return (
            int(color[1:3], 16) / 255.0,
            int(color[3:5], 16) / 255.0,
            int(color[5:7], 16) / 255.0,
        )
    return (170.0 / 255.0, 170.0 / 255.0, 170.0 / 255.0)


def _gray01(level: int) -> Tuple[float, float, float]:
    v = max(0, min(255, int(level))) / 255.0
    return (v, v, v)


def primitive_rgb01(prim, normal: Vec3, *, shade_mode: str, light_dir: Vec3) -> Tuple[float, float, float]:
    if shade_mode == "ega" and prim.shade_level is not None:
        ega = {
            0: "#000000", 1: "#0000aa", 2: "#00aa00", 3: "#00aaaa",
            4: "#aa0000", 5: "#aa00aa", 6: "#aa5500", 7: "#aaaaaa",
            8: "#555555", 9: "#5555ff", 10: "#55ff55", 11: "#55ffff",
            12: "#ff5555", 13: "#ff55ff", 14: "#ffff55", 15: "#ffffff",
        }
        return _rgb01_from_hex(ega.get(int(round(prim.shade_level)) & 0x0F, "#aaaaaa"))
    if shade_mode == "stored" and prim.shade_level is not None:
        level = 58 + int(max(0.0, min(15.0, prim.shade_level)) * 11.5)
        if prim.material_or_flags & 0x80:
            level += 10
        return _gray01(level)
    lit = max(abs(dot(normal, light_dir)), 0.18)
    group_bias = (prim.group_index % 4) * 12
    return _gray01(78 + int(lit * 132) + group_bias)


def build_group_matrices(record) -> Dict[int, np.ndarray]:
    groups = {int(p.group_index) for p in record.primitives}
    declared = int(record.raw.get("group_count", 0) or 0)
    groups.update(range(max(0, declared)))
    ident = np.identity(4, dtype=np.float32)
    return {g: ident.copy() for g in sorted(groups)}


def triangulate_record(
    record,
    *,
    shade_mode: str = "stored",
    light_dir: Vec3 = normalize((0.35, 0.65, -0.95)),
    yaw: float = 0.0,
    pitch: float = 0.0,
    roll: float = 0.0,
    show_lines: bool = False,
    show_edges: bool = True,
    cull_mode: str = "off",
    edge_rgb: Tuple[float, float, float] = (0.02, 0.02, 0.02),
) -> GpuBatch:
    """Flatten record primitives into GPU-friendly vertices plus EBO indices.

    Vertices are intentionally duplicated per primitive so every polygon can
    keep the flat color assigned by the original diagnostic viewer.
    """
    vertices: List[Tuple[float, float, float, float, float, float]] = []
    tri_indices: List[int] = []
    line_indices: List[int] = []
    light = normalize(light_dir)

    transformed = [rotate_xyz(v, yaw, pitch, roll) for v in record.vertices]

    def signed_area2(points: Sequence[Tuple[float, float]]) -> float:
        s = 0.0
        for i, (x0, y0) in enumerate(points):
            x1, y1 = points[(i + 1) % len(points)]
            s += x0 * y1 - x1 * y0
        return s

    def is_culled(normal: Vec3, pts2: Sequence[Tuple[float, float]]) -> bool:
        if cull_mode == "nz":
            return normal[2] < 0.0
        if cull_mode == "pz":
            return normal[2] > 0.0
        if cull_mode == "screen_cw":
            return signed_area2(pts2) < 0.0
        if cull_mode == "screen_ccw":
            return signed_area2(pts2) > 0.0
        return False

    polygon_count = 0
    for prim in record.primitives:
        valid = [i for i in prim.indices if 0 <= i < len(record.vertices)]
        if len(valid) < 2:
            continue
        pts = [record.vertices[i] for i in valid]
        pts_t = [transformed[i] for i in valid]
        if prim.kind == "line":
            if show_lines and show_edges:
                rgb = (0.94, 0.94, 0.94)
                start = len(vertices)
                for x, y, z in pts:
                    vertices.append((x, y, z, *rgb))
                for i in range(len(pts) - 1):
                    line_indices.extend([start + i, start + i + 1])
            continue
        if len(pts) < 3:
            continue
        normal = normalize(cross(sub(pts_t[1], pts_t[0]), sub(pts_t[2], pts_t[0])))
        if is_culled(normal, [(p[0], p[1]) for p in pts_t]):
            continue
        polygon_count += 1
        rgb = primitive_rgb01(prim, normal, shade_mode=shade_mode, light_dir=light)
        start = len(vertices)
        for x, y, z in pts:
            vertices.append((x, y, z, *rgb))
        for j in range(1, len(pts) - 1):
            tri_indices.extend([start, start + j, start + j + 1])
        if show_edges:
            edge_start = len(vertices)
            for x, y, z in pts:
                vertices.append((x, y, z, *edge_rgb))
            for j in range(len(pts)):
                line_indices.extend([edge_start + j, edge_start + ((j + 1) % len(pts))])

    if vertices:
        vertex_array = np.asarray(vertices, dtype=np.float32)
    else:
        vertex_array = np.zeros((0, 6), dtype=np.float32)
    return GpuBatch(
        vertices=vertex_array,
        triangle_indices=np.asarray(tri_indices, dtype=np.uint32),
        line_indices=np.asarray(line_indices, dtype=np.uint32),
        group_matrices=build_group_matrices(record),
        triangle_count=len(tri_indices) // 3,
        line_count=len(line_indices) // 2,
        polygon_count=polygon_count,
        color_count=len({tuple(row[3:6]) for row in vertex_array.tolist()}) if len(vertex_array) else 0,
    )


def _identity() -> np.ndarray:
    return np.identity(4, dtype=np.float32)


def _translation(x: float, y: float, z: float) -> np.ndarray:
    m = _identity()
    m[0, 3] = x
    m[1, 3] = y
    m[2, 3] = z
    return m


def _scale(x: float, y: float, z: float) -> np.ndarray:
    m = _identity()
    m[0, 0] = x
    m[1, 1] = y
    m[2, 2] = z
    return m


def _rotation(yaw: float, pitch: float, roll: float) -> np.ndarray:
    cy, sy = math.cos(yaw), math.sin(yaw)
    cp, sp = math.cos(pitch), math.sin(pitch)
    cr, sr = math.cos(roll), math.sin(roll)
    ry = np.array([[cy, 0.0, sy, 0.0], [0.0, 1.0, 0.0, 0.0], [-sy, 0.0, cy, 0.0], [0.0, 0.0, 0.0, 1.0]], dtype=np.float32)
    rx = np.array([[1.0, 0.0, 0.0, 0.0], [0.0, cp, -sp, 0.0], [0.0, sp, cp, 0.0], [0.0, 0.0, 0.0, 1.0]], dtype=np.float32)
    rz = np.array([[cr, -sr, 0.0, 0.0], [sr, cr, 0.0, 0.0], [0.0, 0.0, 1.0, 0.0], [0.0, 0.0, 0.0, 1.0]], dtype=np.float32)
    return rz @ rx @ ry


def _ortho(left: float, right: float, bottom: float, top: float, near: float, far: float) -> np.ndarray:
    m = np.zeros((4, 4), dtype=np.float32)
    m[0, 0] = 2.0 / (right - left)
    m[1, 1] = 2.0 / (top - bottom)
    m[2, 2] = -2.0 / (far - near)
    m[0, 3] = -(right + left) / (right - left)
    m[1, 3] = -(top + bottom) / (top - bottom)
    m[2, 3] = -(far + near) / (far - near)
    m[3, 3] = 1.0
    return m


def _perspective_from_focal(focal_pixels: float, width: int, height: int, near: float, far: float) -> np.ndarray:
    f_y = max(0.01, focal_pixels / max(1.0, height * 0.5))
    aspect = max(1.0, float(width)) / max(1.0, float(height))
    m = np.zeros((4, 4), dtype=np.float32)
    m[0, 0] = f_y / aspect
    m[1, 1] = f_y
    m[2, 2] = -(far + near) / (far - near)
    m[2, 3] = -(2.0 * far * near) / (far - near)
    m[3, 2] = -1.0
    return m


class OpenGLViewport(tk.Frame):
    def __init__(self, master, **kwargs) -> None:
        super().__init__(master, background="#101010", highlightthickness=0, **kwargs)
        self.overlay = tk.Label(self, anchor="nw", justify="left", fg="#f4f4f4", bg="#101010")
        self.help = tk.Label(self, anchor="sw", justify="left", fg="#aaaaaa", bg="#101010")
        self.overlay.place(x=12, y=10)
        self.help.place(x=12, rely=1.0, y=-8, anchor="sw")

    def set_overlay(self, text: str, help_text: str = "") -> None:
        self.overlay.configure(text=text)
        self.help.configure(text=help_text)


if sys.platform == "win32":
    user32 = ctypes.windll.user32
    gdi32 = ctypes.windll.gdi32
    opengl32 = ctypes.windll.opengl32
    user32.GetDC.restype = ctypes.c_void_p
    user32.GetDC.argtypes = [ctypes.c_void_p]
    gdi32.ChoosePixelFormat.restype = ctypes.c_int
    gdi32.ChoosePixelFormat.argtypes = [ctypes.c_void_p, ctypes.c_void_p]
    gdi32.SetPixelFormat.restype = ctypes.c_int
    gdi32.SetPixelFormat.argtypes = [ctypes.c_void_p, ctypes.c_int, ctypes.c_void_p]
    gdi32.SwapBuffers.restype = ctypes.c_int
    gdi32.SwapBuffers.argtypes = [ctypes.c_void_p]
    opengl32.wglCreateContext.restype = ctypes.c_void_p
    opengl32.wglCreateContext.argtypes = [ctypes.c_void_p]
    opengl32.wglMakeCurrent.restype = ctypes.c_int
    opengl32.wglMakeCurrent.argtypes = [ctypes.c_void_p, ctypes.c_void_p]
else:  # pragma: no cover - current target is Windows/Tk.
    user32 = gdi32 = opengl32 = None


class PIXELFORMATDESCRIPTOR(ctypes.Structure):
    _fields_ = [
        ("nSize", ctypes.c_ushort),
        ("nVersion", ctypes.c_ushort),
        ("dwFlags", ctypes.c_uint),
        ("iPixelType", ctypes.c_ubyte),
        ("cColorBits", ctypes.c_ubyte),
        ("cRedBits", ctypes.c_ubyte),
        ("cRedShift", ctypes.c_ubyte),
        ("cGreenBits", ctypes.c_ubyte),
        ("cGreenShift", ctypes.c_ubyte),
        ("cBlueBits", ctypes.c_ubyte),
        ("cBlueShift", ctypes.c_ubyte),
        ("cAlphaBits", ctypes.c_ubyte),
        ("cAlphaShift", ctypes.c_ubyte),
        ("cAccumBits", ctypes.c_ubyte),
        ("cAccumRedBits", ctypes.c_ubyte),
        ("cAccumGreenBits", ctypes.c_ubyte),
        ("cAccumBlueBits", ctypes.c_ubyte),
        ("cAccumAlphaBits", ctypes.c_ubyte),
        ("cDepthBits", ctypes.c_ubyte),
        ("cStencilBits", ctypes.c_ubyte),
        ("cAuxBuffers", ctypes.c_ubyte),
        ("iLayerType", ctypes.c_ubyte),
        ("bReserved", ctypes.c_ubyte),
        ("dwLayerMask", ctypes.c_uint),
        ("dwVisibleMask", ctypes.c_uint),
        ("dwDamageMask", ctypes.c_uint),
    ]


PFD_DRAW_TO_WINDOW = 0x00000004
PFD_SUPPORT_OPENGL = 0x00000020
PFD_DOUBLEBUFFER = 0x00000001
PFD_TYPE_RGBA = 0
PFD_MAIN_PLANE = 0

GL_COLOR_BUFFER_BIT = 0x00004000
GL_DEPTH_BUFFER_BIT = 0x00000100
GL_LINES = 0x0001
GL_TRIANGLES = 0x0004
GL_FLOAT = 0x1406
GL_UNSIGNED_INT = 0x1405
GL_DEPTH_TEST = 0x0B71
GL_LEQUAL = 0x0203
GL_ARRAY_BUFFER = 0x8892
GL_ELEMENT_ARRAY_BUFFER = 0x8893
GL_STATIC_DRAW = 0x88E4
GL_VERTEX_ARRAY = 0x8074
GL_COLOR_ARRAY = 0x8076
GL_MODELVIEW = 0x1700
GL_PROJECTION = 0x1701


class WglFunctions:
    def __init__(self) -> None:
        self.glClearColor = self._base("glClearColor", None, ctypes.c_float, ctypes.c_float, ctypes.c_float, ctypes.c_float)
        self.glClear = self._base("glClear", None, ctypes.c_uint)
        self.glViewport = self._base("glViewport", None, ctypes.c_int, ctypes.c_int, ctypes.c_int, ctypes.c_int)
        self.glEnable = self._base("glEnable", None, ctypes.c_uint)
        self.glDisable = self._base("glDisable", None, ctypes.c_uint)
        self.glDepthFunc = self._base("glDepthFunc", None, ctypes.c_uint)
        self.glMatrixMode = self._base("glMatrixMode", None, ctypes.c_uint)
        self.glLoadMatrixf = self._base("glLoadMatrixf", None, ctypes.POINTER(ctypes.c_float))
        self.glEnableClientState = self._base("glEnableClientState", None, ctypes.c_uint)
        self.glDisableClientState = self._base("glDisableClientState", None, ctypes.c_uint)
        self.glVertexPointer = self._base("glVertexPointer", None, ctypes.c_int, ctypes.c_uint, ctypes.c_int, ctypes.c_void_p)
        self.glColorPointer = self._base("glColorPointer", None, ctypes.c_int, ctypes.c_uint, ctypes.c_int, ctypes.c_void_p)
        self.glDrawElements = self._base("glDrawElements", None, ctypes.c_uint, ctypes.c_int, ctypes.c_uint, ctypes.c_void_p)
        self.glLineWidth = self._base("glLineWidth", None, ctypes.c_float)
        self.glGenBuffers = self._proc("glGenBuffers", None, ctypes.c_int, ctypes.POINTER(ctypes.c_uint))
        self.glBindBuffer = self._proc("glBindBuffer", None, ctypes.c_uint, ctypes.c_uint)
        self.glBufferData = self._proc("glBufferData", None, ctypes.c_uint, ctypes.c_size_t, ctypes.c_void_p, ctypes.c_uint)
        self.glDeleteBuffers = self._proc("glDeleteBuffers", None, ctypes.c_int, ctypes.POINTER(ctypes.c_uint))

    def _base(self, name: str, restype, *argtypes):
        func = getattr(opengl32, name)
        func.restype = restype
        func.argtypes = argtypes
        return func

    def _proc(self, name: str, restype, *argtypes):
        addr = opengl32.wglGetProcAddress(name.encode("ascii"))
        if not addr:
            raise RuntimeError(f"OpenGL function {name} is unavailable; VBO/EBO support is required.")
        proto = ctypes.WINFUNCTYPE(restype, *argtypes)
        return proto(addr)


class GpuRenderer:
    def __init__(self, viewport: OpenGLViewport) -> None:
        self.viewport = viewport
        self.yaw = -1.10
        self.pitch = 0.10
        self.roll = 0.0
        self.zoom = 1.0
        self.pan_x = 0.0
        self.pan_y = 0.0
        self.perspective = True
        self.solid = True
        self.show_edges = True
        self.show_vertices = False
        self.show_axes = True
        self.show_lines = False
        self.group_sort_mode = True
        self.depth_order = "far"
        self.shade_mode = "stored"
        self.cull_mode = "off"
        self.render_backend = "gpu-vbo-ebo"
        self.last_render_ms = 0.0
        self.background = "#101010"
        self.light_dir = normalize((0.35, 0.65, -0.95))
        self._hwnd = None
        self._hdc = None
        self._hrc = None
        self._gl: Optional[WglFunctions] = None
        self._vbo = ctypes.c_uint(0)
        self._tri_ebo = ctypes.c_uint(0)
        self._line_ebo = ctypes.c_uint(0)
        self._uploaded_signature = None
        self._uploaded: Optional[GpuBatch] = None
        self._last_error: Optional[str] = None

    def reset_view(self) -> None:
        self.apply_view_preset("game")
        self.zoom = 1.0
        self.pan_x = 0.0
        self.pan_y = 0.0

    def apply_view_preset(self, name: str) -> None:
        name = name.lower()
        if name == "side":
            self.yaw = -1.57
            self.pitch = 0.00
            self.roll = 0.0
            self.perspective = False
        elif name == "front":
            self.yaw = 0.0
            self.pitch = 0.0
            self.roll = 0.0
            self.perspective = False
        elif name == "top":
            self.yaw = 0.0
            self.pitch = 1.57
            self.roll = 0.0
            self.perspective = False
        else:
            self.yaw = -1.10
            self.pitch = 0.10
            self.roll = 0.0
            self.perspective = True

    def fit_record(self, record, width: int, height: int, *, target: float = 0.68) -> None:
        if not record.vertices:
            self.zoom = 1.0
            return
        cx, cy, cz = record.center
        pts = [rotate_xyz((x - cx, y - cy, z - cz), self.yaw, self.pitch, self.roll) for x, y, z in record.vertices]
        old_zoom = self.zoom
        self.zoom = 1.0
        base_scale = min(width, height) * 0.42 / max(record.radius, 1.0)
        projected = []
        for x, y, z in pts:
            if self.perspective:
                camera_distance = max(record.radius * 4.0, 100.0)
                factor = camera_distance / max(camera_distance + z, record.radius * 0.35)
            else:
                factor = 1.0
            projected.append((x * base_scale * factor, y * base_scale * factor))
        self.zoom = old_zoom
        if not projected:
            return
        span_x = max(p[0] for p in projected) - min(p[0] for p in projected)
        span_y = max(p[1] for p in projected) - min(p[1] for p in projected)
        if span_x <= 1e-6 or span_y <= 1e-6:
            return
        usable_w = max(80.0, width * target)
        usable_h = max(80.0, height * target)
        self.zoom = max(0.1, min(35.0, min(usable_w / span_x, usable_h / span_y)))
        self.pan_x = 0.0
        self.pan_y = 0.0

    def _ensure_context(self) -> bool:
        if self._gl is not None:
            return True
        if sys.platform != "win32":
            self._last_error = "GPU viewer currently creates a native WGL context, so it must run on Windows."
            return False
        self.viewport.update_idletasks()
        hwnd = int(self.viewport.winfo_id())
        hdc = user32.GetDC(hwnd)
        pfd = PIXELFORMATDESCRIPTOR()
        pfd.nSize = ctypes.sizeof(PIXELFORMATDESCRIPTOR)
        pfd.nVersion = 1
        pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER
        pfd.iPixelType = PFD_TYPE_RGBA
        pfd.cColorBits = 32
        pfd.cDepthBits = 24
        pfd.iLayerType = PFD_MAIN_PLANE
        fmt = gdi32.ChoosePixelFormat(hdc, ctypes.byref(pfd))
        if not fmt or not gdi32.SetPixelFormat(hdc, fmt, ctypes.byref(pfd)):
            self._last_error = "Could not set an OpenGL pixel format for the Tk viewport."
            return False
        hrc = opengl32.wglCreateContext(hdc)
        if not hrc or not opengl32.wglMakeCurrent(hdc, hrc):
            self._last_error = "Could not create an OpenGL context."
            return False
        opengl32.wglGetProcAddress.restype = ctypes.c_void_p
        opengl32.wglGetProcAddress.argtypes = [ctypes.c_char_p]
        self._hwnd = hwnd
        self._hdc = hdc
        self._hrc = hrc
        try:
            self._gl = WglFunctions()
        except Exception as exc:
            self._last_error = str(exc)
            return False
        buffers = (ctypes.c_uint * 3)()
        self._gl.glGenBuffers(3, buffers)
        self._vbo = ctypes.c_uint(buffers[0])
        self._tri_ebo = ctypes.c_uint(buffers[1])
        self._line_ebo = ctypes.c_uint(buffers[2])
        self._gl.glEnable(GL_DEPTH_TEST)
        self._gl.glDepthFunc(GL_LEQUAL)
        return True

    def _upload(self, batch: GpuBatch, signature) -> None:
        gl = self._gl
        assert gl is not None
        if self._uploaded_signature == signature:
            return
        gl.glBindBuffer(GL_ARRAY_BUFFER, self._vbo.value)
        v = np.ascontiguousarray(batch.vertices, dtype=np.float32)
        gl.glBufferData(GL_ARRAY_BUFFER, v.nbytes, v.ctypes.data_as(ctypes.c_void_p), GL_STATIC_DRAW)
        gl.glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, self._tri_ebo.value)
        tri = np.ascontiguousarray(batch.triangle_indices, dtype=np.uint32)
        gl.glBufferData(GL_ELEMENT_ARRAY_BUFFER, tri.nbytes, tri.ctypes.data_as(ctypes.c_void_p), GL_STATIC_DRAW)
        gl.glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, self._line_ebo.value)
        lines = np.ascontiguousarray(batch.line_indices, dtype=np.uint32)
        gl.glBufferData(GL_ELEMENT_ARRAY_BUFFER, lines.nbytes, lines.ctypes.data_as(ctypes.c_void_p), GL_STATIC_DRAW)
        self._uploaded_signature = signature
        self._uploaded = batch

    def _matrices(self, record, width: int, height: int) -> Tuple[np.ndarray, np.ndarray]:
        radius = max(record.radius, 1.0)
        base_scale = min(width, height) * 0.42 * self.zoom / radius
        cx, cy, cz = record.center
        model = _scale(1.0, 1.0, -1.0) @ _rotation(self.yaw, self.pitch, self.roll) @ _translation(-cx, -cy, -cz)
        if self.perspective:
            camera_distance = max(radius * 4.0, 100.0)
            projection = _perspective_from_focal(base_scale * camera_distance, width, height, 1.0, max(1000.0, camera_distance + radius * 8.0))
            pan_world_x = self.pan_x / max(base_scale, 1e-6)
            pan_world_y = -self.pan_y / max(base_scale, 1e-6)
            modelview = _translation(0.0, 0.0, -camera_distance) @ _translation(pan_world_x, pan_world_y, 0.0) @ model
        else:
            half_w = max(1.0, width * 0.5 / max(base_scale, 1e-6))
            half_h = max(1.0, height * 0.5 / max(base_scale, 1e-6))
            pan_world_x = self.pan_x / max(base_scale, 1e-6)
            pan_world_y = self.pan_y / max(base_scale, 1e-6)
            projection = _ortho(-half_w - pan_world_x, half_w - pan_world_x, -half_h + pan_world_y, half_h + pan_world_y, -max(1000.0, radius * 8.0), max(1000.0, radius * 8.0))
            modelview = model
        return projection, modelview

    def render(self, record, overlay: str = "") -> None:
        width = max(self.viewport.winfo_width(), 320)
        height = max(self.viewport.winfo_height(), 240)
        if record is None:
            self.viewport.set_overlay("Open a JENPCK/LOCPCK/MARPCK .TBL file to inspect records.")
            return
        if not self._ensure_context():
            self.viewport.set_overlay(f"OpenGL unavailable: {self._last_error or 'unknown error'}")
            return
        t0 = time.perf_counter()
        batch = triangulate_record(
            record,
            shade_mode=self.shade_mode,
            light_dir=self.light_dir,
            yaw=self.yaw,
            pitch=self.pitch,
            roll=self.roll,
            show_lines=self.show_lines,
            show_edges=self.show_edges,
            cull_mode=self.cull_mode,
            edge_rgb=(0.86, 0.86, 0.86) if not self.solid else (0.02, 0.02, 0.02),
        )
        signature = (
            id(record),
            len(record.vertices),
            len(record.primitives),
            self.shade_mode,
            self.show_lines,
            self.show_edges,
            self.cull_mode,
            self.solid,
            round(self.yaw, 5),
            round(self.pitch, 5),
            round(self.roll, 5),
        )
        self._upload(batch, signature)
        gl = self._gl
        assert gl is not None
        bg = _rgb01_from_hex(self.background)
        gl.glViewport(0, 0, width, height)
        gl.glClearColor(bg[0], bg[1], bg[2], 1.0)
        gl.glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT)
        projection, modelview = self._matrices(record, width, height)
        gl.glMatrixMode(GL_PROJECTION)
        gl.glLoadMatrixf(np.ascontiguousarray(projection.T, dtype=np.float32).ctypes.data_as(ctypes.POINTER(ctypes.c_float)))
        gl.glMatrixMode(GL_MODELVIEW)
        gl.glLoadMatrixf(np.ascontiguousarray(modelview.T, dtype=np.float32).ctypes.data_as(ctypes.POINTER(ctypes.c_float)))

        stride = 6 * ctypes.sizeof(ctypes.c_float)
        gl.glBindBuffer(GL_ARRAY_BUFFER, self._vbo.value)
        gl.glEnableClientState(GL_VERTEX_ARRAY)
        gl.glEnableClientState(GL_COLOR_ARRAY)
        gl.glVertexPointer(3, GL_FLOAT, stride, ctypes.c_void_p(0))
        gl.glColorPointer(3, GL_FLOAT, stride, ctypes.c_void_p(3 * ctypes.sizeof(ctypes.c_float)))
        if self.solid and batch.triangle_indices.size:
            gl.glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, self._tri_ebo.value)
            gl.glDrawElements(GL_TRIANGLES, int(batch.triangle_indices.size), GL_UNSIGNED_INT, ctypes.c_void_p(0))
        if batch.line_indices.size:
            gl.glLineWidth(1.0)
            gl.glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, self._line_ebo.value)
            gl.glDrawElements(GL_LINES, int(batch.line_indices.size), GL_UNSIGNED_INT, ctypes.c_void_p(0))
        gl.glDisableClientState(GL_COLOR_ARRAY)
        gl.glDisableClientState(GL_VERTEX_ARRAY)
        gdi32.SwapBuffers(self._hdc)
        self.last_render_ms = (time.perf_counter() - t0) * 1000.0
        self._update_overlay(record, overlay, batch)

    def _update_overlay(self, record, overlay: str, batch: GpuBatch) -> None:
        lines = [
            record.label,
            f"offset=0x{record.raw.get('offset', 0):04x}  size={record.raw.get('size', 0)}  source={record.path.name}",
            f"mode={'solid' if self.solid else 'wire'} backend={self.render_backend} projection={'perspective' if self.perspective else 'ortho'}  "
            f"tri={batch.triangle_count} line={batch.line_count} groups={len(batch.group_matrices)} colors={batch.color_count}  "
            f"shade={self.shade_mode} cull={self.cull_mode} zoom={self.zoom:.2f} render={self.last_render_ms:.1f}ms",
        ]
        if record.raw.get("composite_mode"):
            sources = ", ".join(f"#{int(i):03d}" for i in record.raw.get("source_records", []))
            if sources:
                lines.append(f"assembled from {sources}")
        if overlay:
            lines.append(overlay)
        help_line = "Mouse: rotate, wheel: zoom, middle/right drag: pan | []: frame | F: anim | T: assembled/raw | W/P/G/D/L/S/C | 1/2/3 | O/E"
        self.viewport.set_overlay("\n".join(lines), help_line)
