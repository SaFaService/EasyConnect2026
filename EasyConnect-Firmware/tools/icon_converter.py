#!/usr/bin/env python3
"""
icon_converter.py
-----------------
Wizard per la conversione di icone in C-array LVGL v8
per il progetto EasyConnect Display (ESP32-S3, LVGL 8.x).

Sorgenti supportate:
  - File locale (PNG / JPG / ICO / SVG)
  - Cartella locale  (ricerca anche ricorsiva)
  - URL diretto      (qualsiasi http/https → PNG / JPG / SVG)
  - Libreria online  (Lucide · Font Awesome Free · Material Design Icons)

Output generato per ogni icona:
  - <nome>.h   →  extern const lv_img_dsc_t <nome>;
  - <nome>.cpp →  dati pixel RGB565 (+ alpha) + lv_img_dsc_t

Uso base (wizard interattivo):
    python tools/icon_converter.py

Uso con argomenti CLI:
    python tools/icon_converter.py path/to/icons/
    python tools/icon_converter.py path/to/icona.png
    python tools/icon_converter.py https://example.com/icon.svg
    python tools/icon_converter.py --library lucide  --icons home,wifi,settings
    python tools/icon_converter.py --library fa      --icons house,wifi,gear --fa-style solid
    python tools/icon_converter.py --library mdi     --icons home,wifi,cog
    python tools/icon_converter.py path/to/icons/ --out-h include/icons --out-cpp src/ui/icons
    python tools/icon_converter.py path/to/icons/ --size 100x100 --alpha --no-index

    Tutti gli argomenti CLI sono opzionali: quelli non forniti vengono chiesti
    interattivamente con i default del progetto EasyConnect pre-compilati.

Dipendenze:
    pip install Pillow
    pip install numpy        (opzionale, molto più veloce)
    pip install requests     (opzionale, download più robusto rispetto a urllib)

Supporto SVG (scegli una delle due opzioni):
    Opzione A – PyMuPDF  (consigliata su Windows: bundled, nessuna DLL esterna)
        pip install PyMuPDF

    Opzione B – cairosvg  (qualità ottima, richiede la Cairo DLL su Windows)
        1. Scarica e installa GTK for Windows Runtime:
           https://github.com/tschoonj/GTK-for-Windows-Runtime-Environment-Installer/releases
        2. pip install cairosvg
        (cairosvg ha la precedenza su PyMuPDF se entrambi sono installati)
"""

import argparse
import io
import re
import sys
import urllib.error
import urllib.request
from pathlib import Path

# ─── dependency check ────────────────────────────────────────────────────────

try:
    from PIL import Image
except ImportError:
    print("\n[ERRORE] Libreria Pillow non trovata.")
    print("         Installa con:  pip install Pillow\n")
    sys.exit(1)

try:
    import numpy as np
    HAS_NUMPY = True
except ImportError:
    HAS_NUMPY = False

# cairosvg: qualità migliore ma richiede la Cairo DLL su Windows.
# L'ImportError cattura il modulo mancante; OSError cattura la DLL mancante.
try:
    import cairosvg
    HAS_CAIROSVG = True
except (ImportError, OSError):
    HAS_CAIROSVG = False

# PyMuPDF (fitz): renderer SVG tramite MuPDF bundled.
# Nessuna DLL esterna richiesta su Windows. pip install PyMuPDF
try:
    import fitz  # PyMuPDF
    HAS_PYMUPDF = True
except ImportError:
    HAS_PYMUPDF = False

HAS_SVG = HAS_CAIROSVG or HAS_PYMUPDF

try:
    import requests as _requests
    HAS_REQUESTS = True
except ImportError:
    HAS_REQUESTS = False

# ─── project paths ────────────────────────────────────────────────────────────
# Lo script è in <progetto>/tools/ → la radice del progetto è un livello sopra.

_SCRIPT_DIR   = Path(__file__).resolve().parent
_PROJECT_ROOT = _SCRIPT_DIR.parent

DEFAULT_OUT_H   = _PROJECT_ROOT / "include" / "icons"
DEFAULT_OUT_CPP = _PROJECT_ROOT / "src" / "ui" / "icons"

# ─── constants ────────────────────────────────────────────────────────────────

SUPPORTED = {".png", ".jpg", ".jpeg", ".ico", ".svg"}

# Librerie icone online integrate
ICON_LIBRARIES = {
    "lucide": {
        "label":       "Lucide Icons  (lucide.dev)",
        "url_pattern": "https://unpkg.com/lucide-static@latest/icons/{name}.svg",
        "note":        "Nomi es.: home, wifi, settings, power, bell, search, ...",
        "site":        "https://lucide.dev/icons/",
        "has_styles":  False,
    },
    "fa": {
        "label":       "Font Awesome Free  (fontawesome.com)",
        "url_pattern": "https://raw.githubusercontent.com/FortAwesome/Font-Awesome/6.x/svgs/{style}/{name}.svg",
        "note":        "Nomi es.: house, wifi, gear, power-off, bell, magnifying-glass, ...",
        "site":        "https://fontawesome.com/icons?o=r&m=free",
        "has_styles":  True,
        "styles":      ["solid", "regular", "brands"],
        "default_style": "solid",
    },
    "mdi": {
        "label":       "Material Design Icons  (materialdesignicons.com)",
        "url_pattern": "https://raw.githubusercontent.com/Templarian/MaterialDesign/master/svg/{name}.svg",
        "note":        "Nomi es.: home, wifi, cog, power, bell, magnify, ...",
        "site":        "https://pictogrammers.com/library/mdi/",
        "has_styles":  False,
    },
}

BANNER = r"""
 ╔═══════════════════════════════════════════════════════════════╗
 ║        EasyConnect  –  Icon → LVGL C-Array Converter          ║
 ║  Sorgenti: locale · URL · Lucide · FontAwesome · MDI          ║
 ║  Output  : lv_img_dsc_t  (LVGL 8.x / RGB565)                  ║
 ╚═══════════════════════════════════════════════════════════════╝
"""

# ─── CLI argument parsing ─────────────────────────────────────────────────────

def parse_args() -> argparse.Namespace:
    p = argparse.ArgumentParser(
        description="Converte icone in C-array LVGL v8 per EasyConnect Display.",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=(
            "Esempi:\n"
            "  python icon_converter.py\n"
            "  python icon_converter.py assets/icons/\n"
            "  python icon_converter.py assets/icons/fan.png\n"
            "  python icon_converter.py https://example.com/icon.svg\n"
            "  python icon_converter.py --library lucide --icons home,wifi,settings\n"
            "  python icon_converter.py --library fa --icons house,wifi,gear --fa-style solid\n"
            "  python icon_converter.py --library mdi --icons home,wifi,cog\n"
            "  python icon_converter.py assets/icons/ --size 64x64\n"
            "  python icon_converter.py assets/icons/ --out-h include/icons --out-cpp src/ui/icons\n"
        ),
    )
    p.add_argument(
        "src",
        nargs="?",
        default=None,
        help="File/cartella locale, oppure URL diretto http/https. "
             "Ometti per wizard interattivo.",
    )
    p.add_argument(
        "--library", "-l",
        dest="library",
        choices=list(ICON_LIBRARIES.keys()),
        default=None,
        metavar="LIBRERIA",
        help="Libreria icone online: lucide | fa | mdi",
    )
    p.add_argument(
        "--icons", "-i",
        dest="icons",
        default=None,
        metavar="NOMI",
        help="Nomi icone separati da virgola (usato con --library). "
             "Es.: home,wifi,settings",
    )
    p.add_argument(
        "--fa-style",
        dest="fa_style",
        choices=["solid", "regular", "brands"],
        default="solid",
        help="Stile Font Awesome (default: solid).",
    )
    p.add_argument(
        "--out-h", "-H",
        dest="out_h",
        default=None,
        metavar="PERCORSO",
        help=f"Cartella output header (.h). Default: {DEFAULT_OUT_H}",
    )
    p.add_argument(
        "--out-cpp", "-C",
        dest="out_cpp",
        default=None,
        metavar="PERCORSO",
        help=f"Cartella output sorgenti (.cpp). Default: {DEFAULT_OUT_CPP}",
    )
    p.add_argument(
        "--out", "-o",
        dest="out_single",
        default=None,
        metavar="PERCORSO",
        help="Cartella unica per header e sorgenti (sovrascrive --out-h e --out-cpp).",
    )
    p.add_argument(
        "--size", "-s",
        dest="size",
        default=None,
        metavar="WxH",
        help="Ridimensiona le icone (es. 100x100). Per SVG la dimensione è passata "
             "direttamente al renderer vettoriale.",
    )
    p.add_argument(
        "--alpha", "-a",
        action="store_true",
        default=False,
        help="Forza canale alpha su tutti i file (inclusi JPG).",
    )
    p.add_argument(
        "--no-index",
        action="store_true",
        default=False,
        help="Non generare icons_index.h.",
    )
    p.add_argument(
        "--no-recursive",
        action="store_true",
        default=False,
        help="Non cercare nelle sottocartelle (solo se src è una cartella).",
    )
    return p.parse_args()


def parse_size(size_str: str) -> tuple | None:
    m = re.fullmatch(r"(\d+)[xX×](\d+)", size_str.strip())
    if not m:
        return None
    return int(m.group(1)), int(m.group(2))


# ─── wizard helpers ───────────────────────────────────────────────────────────

def ask(prompt: str, default: str = "") -> str:
    suffix = f" [{default}]" if default else ""
    while True:
        val = input(f"  {prompt}{suffix}: ").strip()
        if val == "" and default != "":
            return default
        if val:
            return val
        print("    → Il valore non può essere vuoto.")


def ask_int(prompt: str, default: int, lo: int = 1, hi: int = 4096) -> int:
    while True:
        raw = ask(prompt, str(default))
        try:
            n = int(raw)
            if lo <= n <= hi:
                return n
            print(f"    → Valore fuori range ({lo}–{hi}).")
        except ValueError:
            print("    → Inserisci un numero intero.")


def ask_bool(prompt: str, default: bool = True) -> bool:
    d = "s" if default else "n"
    while True:
        raw = ask(f"{prompt} (s/n)", d).lower()
        if raw in ("s", "si", "sì", "y", "yes"):
            return True
        if raw in ("n", "no"):
            return False
        print("    → Risposta non valida (s / n).")


def ask_choice(prompt: str, choices: list[tuple[str, str]]) -> str:
    """
    choices: lista di (chiave, etichetta).
    Ritorna la chiave scelta.
    """
    print(f"  {prompt}")
    for i, (key, label) in enumerate(choices, 1):
        print(f"    {i}) {label}")
    while True:
        raw = input("  Scelta: ").strip()
        if raw.isdigit():
            idx = int(raw) - 1
            if 0 <= idx < len(choices):
                return choices[idx][0]
        print(f"    → Inserisci un numero tra 1 e {len(choices)}.")


# ─── name sanitization ───────────────────────────────────────────────────────

def sanitize_varname(filename: str) -> str:
    stem = Path(filename).stem
    stem = re.sub(r"[^a-zA-Z0-9]", "_", stem)
    stem = re.sub(r"_+", "_", stem).strip("_")
    if not stem or stem[0].isdigit():
        stem = "icon_" + stem
    return stem.lower()


# ─── URL / library download ──────────────────────────────────────────────────

def _is_url(s: str) -> bool:
    return s.startswith("http://") or s.startswith("https://")


def download_bytes(url: str) -> bytes:
    """Scarica un URL e restituisce i byte. Usa requests se disponibile."""
    if HAS_REQUESTS:
        resp = _requests.get(url, timeout=15, headers={"User-Agent": "icon_converter/1.0"})
        if resp.status_code == 404:
            raise FileNotFoundError(f"404 – icona non trovata: {url}")
        resp.raise_for_status()
        return resp.content
    else:
        req = urllib.request.Request(url, headers={"User-Agent": "icon_converter/1.0"})
        try:
            with urllib.request.urlopen(req, timeout=15) as r:
                return r.read()
        except urllib.error.HTTPError as e:
            if e.code == 404:
                raise FileNotFoundError(f"404 – icona non trovata: {url}")
            raise


def _guess_ext_from_url(url: str) -> str:
    """Estrae l'estensione dal percorso URL, default .svg."""
    path = url.split("?")[0].split("#")[0]
    suffix = Path(path).suffix.lower()
    return suffix if suffix in SUPPORTED else ".svg"


def build_library_urls(lib_key: str, icon_names: list[str], fa_style: str = "solid") -> list[tuple[str, str]]:
    """
    Ritorna lista di (url, filename) per ogni nome icona richiesta.
    """
    lib = ICON_LIBRARIES[lib_key]
    result = []
    for name in icon_names:
        name = name.strip()
        if not name:
            continue
        if lib["has_styles"]:
            url = lib["url_pattern"].format(name=name, style=fa_style)
            filename = f"{name}_{fa_style}.svg"
        else:
            url = lib["url_pattern"].format(name=name)
            filename = f"{name}.svg"
        result.append((url, filename))
    return result


def download_sources_to_dir(
    sources: list[tuple[str, str]],
    dest_dir: Path,
) -> list[Path]:
    """
    Scarica ogni (url, filename) in dest_dir.
    Ritorna solo i file scaricati con successo.
    """
    result = []
    for url, filename in sources:
        dest = dest_dir / filename
        try:
            data = download_bytes(url)
            dest.write_bytes(data)
            result.append(dest)
            print(f"  [DL  ] {filename}")
        except FileNotFoundError as e:
            print(f"  [ERR ] {e}")
        except Exception as e:
            print(f"  [ERR ] {filename}: {e}")
    return result


# ─── SVG rendering ───────────────────────────────────────────────────────────

_SVG_NO_RENDERER_MSG = (
    "Nessun renderer SVG disponibile.\n"
    "  Opzione A (consigliata su Windows, nessuna DLL):\n"
    "      pip install PyMuPDF\n"
    "  Opzione B (qualità ottima, richiede DLL Cairo su Windows):\n"
    "      https://github.com/tschoonj/GTK-for-Windows-Runtime-Environment-Installer/releases\n"
    "      pip install cairosvg"
)


def _render_svg(source, target_size: tuple | None) -> Image.Image:
    """
    Rasterizza un SVG in un'immagine PIL RGBA.
    source: Path (file locale) oppure bytes (da memoria/URL).
    Usa cairosvg se disponibile, altrimenti PyMuPDF.
    """
    if not HAS_SVG:
        raise RuntimeError(_SVG_NO_RENDERER_MSG)

    if HAS_CAIROSVG:
        kwargs: dict = {}
        if target_size:
            kwargs["output_width"]  = target_size[0]
            kwargs["output_height"] = target_size[1]
        if isinstance(source, Path):
            png_bytes = cairosvg.svg2png(url=str(source.resolve()), **kwargs)
        else:
            png_bytes = cairosvg.svg2png(bytestring=source, **kwargs)
        return Image.open(io.BytesIO(png_bytes)).convert("RGBA")

    # ── fallback: PyMuPDF ────────────────────────────────────────────────────
    if isinstance(source, Path):
        doc = fitz.open(str(source))
    else:
        doc = fitz.open(stream=source, filetype="svg")

    page = doc[0]
    if target_size:
        rect = page.rect
        sx = target_size[0] / rect.width  if rect.width  > 0 else 1.0
        sy = target_size[1] / rect.height if rect.height > 0 else 1.0
        mat = fitz.Matrix(sx, sy)
        pix = page.get_pixmap(matrix=mat, alpha=True)
    else:
        pix = page.get_pixmap(alpha=True)

    return Image.frombytes("RGBA", [pix.width, pix.height], pix.samples)


# ─── image loading ───────────────────────────────────────────────────────────

def open_image(path: Path, target_size: tuple | None) -> Image.Image:
    """
    Apre un'immagine locale supportata.
    - ICO : Pillow sceglie la variante alla risoluzione più alta.
    - SVG : rasterizzato tramite _render_svg (cairosvg → svglib → errore).
    """
    if path.suffix.lower() == ".svg":
        return _render_svg(path, target_size)

    img = Image.open(str(path))
    if path.suffix.lower() == ".ico":
        img = img.convert("RGBA")
    if target_size:
        img = img.resize(target_size, Image.LANCZOS)
    return img


def open_image_from_bytes(data: bytes, filename: str, target_size: tuple | None) -> Image.Image:
    """
    Come open_image, ma opera su bytes in memoria (usato per URL diretti).
    """
    suffix = Path(filename).suffix.lower()
    if suffix == ".svg":
        return _render_svg(data, target_size)

    img = Image.open(io.BytesIO(data))
    if suffix == ".ico":
        img = img.convert("RGBA")
    if target_size:
        img = img.resize(target_size, Image.LANCZOS)
    return img


def _has_transparency(img: Image.Image) -> bool:
    return img.mode in ("RGBA", "LA") or (
        img.mode == "P" and "transparency" in img.info
    )


# ─── pixel conversion ────────────────────────────────────────────────────────

def _rgb_to_565(r: int, g: int, b: int) -> int:
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)


def image_to_lvgl_bytes(
    img: Image.Image, force_alpha: bool, is_ico_or_svg: bool
) -> tuple[list[int], bool]:
    has_alpha = force_alpha or is_ico_or_svg or _has_transparency(img)
    if HAS_NUMPY:
        return _convert_numpy(img, has_alpha)
    return _convert_pil(img, has_alpha)


def _convert_numpy(img: Image.Image, has_alpha: bool) -> tuple[list[int], bool]:
    if has_alpha:
        arr = np.array(img.convert("RGBA"), dtype=np.uint16)
        r, g, b, a = arr[:, :, 0], arr[:, :, 1], arr[:, :, 2], arr[:, :, 3]
        rgb565 = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)
        out = np.stack(
            [(rgb565 & 0xFF).astype(np.uint8),
             ((rgb565 >> 8) & 0xFF).astype(np.uint8),
             a.astype(np.uint8)],
            axis=2,
        ).flatten()
    else:
        arr = np.array(img.convert("RGB"), dtype=np.uint16)
        r, g, b = arr[:, :, 0], arr[:, :, 1], arr[:, :, 2]
        rgb565 = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)
        out = np.stack(
            [(rgb565 & 0xFF).astype(np.uint8),
             ((rgb565 >> 8) & 0xFF).astype(np.uint8)],
            axis=2,
        ).flatten()
    return out.tolist(), has_alpha


def _convert_pil(img: Image.Image, has_alpha: bool) -> tuple[list[int], bool]:
    if has_alpha:
        pixels = list(img.convert("RGBA").getdata())
        out = []
        for r, g, b, a in pixels:
            c = _rgb_to_565(r, g, b)
            out += [c & 0xFF, (c >> 8) & 0xFF, a]
    else:
        pixels = list(img.convert("RGB").getdata())
        out = []
        for r, g, b in pixels:
            c = _rgb_to_565(r, g, b)
            out += [c & 0xFF, (c >> 8) & 0xFF]
    return out, has_alpha


# ─── C code generation ───────────────────────────────────────────────────────

def _format_bytes(data: list[int]) -> str:
    lines = []
    for i in range(0, len(data), 16):
        lines.append("    " + ", ".join(f"0x{b:02X}" for b in data[i : i + 16]) + ",")
    return "\n".join(lines)


def generate_header(var: str, w: int, h: int, src_name: str) -> str:
    return (
        f"#pragma once\n"
        f"// Auto-generated by icon_converter.py – source: {src_name}\n"
        f'#include "lvgl.h"\n\n'
        f"extern const lv_img_dsc_t {var};\n"
    )


def generate_source(
    var: str, w: int, h: int, data: list[int], has_alpha: bool, src_name: str
) -> str:
    cf = "LV_IMG_CF_TRUE_COLOR_ALPHA" if has_alpha else "LV_IMG_CF_TRUE_COLOR"
    px_size = 3 if has_alpha else 2
    data_size = w * h * px_size
    return (
        f'#include "{var}.h"\n\n'
        f"// Auto-generated by icon_converter.py – source: {src_name}\n"
        f"// Dimensions: {w}x{h}  Format: {cf}  Data size: {data_size} bytes\n\n"
        f"static const uint8_t _{var}_data[] = {{\n"
        f"{_format_bytes(data)}\n"
        f"}};\n\n"
        f"const lv_img_dsc_t {var} = {{\n"
        f"    .header = {{\n"
        f"        .cf          = {cf},\n"
        f"        .always_zero = 0,\n"
        f"        .reserved    = 0,\n"
        f"        .w           = {w},\n"
        f"        .h           = {h},\n"
        f"    }},\n"
        f"    .data_size = {data_size},\n"
        f"    .data      = _{var}_data,\n"
        f"}};\n"
    )


def generate_index(entries: list[tuple[str, int, int]]) -> str:
    lines = [
        "#pragma once",
        "// Auto-generated icon index.",
        "// Includi questo file per accedere a tutte le icone convertite.",
        '#include "lvgl.h"',
        "",
    ]
    for var, w, h in sorted(entries, key=lambda e: e[0]):
        comment = f"// {w}x{h}" if w and h else ""
        lines.append(f'#include "{var}.h"  {comment}')
    return "\n".join(lines) + "\n"


# ─── file processing ─────────────────────────────────────────────────────────

def collect_images(src: Path, recursive: bool) -> list[Path]:
    if src.is_file():
        if src.suffix.lower() not in SUPPORTED:
            print(f"  [ERR ] Formato non supportato: {src.suffix}")
            sys.exit(1)
        return [src]
    if recursive:
        files = [p for p in src.rglob("*") if p.suffix.lower() in SUPPORTED and p.is_file()]
    else:
        files = [p for p in src.iterdir() if p.is_file() and p.suffix.lower() in SUPPORTED]
    return sorted(files)


def process_file(
    src: Path,
    out_h_dir: Path,
    out_cpp_dir: Path,
    target_size: tuple | None,
    force_alpha: bool,
) -> tuple[str, str, tuple | None]:
    var      = sanitize_varname(src.name)
    h_file   = out_h_dir   / f"{var}.h"
    cpp_file = out_cpp_dir / f"{var}.cpp"

    if h_file.exists() and cpp_file.exists():
        try:
            content = cpp_file.read_text(encoding="utf-8")
            wm = re.search(r"\.w\s*=\s*(\d+)", content)
            hm = re.search(r"\.h\s*=\s*(\d+)", content)
            w  = int(wm.group(1)) if wm else 0
            h  = int(hm.group(1)) if hm else 0
        except Exception:
            w = h = 0
        return "skip", f"{src.name} → già presente ({var})", (var, w, h)

    try:
        is_transparent = src.suffix.lower() in (".ico", ".svg")
        img  = open_image(src, target_size)
        data, has_alpha = image_to_lvgl_bytes(img, force_alpha, is_transparent)
        w, h = img.width, img.height

        h_file.write_text(generate_header(var, w, h, src.name),               encoding="utf-8")
        cpp_file.write_text(generate_source(var, w, h, data, has_alpha, src.name), encoding="utf-8")

        mode_str = "ALPHA" if has_alpha else "RGB565"
        return "ok", f"{src.name} → {var} ({w}×{h}, {mode_str}, {len(data)} B)", (var, w, h)
    except Exception as exc:
        return "error", f"{src.name} → ERRORE: {exc}", None


def process_url(
    url: str,
    out_h_dir: Path,
    out_cpp_dir: Path,
    target_size: tuple | None,
    force_alpha: bool,
) -> tuple[str, str, tuple | None]:
    """Converte direttamente da un URL senza scrivere file temporanei."""
    filename = Path(url.split("?")[0]).name or "icon.svg"
    var      = sanitize_varname(filename)
    h_file   = out_h_dir   / f"{var}.h"
    cpp_file = out_cpp_dir / f"{var}.cpp"

    if h_file.exists() and cpp_file.exists():
        try:
            content = cpp_file.read_text(encoding="utf-8")
            wm = re.search(r"\.w\s*=\s*(\d+)", content)
            hm = re.search(r"\.h\s*=\s*(\d+)", content)
            w  = int(wm.group(1)) if wm else 0
            h  = int(hm.group(1)) if hm else 0
        except Exception:
            w = h = 0
        return "skip", f"{filename} → già presente ({var})", (var, w, h)

    try:
        data_bytes = download_bytes(url)
        suffix = _guess_ext_from_url(url)
        is_transparent = suffix in (".ico", ".svg")
        img  = open_image_from_bytes(data_bytes, filename, target_size)
        data, has_alpha = image_to_lvgl_bytes(img, force_alpha, is_transparent)
        w, h = img.width, img.height

        h_file.write_text(generate_header(var, w, h, url),                        encoding="utf-8")
        cpp_file.write_text(generate_source(var, w, h, data, has_alpha, url), encoding="utf-8")

        mode_str = "ALPHA" if has_alpha else "RGB565"
        return "ok", f"{filename} → {var} ({w}×{h}, {mode_str}, {len(data)} B)", (var, w, h)
    except FileNotFoundError as exc:
        return "error", str(exc), None
    except Exception as exc:
        return "error", f"{filename} → ERRORE: {exc}", None


# ─── run conversion ───────────────────────────────────────────────────────────

def run_conversion(
    images: list[Path],
    urls: list[str],
    out_h_dir: Path,
    out_cpp_dir: Path,
    target_size: tuple | None,
    force_alpha: bool,
    gen_index: bool,
) -> None:
    out_h_dir.mkdir(parents=True, exist_ok=True)
    out_cpp_dir.mkdir(parents=True, exist_ok=True)

    print("\n── Conversione ──────────────────────────────────────────────────────")
    counts: dict[str, int] = {"ok": 0, "skip": 0, "error": 0}
    errors: list[str]      = []
    index_entries: list[tuple[str, int, int]] = []

    for src in images:
        status, msg, entry = process_file(
            src, out_h_dir, out_cpp_dir, target_size, force_alpha
        )
        tag = {"ok": "[OK  ]", "skip": "[SKIP]", "error": "[ERR ]"}[status]
        print(f"  {tag} {msg}")
        counts[status] += 1
        if status == "error":
            errors.append(msg)
        if entry:
            index_entries.append(entry)

    for url in urls:
        status, msg, entry = process_url(
            url, out_h_dir, out_cpp_dir, target_size, force_alpha
        )
        tag = {"ok": "[OK  ]", "skip": "[SKIP]", "error": "[ERR ]"}[status]
        print(f"  {tag} {msg}")
        counts[status] += 1
        if status == "error":
            errors.append(msg)
        if entry:
            index_entries.append(entry)

    if gen_index and index_entries:
        index_path = out_h_dir / "icons_index.h"
        index_path.write_text(generate_index(index_entries), encoding="utf-8")
        print(f"\n  [OK  ] icons_index.h → {index_path.resolve()}")

    print("\n── Riepilogo ────────────────────────────────────────────────────────")
    print(f"  Convertite : {counts['ok']}")
    print(f"  Saltate    : {counts['skip']}  (già presenti)")
    print(f"  Errori     : {counts['error']}")
    if errors:
        print("  Dettaglio errori:")
        for e in errors:
            print(f"    • {e}")
    print()
    print(f"  Header   → {out_h_dir.resolve()}")
    if out_cpp_dir != out_h_dir:
        print(f"  Sorgenti → {out_cpp_dir.resolve()}")
    if counts["ok"] > 0:
        print()
        print("  Prossimi passi in PlatformIO:")
        print("  1. Aggiungi i .cpp di src/ui/icons/ al build_src_filter (platformio.ini)")
        print("  2. Verifica che include/icons/ sia raggiungibile da -I include")
        print('  3. Nel codice C++: #include "icons_index.h"')
        print("  4. Usa l'icona:    lv_img_set_src(obj, &nome_icona);")
    print()


# ─── wizard ───────────────────────────────────────────────────────────────────

def wizard(args: argparse.Namespace) -> None:
    print(BANNER)
    accel = "(numpy)" if HAS_NUMPY else "(PIL puro – installa numpy per velocità)"
    if HAS_CAIROSVG:
        svg_status = "cairosvg  (qualità ottima)"
    elif HAS_PYMUPDF:
        svg_status = "PyMuPDF/MuPDF  (qualità ottima, nessuna DLL esterna)"
    else:
        svg_status = "NON DISPONIBILE  →  pip install PyMuPDF"
    dl_status = "requests" if HAS_REQUESTS else "urllib (installa requests per download migliori)"
    print(f"  Conversione : {accel}")
    print(f"  Renderer SVG: {svg_status}")
    print(f"  Download    : {dl_status}\n")

    images: list[Path] = []
    urls:   list[str]  = []

    # ─────────────────────────────────────────────────────────────────────────
    # PASSO 1 – Sorgente
    # ─────────────────────────────────────────────────────────────────────────

    # Determina la modalità sorgente
    if args.library is not None:
        src_mode = "library"
    elif args.src is not None and _is_url(args.src):
        src_mode = "url"
    elif args.src is not None:
        src_mode = "local"
    else:
        # Nessun argomento CLI → chiedi all'utente
        print("── Passo 1: Sorgente ───────────────────────────────────────────────")
        src_mode = ask_choice(
            "Cosa vuoi convertire?",
            [
                ("local",   "File o cartella locale  (PNG / JPG / ICO / SVG)"),
                ("url",     "URL diretto             (http/https → una sola immagine)"),
                ("library", "Libreria icone online   (Lucide · FontAwesome · MDI)"),
            ],
        )
        print()

    # ── Modalità: locale ──────────────────────────────────────────────────────
    if src_mode == "local":
        if args.src is not None:
            src_path = Path(args.src)
            if not src_path.exists():
                print(f"  [ERRORE] Percorso non trovato: {src_path}")
                sys.exit(1)
            print(f"── Sorgente locale: {src_path.resolve()}")
        else:
            print("   Inserisci il percorso di una cartella oppure di un singolo file.")
            while True:
                raw      = ask("File o cartella (PNG / JPG / ICO / SVG)")
                src_path = Path(raw)
                if src_path.exists():
                    break
                print(f"    → Percorso non trovato: {src_path}")

        recursive = not args.no_recursive
        if src_path.is_dir() and not args.no_recursive and args.src is None:
            recursive = ask_bool("Cercare anche nelle sottocartelle?", True)

        images = collect_images(src_path, recursive)
        if not images:
            print(f"\n  [ATTENZIONE] Nessuna immagine trovata in {src_path}.")
            sys.exit(0)
        label = src_path.name if src_path.is_file() else f"{len(images)} immagini"
        print(f"  → {label}")

    # ── Modalità: URL diretto ─────────────────────────────────────────────────
    elif src_mode == "url":
        if args.src is not None:
            raw_url = args.src
            print(f"── URL diretto: {raw_url}")
        else:
            raw_url = ask("URL dell'immagine (PNG / JPG / SVG)")
        if not _is_url(raw_url):
            print("  [ERRORE] L'URL deve iniziare con http:// o https://")
            sys.exit(1)
        ext = _guess_ext_from_url(raw_url)
        if ext == ".svg" and not HAS_SVG:
            print("  [ERRORE] L'URL punta a un SVG ma nessun renderer SVG è installato.")
            print("           Soluzione rapida: pip install svglib reportlab")
            sys.exit(1)
        urls = [raw_url]
        print(f"  → 1 URL ({ext})")

    # ── Modalità: libreria online ─────────────────────────────────────────────
    elif src_mode == "library":
        if not HAS_SVG:
            print("  [ERRORE] Le librerie online forniscono SVG ma nessun renderer SVG è installato.")
            print("           Soluzione rapida: pip install svglib reportlab")
            sys.exit(1)

        # Seleziona libreria
        if args.library is not None:
            lib_key = args.library
            print(f"── Libreria: {ICON_LIBRARIES[lib_key]['label']}")
        else:
            lib_key = ask_choice(
                "Seleziona la libreria:",
                [(k, v["label"]) for k, v in ICON_LIBRARIES.items()],
            )
        lib = ICON_LIBRARIES[lib_key]
        print(f"  → Sito di riferimento: {lib['site']}")
        print(f"  → {lib['note']}")

        # Stile FontAwesome
        fa_style = args.fa_style
        if lib["has_styles"] and args.library is None:
            fa_style = ask_choice(
                "Stile Font Awesome:",
                [(s, s) for s in lib["styles"]],
            )

        # Nomi icone
        if args.icons is not None:
            icon_names = [n.strip() for n in args.icons.split(",") if n.strip()]
            print(f"  → {len(icon_names)} icone da CLI: {', '.join(icon_names)}")
        else:
            print("\n  Inserisci i nomi delle icone separati da virgola.")
            print(f"  ({lib['note']})")
            raw_names = ask("Nomi icone")
            icon_names = [n.strip() for n in raw_names.split(",") if n.strip()]

        if not icon_names:
            print("  [ATTENZIONE] Nessun nome icona fornito.")
            sys.exit(0)

        sources = build_library_urls(lib_key, icon_names, fa_style)
        urls    = [u for u, _ in sources]
        print(f"  → {len(urls)} icone da scaricare")

    # ─────────────────────────────────────────────────────────────────────────
    # PASSO 2 – Cartelle di output
    # ─────────────────────────────────────────────────────────────────────────
    if args.out_single is not None:
        out_h_dir = out_cpp_dir = Path(args.out_single)
        print(f"\n── Output: {out_h_dir.resolve()}")
    elif args.out_h is not None or args.out_cpp is not None:
        out_h_dir   = Path(args.out_h)   if args.out_h   else DEFAULT_OUT_H
        out_cpp_dir = Path(args.out_cpp) if args.out_cpp else DEFAULT_OUT_CPP
        print(f"\n── Output header  : {out_h_dir.resolve()}")
        print(f"── Output sorgenti: {out_cpp_dir.resolve()}")
    else:
        print(f"\n── Passo 2: Cartella di output ─────────────────────────────────────")
        print(f"   Default progetto → header  : {DEFAULT_OUT_H}")
        print(f"                      sorgenti: {DEFAULT_OUT_CPP}")
        split = ask_bool("Separare header (.h) da sorgenti (.cpp)?", True)
        if split:
            out_h_dir   = Path(ask("Cartella header   (.h)",   str(DEFAULT_OUT_H)))
            out_cpp_dir = Path(ask("Cartella sorgenti (.cpp)", str(DEFAULT_OUT_CPP)))
        else:
            out_h_dir = out_cpp_dir = Path(ask("Cartella di output unica", str(DEFAULT_OUT_CPP)))

    # ─────────────────────────────────────────────────────────────────────────
    # PASSO 3 – Dimensioni
    # ─────────────────────────────────────────────────────────────────────────
    target_size: tuple | None = None
    if args.size is not None:
        target_size = parse_size(args.size)
        if target_size is None:
            print(f"  [ERRORE] Formato --size non valido: '{args.size}' (es.: 100x100)")
            sys.exit(1)
        print(f"\n── Dimensioni: {target_size[0]}×{target_size[1]} px (da CLI)")
    else:
        print("\n── Passo 3: Dimensioni ─────────────────────────────────────────────")
        if ask_bool("Ridimensionare le icone a una dimensione fissa?", False):
            tw = ask_int("Larghezza target (pixel)", 100)
            th = ask_int("Altezza target   (pixel)", 100)
            target_size = (tw, th)
            print(f"    → {tw}×{th} px")
        else:
            print("    → Dimensioni originali mantenute")

    # ─────────────────────────────────────────────────────────────────────────
    # PASSO 4 – Canale alpha
    # ─────────────────────────────────────────────────────────────────────────
    if args.alpha:
        force_alpha = True
        print("\n── Alpha: forzata su tutti i file (da CLI)")
    else:
        print("\n── Passo 4: Canale alpha ───────────────────────────────────────────")
        print("   PNG, ICO e SVG mantengono il proprio alpha automaticamente.")
        force_alpha = ask_bool(
            "Forzare alpha anche su JPG (aggiunge 1 byte/pixel)?", False
        )

    # ─────────────────────────────────────────────────────────────────────────
    # PASSO 5 – File indice
    # ─────────────────────────────────────────────────────────────────────────
    if args.no_index:
        gen_index = False
        print("\n── Indice: disabilitato (da CLI --no-index)")
    else:
        print("\n── Passo 5: File indice ────────────────────────────────────────────")
        gen_index = ask_bool("Generare 'icons_index.h' con tutti gli #include?", True)

    # ─────────────────────────────────────────────────────────────────────────
    # CONVERSIONE
    # ─────────────────────────────────────────────────────────────────────────
    run_conversion(
        images, urls,
        out_h_dir, out_cpp_dir,
        target_size, force_alpha, gen_index,
    )


# ─── entry point ─────────────────────────────────────────────────────────────

if __name__ == "__main__":
    wizard(parse_args())
