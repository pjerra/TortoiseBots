#!/usr/bin/env python3
"""Generate Forever map art and item icons from your local WoW client.

Reads extracted WDC5 .db2 files (UiMap, UiMapXMapArt, UiMapArt,
UiMapArtStyleLayer, UiMapArtTile, WorldMapOverlay, WorldMapOverlayTile, plus
ItemAppearance / ItemModifiedAppearance when --icons-from-db2 is used) and
fetches the referenced BLP textures by FileDataID from the client's CASC
storage using CascLib.

Client artwork is never shipped in the repository or dashboard binaries; every
operator generates it locally once into `--out` (default: forever-art/).

Output layout:
  <out>/maps/<uiMapId>.webp   Base-layer (LayerIndex 0) + WorldMapOverlay
                              composite, cropped to LayerWidth x LayerHeight.
  <out>/maps/index.json       {"<uiMapId>": {"w": px, "h": px}, ...}
  <out>/icons/<fileDataId>.png

Requires: Pillow (with BLP support) and g++ + zlib headers to build the tiny
CascLib C wrapper on first run (or pass --casc-lib /path/to/libcasc_wrap.so).

Manual CascLib shared-library build (optional):
  g++ -shared -fPIC -O2 -DCASC_USE_SYSTEM_ZLIB -DCASCLIB_NO_AUTO_LINK_LIBRARY \\
      -DCASCLIB_NODEBUG -Idep/CascLib/src dep/CascLib/src/*.cpp \\
      dep/CascLib/src/common/*.cpp dep/CascLib/src/hashes/*.cpp \\
      dep/CascLib/src/jenkins/lookup3.c dep/CascLib/src/overwatch/*.cpp \\
      casc_wrap.cpp -lz -o libcasc_wrap.so

Usage:
  python3 -I gen_forever_art.py \\
      --client "/path/to/World of Warcraft" \\
      --dbc-dir "/path/to/data/dbc/enUS" \\
      --items-file "/path/to/logs/tortoisebots-items.json" \\
      [--out /path/to/forever-art] [--only maps|icons] [--icons-from-db2] \\
      [--dry-run] [--force]
"""

import argparse
import ctypes
import io
import json
import os
import struct
import subprocess
import sys
import tempfile
import time

try:
    from PIL import Image
except ImportError as exc:  # pragma: no cover - tooling guard
    sys.exit(f"missing dependency: {exc}. Install Pillow (e.g. python3 -m pip install Pillow)")

WEBP_QUALITY = 90
CASC_LOCALE_ENUS = 0x00000002

CASC_WRAP_CPP = r"""
#include "CascLib.h"
#include <cstdlib>

extern "C" {

void* casc_open(const char* data_path, const char* product, unsigned int locale_mask) {
    CASC_OPEN_STORAGE_ARGS args = {};
    args.Size = sizeof(CASC_OPEN_STORAGE_ARGS);
    args.szLocalPath = data_path;
    args.szCodeName = product;
    args.dwLocaleMask = locale_mask;
    HANDLE hStorage = nullptr;
    if (!CascOpenStorageEx(nullptr, &args, false, &hStorage)) {
        return nullptr;
    }
    return hStorage;
}

unsigned int casc_last_error() {
    return GetCascError();
}

void casc_close(void* storage) {
    if (storage) {
        CascCloseStorage(storage);
    }
}

int casc_read_file_id(void* storage, unsigned int file_data_id, unsigned int locale_mask,
                      unsigned char** out_buf, unsigned int* out_size) {
    if (!storage || !out_buf || !out_size) return 0;
    *out_buf = nullptr;
    *out_size = 0;

    HANDLE hFile = nullptr;
    if (!CascOpenFile(storage, CASC_FILE_DATA_ID(file_data_id), locale_mask, CASC_OPEN_BY_FILEID, &hFile)) {
        return 0;
    }

    ULONGLONG fileSize64 = 0;
    if (!CascGetFileSize64(hFile, &fileSize64) || fileSize64 == 0 || fileSize64 > 0x10000000ULL) {
        CascCloseFile(hFile);
        return 0;
    }

    unsigned int totalSize = static_cast<unsigned int>(fileSize64);
    unsigned char* buf = static_cast<unsigned char*>(std::malloc(totalSize));
    if (!buf) {
        CascCloseFile(hFile);
        return 0;
    }

    unsigned int offset = 0;
    while (offset < totalSize) {
        DWORD bytesRead = 0;
        if (!CascReadFile(hFile, buf + offset, totalSize - offset, &bytesRead)) {
            std::free(buf);
            CascCloseFile(hFile);
            return 0;
        }
        if (bytesRead == 0) break;
        offset += bytesRead;
    }

    CascCloseFile(hFile);
    if (offset != totalSize) {
        std::free(buf);
        return 0;
    }

    *out_buf = buf;
    *out_size = totalSize;
    return 1;
}

void casc_free(void* ptr) {
    std::free(ptr);
}

}
"""


class DB2Reader:
    """Minimal WDC5 .db2 reader for the map and item appearance tables."""

    def __init__(self, path):
        with open(path, "rb") as handle:
            self.raw = handle.read()
        magic, version = struct.unpack_from("<4sI", self.raw, 0)
        if magic != b"WDC5" or version != 5:
            raise ValueError(f"{path}: expected WDC5 v5, got {magic!r} v{version}")

        (self.record_count, self.field_count, self.record_size, self.string_table_size,
         self.table_hash, self.layout_hash, self.min_id, self.max_id, self.locale,
         self.flags, self.index_field, self.total_field_count, self.packed_data_offset,
         self.parent_lookup_count, self.column_meta_size, self.common_data_size,
         self.pallet_data_size, self.section_count) = struct.unpack_from("<IIIIIIIIIHhIIIIIII", self.raw, 136)

        self.sparse = bool(self.flags & 0x1)
        pos = 204
        self.sections = []
        for _ in range(self.section_count):
            self.sections.append(struct.unpack_from("<QIIIIIIII", self.raw, pos))
            pos += 40

        pos += 4 * self.field_count  # DB2FieldEntry[]

        self.col_meta = []
        if not self.sparse and self.column_meta_size:
            for _ in range(self.total_field_count):
                self.col_meta.append(struct.unpack_from("<HHIIIII", self.raw, pos))
                pos += 24

        self.pallet = [None] * self.total_field_count
        for i, cm in enumerate(self.col_meta):
            if cm[3] == 3 and cm[2]:
                self.pallet[i] = struct.unpack_from(f"<{cm[2] // 4}I", self.raw, pos)
                pos += cm[2]

        self.pallet_array = [None] * self.total_field_count
        for i, cm in enumerate(self.col_meta):
            if cm[3] == 4 and cm[2]:
                self.pallet_array[i] = struct.unpack_from(f"<{cm[2] // 4}I", self.raw, pos)
                pos += cm[2]

        self.common = [{} for _ in range(self.total_field_count)]
        for i, cm in enumerate(self.col_meta):
            if cm[3] == 2 and cm[2]:
                for _ in range(cm[2] // 8):
                    rid, val = struct.unpack_from("<II", self.raw, pos)
                    self.common[i][rid] = val
                    pos += 8

        self.records = []
        self.sparse_sizes = []
        self.copies = []
        self.parent_ids = {}
        if self.sparse:
            for sec in self.sections:
                tact, _file_off, _rec_cnt, _str_sz, cat_off, _id_sz, _par_sz, cat_cnt, copy_cnt = sec
                if tact != 0 or cat_cnt == 0 or cat_off == 0:
                    continue
                p = cat_off
                cat_ids = struct.unpack_from(f"<{cat_cnt}I", self.raw, p)
                p += 4 * cat_cnt
                for _ in range(copy_cnt):
                    self.copies.append(struct.unpack_from("<II", self.raw, p))
                    p += 8
                for r in range(cat_cnt):
                    foff, rsz = struct.unpack_from("<IH", self.raw, p)
                    p += 6
                    self.records.append((foff, cat_ids[r]))
                    self.sparse_sizes.append(rsz)
            return

        global_rec_idx = 0
        for sec in self.sections:
            _tact, file_offset, rec_count, str_size, _cat_off, id_tbl_sz, parent_sz, _cat_cnt, copy_cnt = sec
            p = file_offset
            rec_data_start = p
            p += rec_count * self.record_size
            p += str_size
            if id_tbl_sz:
                ids = list(struct.unpack_from(f"<{id_tbl_sz // 4}I", self.raw, p))
                p += id_tbl_sz
            else:
                ids = [None] * rec_count
            if copy_cnt:
                p += copy_cnt * 8
            if self.parent_lookup_count and parent_sz:
                num_entries, _min_pid, _max_pid = struct.unpack_from("<III", self.raw, p)
                p += 12
                for _ in range(num_entries):
                    pid, ridx = struct.unpack_from("<II", self.raw, p)
                    self.parent_ids[global_rec_idx + ridx] = pid
                    p += 8
            for r in range(rec_count):
                self.records.append((rec_data_start + r * self.record_size, ids[r]))
            global_rec_idx += rec_count

    def _packed(self, rec_off, byte_off, bit_off, bit_width):
        p = rec_off + byte_off
        chunk = self.raw[p:p + 8]
        if len(chunk) < 8:
            chunk = chunk + b"\x00" * (8 - len(chunk))
        val = struct.unpack("<Q", chunk)[0]
        return (val >> (bit_off & 7)) & ((1 << bit_width) - 1)

    def get_int(self, rec_idx, field, signed=False):
        rec_off, rid = self.records[rec_idx]
        bo, bs, _ads, ct, d0, d1, _d2 = self.col_meta[field]
        if ct == 0:  # None
            off = rec_off + bo // 8
            fmt = {8: ("<b" if signed else "<B"),
                   16: ("<h" if signed else "<H"),
                   32: ("<i" if signed else "<I")}.get(bs)
            if fmt is None:
                raise ValueError(f"unsupported bit size {bs}")
            return struct.unpack_from(fmt, self.raw, off)[0]
        if ct == 1:  # Immediate
            return self._packed(rec_off, d0 // 8 + self.packed_data_offset, d0, d1)
        if ct == 5:  # SignedImmediate
            val = self._packed(rec_off, d0 // 8 + self.packed_data_offset, d0, d1)
            mask = 1 << (d1 - 1)
            return (val ^ mask) - mask
        if ct == 2:  # CommonData
            if rid is None:
                rid = self.get_id(rec_idx)
            val = self.common[field].get(rid, d0)
            if signed and (val & 0x80000000):
                val -= 0x100000000
            return val
        if ct == 3:  # Pallet
            pidx = self._packed(rec_off, d0 // 8 + self.packed_data_offset, d0, d1)
            val = self.pallet[field][pidx]
            if signed and (val & 0x80000000):
                val -= 0x100000000
            return val
        raise ValueError(f"unsupported column compression {ct}")

    def get_id(self, rec_idx):
        _, rid = self.records[rec_idx]
        if rid is not None:
            return rid
        return self.get_int(rec_idx, self.index_field, signed=False)

    def get_str(self, rec_idx, field):
        rec_off, _ = self.records[rec_idx]
        field_off = rec_off + self.col_meta[field][0] // 8
        str_off = self.get_int(rec_idx, field, signed=False)
        if str_off == 0:
            return ""
        start = field_off + str_off
        end = self.raw.index(b"\x00", start)
        return self.raw[start:end].decode("utf-8", "replace")

    def get_float(self, rec_idx, field):
        rec_off, rid = self.records[rec_idx]
        bo, _bs, _ads, ct, d0, d1, _d2 = self.col_meta[field]
        if ct == 0:
            return struct.unpack_from("<f", self.raw, rec_off + bo // 8)[0]
        if ct == 1:
            val = self._packed(rec_off, d0 // 8 + self.packed_data_offset, d0, d1)
        elif ct == 2:
            if rid is None:
                rid = self.get_id(rec_idx)
            val = self.common[field].get(rid, d0)
        elif ct == 3:
            pidx = self._packed(rec_off, d0 // 8 + self.packed_data_offset, d0, d1)
            val = self.pallet[field][pidx]
        else:
            raise ValueError(f"unsupported float column compression {ct}")
        return struct.unpack("<f", struct.pack("<I", val))[0]

    def get_int_array(self, rec_idx, field):
        rec_off, _ = self.records[rec_idx]
        _bo, _bs, _ads, ct, d0, d1, d2 = self.col_meta[field]
        if ct != 4:
            raise ValueError(f"unsupported array column compression {ct}")
        pidx = self._packed(rec_off, d0 // 8 + self.packed_data_offset, d0, d1)
        start = pidx * d2
        return self.pallet_array[field][start:start + d2]

    def get_sparse_strings(self, rec_idx, count):
        rec_off, _ = self.records[rec_idx]
        rec_size = self.sparse_sizes[rec_idx]
        parts = self.raw[rec_off:rec_off + rec_size].split(b"\x00", count)
        out = [p.decode("utf-8", "replace") for p in parts[:count]]
        while len(out) < count:
            out.append("")
        return out


def find_casc_src_dir(start_dir):
    cur = os.path.abspath(start_dir)
    for _ in range(10):
        candidate = os.path.join(cur, "dep", "CascLib", "src")
        if os.path.isfile(os.path.join(candidate, "CascLib.h")):
            return candidate
        parent = os.path.dirname(cur)
        if parent == cur:
            break
        cur = parent
    return None


def build_casc_wrapper(casc_src_dir, out_so):
    srcs = [
        "common/Common.cpp", "common/Directory.cpp", "common/Csv.cpp",
        "common/FileStream.cpp", "common/FileTree.cpp", "common/ListFile.cpp",
        "common/Mime.cpp", "common/RootHandler.cpp", "common/Sockets.cpp",
        "hashes/md5.cpp", "hashes/sha1.cpp", "jenkins/lookup3.c",
        "overwatch/apm.cpp", "overwatch/cmf.cpp", "overwatch/aes.cpp",
        "CascDecompress.cpp", "CascDecrypt.cpp", "CascDumpData.cpp",
        "CascFiles.cpp", "CascFindFile.cpp", "CascIndexFiles.cpp",
        "CascOpenFile.cpp", "CascOpenStorage.cpp", "CascReadFile.cpp",
        "CascRootFile_Diablo3.cpp", "CascRootFile_Install.cpp", "CascRootFile_MNDX.cpp",
        "CascRootFile_Text.cpp", "CascRootFile_TVFS.cpp", "CascRootFile_OW.cpp",
        "CascRootFile_WoW.cpp",
    ]
    with tempfile.TemporaryDirectory() as tmpdir:
        wrap_cpp = os.path.join(tmpdir, "casc_wrap.cpp")
        with open(wrap_cpp, "w") as handle:
            handle.write(CASC_WRAP_CPP)
        cmd = [
            "g++", "-shared", "-fPIC", "-O2",
            "-DCASC_USE_SYSTEM_ZLIB", "-DCASCLIB_NO_AUTO_LINK_LIBRARY", "-DCASCLIB_NODEBUG",
            f"-I{casc_src_dir}",
            *[os.path.join(casc_src_dir, s) for s in srcs],
            wrap_cpp, "-lz", "-o", out_so,
        ]
        subprocess.run(cmd, check=True)


class CascStorage:
    """Thin ctypes wrapper around CascLib for FileDataID reads."""

    def __init__(self, lib_path, client_dir, product="wowf", locale_mask=CASC_LOCALE_ENUS):
        self.lib = ctypes.CDLL(lib_path)
        self.lib.casc_open.argtypes = [ctypes.c_char_p, ctypes.c_char_p, ctypes.c_uint]
        self.lib.casc_open.restype = ctypes.c_void_p
        self.lib.casc_last_error.restype = ctypes.c_uint
        self.lib.casc_close.argtypes = [ctypes.c_void_p]
        self.lib.casc_read_file_id.argtypes = [
            ctypes.c_void_p, ctypes.c_uint, ctypes.c_uint,
            ctypes.POINTER(ctypes.POINTER(ctypes.c_ubyte)), ctypes.POINTER(ctypes.c_uint),
        ]
        self.lib.casc_read_file_id.restype = ctypes.c_int
        self.lib.casc_free.argtypes = [ctypes.c_void_p]

        self.locale_mask = locale_mask
        self.handle = self.lib.casc_open(
            client_dir.encode("utf-8"), product.encode("utf-8"), locale_mask
        )
        if not self.handle:
            err = self.lib.casc_last_error()
            raise RuntimeError(f"CascOpenStorageEx failed for {client_dir!r} (product={product}, err={err})")

    def read_file_id(self, file_data_id):
        buf = ctypes.POINTER(ctypes.c_ubyte)()
        size = ctypes.c_uint()
        ok = self.lib.casc_read_file_id(self.handle, file_data_id, self.locale_mask, ctypes.byref(buf), ctypes.byref(size))
        if not ok:
            return None
        try:
            return ctypes.string_at(buf, size.value)
        finally:
            self.lib.casc_free(buf)

    def close(self):
        if self.handle:
            self.lib.casc_close(self.handle)
            self.handle = None


def load_map_definitions(dbc_dir):
    """Read UiMap* DB2s and return list of maps with base-layer tile specs."""
    uimap_db = DB2Reader(os.path.join(dbc_dir, "UiMap.db2"))
    xart_db = DB2Reader(os.path.join(dbc_dir, "UiMapXMapArt.db2"))
    art_db = DB2Reader(os.path.join(dbc_dir, "UiMapArt.db2"))
    style_db = DB2Reader(os.path.join(dbc_dir, "UiMapArtStyleLayer.db2"))
    tile_db = DB2Reader(os.path.join(dbc_dir, "UiMapArtTile.db2"))

    uimaps = {}
    for i in range(len(uimap_db.records)):
        mid = uimap_db.get_id(i)
        uimaps[mid] = {
            "id": mid,
            "name": uimap_db.get_str(i, 0),
            "system": uimap_db.get_int(i, 4),
            "type": uimap_db.get_int(i, 5),
        }

    # Prefer PhaseID == 0 art if multiple UiMapXMapArt rows exist per UiMapID.
    uimap_to_art = {}
    for i in range(len(xart_db.records)):
        uimap_id = xart_db.parent_ids.get(i)
        phase_id = xart_db.get_int(i, 0, signed=True)
        art_id = xart_db.get_int(i, 1)
        if uimap_id is None or art_id <= 0:
            continue
        if uimap_id not in uimap_to_art or phase_id == 0:
            uimap_to_art[uimap_id] = art_id

    art_to_style = {}
    for i in range(len(art_db.records)):
        art_to_style[art_db.get_id(i)] = art_db.get_int(i, 2)

    style_layer0 = {}
    for i in range(len(style_db.records)):
        style_id = style_db.parent_ids.get(i)
        layer_idx = style_db.get_int(i, 0)
        if style_id is not None and layer_idx == 0:
            style_layer0[style_id] = (
                style_db.get_int(i, 1),  # LayerWidth
                style_db.get_int(i, 2),  # LayerHeight
                style_db.get_int(i, 3),  # TileWidth
                style_db.get_int(i, 4),  # TileHeight
            )

    art_tiles = {}
    for i in range(len(tile_db.records)):
        art_id = tile_db.parent_ids.get(i)
        if art_id is None or tile_db.get_int(i, 2) != 0:
            continue
        row = tile_db.get_int(i, 0)
        col = tile_db.get_int(i, 1)
        fdid = tile_db.get_int(i, 3)
        if fdid > 0:
            art_tiles.setdefault(art_id, []).append((row, col, fdid))

    overlay_db = DB2Reader(os.path.join(dbc_dir, "WorldMapOverlay.db2"))
    overlay_tile_db = DB2Reader(os.path.join(dbc_dir, "WorldMapOverlayTile.db2"))

    overlay_tiles = {}
    for i in range(len(overlay_tile_db.records)):
        oid = overlay_tile_db.parent_ids.get(i)
        if oid is None:
            continue
        row = overlay_tile_db.get_int(i, 0)
        col = overlay_tile_db.get_int(i, 1)
        layer_idx = overlay_tile_db.get_int(i, 2)
        fdid = overlay_tile_db.get_int(i, 3, signed=True)
        if fdid > 0:
            overlay_tiles.setdefault(oid, []).append((layer_idx, row, col, fdid))

    art_overlays = {}
    for i in range(len(overlay_db.records)):
        oid = overlay_db.get_id(i)
        art_id = overlay_db.get_int(i, 1)
        tiles = overlay_tiles.get(oid)
        if not tiles:
            continue
        art_overlays.setdefault(art_id, []).append({
            "id": oid,
            "offset_x": overlay_db.get_int(i, 4, signed=True),
            "offset_y": overlay_db.get_int(i, 5, signed=True),
            "tiles": sorted(tiles),
        })

    specs = []
    for uimap_id, art_id in sorted(uimap_to_art.items()):
        style_id = art_to_style.get(art_id, 1)
        layer = style_layer0.get(style_id)
        tiles = art_tiles.get(art_id, [])
        if not layer or not tiles:
            continue
        specs.append({
            "id": uimap_id,
            "info": uimaps.get(uimap_id, {"name": str(uimap_id), "system": -1, "type": -1}),
            "width": layer[0],
            "height": layer[1],
            "tile_w": layer[2],
            "tile_h": layer[3],
            "tiles": tiles,
            "overlays": art_overlays.get(art_id, []),
        })
    return specs


def generate_maps(casc, dbc_dir, out_dir, dry_run=False, force=False):
    maps_dir = os.path.join(out_dir, "maps")
    index_path = os.path.join(maps_dir, "index.json")
    specs = load_map_definitions(dbc_dir)

    index = {}
    if os.path.isfile(index_path):
        try:
            with open(index_path) as handle:
                index = json.load(handle)
        except Exception:
            index = {}

    if not dry_run:
        os.makedirs(maps_dir, exist_ok=True)

    written = skipped = failed = 0
    for spec in specs:
        mid = spec["id"]
        out_path = os.path.join(maps_dir, f"{mid}.webp")
        index[str(mid)] = {"w": spec["width"], "h": spec["height"]}

        if not force and os.path.isfile(out_path):
            skipped += 1
            continue

        if dry_run:
            written += 1
            continue

        max_row = max(r for r, _c, _f in spec["tiles"])
        max_col = max(c for _r, c, _f in spec["tiles"])
        canvas_w = max(spec["width"], (max_col + 1) * spec["tile_w"])
        canvas_h = max(spec["height"], (max_row + 1) * spec["tile_h"])
        grid = Image.new("RGBA", (canvas_w, canvas_h), (0, 0, 0, 255))

        tiles_ok = 0
        for row, col, fdid in spec["tiles"]:
            raw = casc.read_file_id(fdid)
            if raw is None:
                continue
            try:
                tile = Image.open(io.BytesIO(raw)).convert("RGBA")
                grid.alpha_composite(tile, (col * spec["tile_w"], row * spec["tile_h"]))
                tiles_ok += 1
            except Exception as exc:
                print(f"  map {mid} tile ({row},{col}) fdid={fdid}: {exc}")

        if tiles_ok == 0:
            failed += 1
            continue

        for ov in spec.get("overlays", []):
            ox, oy = ov["offset_x"], ov["offset_y"]
            for _layer_idx, row, col, fdid in ov["tiles"]:
                raw = casc.read_file_id(fdid)
                if raw is None:
                    continue
                try:
                    tile = Image.open(io.BytesIO(raw)).convert("RGBA")
                    grid.alpha_composite(tile, (ox + col * spec["tile_w"], oy + row * spec["tile_h"]))
                except Exception as exc:
                    print(f"  map {mid} overlay {ov['id']} tile ({row},{col}) fdid={fdid}: {exc}")

        cropped = grid.crop((0, 0, spec["width"], spec["height"])).convert("RGB")
        cropped.save(out_path, "WEBP", quality=WEBP_QUALITY, method=6)
        written += 1

    if not dry_run:
        with open(index_path, "w") as handle:
            json.dump(index, handle, sort_keys=True, separators=(",", ":"))

    return len(specs), written, skipped, failed


def collect_icon_ids(items_file, dbc_dir, from_db2, spells_file=None, names_file=None, include_all=False):
    icon_ids = set()
    item_ids_needing_db2 = set()

    if items_file and os.path.isfile(items_file):
        with open(items_file) as handle:
            items = json.load(handle).get("items", [])
        for it in items:
            icon = it.get("icon", 0)
            if icon and icon > 0:
                icon_ids.add(int(icon))
            elif from_db2:
                item_ids_needing_db2.add(int(it["id"]))

    if from_db2:
        app_db = DB2Reader(os.path.join(dbc_dir, "ItemAppearance.db2"))
        app_to_icon = {}
        for i in range(len(app_db.records)):
            fdid = app_db.get_int(i, 2, signed=True)
            if fdid > 0:
                app_to_icon[app_db.get_id(i)] = fdid

        if item_ids_needing_db2:
            mod_db = DB2Reader(os.path.join(dbc_dir, "ItemModifiedAppearance.db2"))
            for i in range(len(mod_db.records)):
                item_id = mod_db.get_int(i, 1)
                if item_id in item_ids_needing_db2:
                    app_id = mod_db.get_int(i, 3)
                    fdid = app_to_icon.get(app_id)
                    if fdid:
                        icon_ids.add(fdid)
        elif not icon_ids:
            icon_ids.update(app_to_icon.values())

    if include_all:
        if spells_file and os.path.isfile(spells_file):
            with open(spells_file) as handle:
                for sp in json.load(handle).get("spells", []):
                    icon = sp.get("icon", 0)
                    if icon and icon > 0:
                        icon_ids.add(int(icon))

        if names_file and os.path.isfile(names_file):
            with open(names_file) as handle:
                skills = json.load(handle).get("skills", {})
            for sk in skills.values():
                icon = sk.get("icon", 0)
                if icon and icon > 0:
                    icon_ids.add(int(icon))

        tt_path = os.path.join(dbc_dir, "TalentTab.db2")
        if os.path.isfile(tt_path):
            tt_db = DB2Reader(tt_path)
            for i in range(len(tt_db.records)):
                icon = tt_db.get_int(i, 7, signed=True)
                if icon > 0:
                    icon_ids.add(icon)

    return sorted(icon_ids)


def generate_icons(casc, items_file, dbc_dir, out_dir, from_db2=False, dry_run=False, force=False,
                   spells_file=None, names_file=None, include_all=False):
    icons_dir = os.path.join(out_dir, "icons")
    icon_ids = collect_icon_ids(
        items_file, dbc_dir, from_db2,
        spells_file=spells_file, names_file=names_file, include_all=include_all,
    )
    if not dry_run:
        os.makedirs(icons_dir, exist_ok=True)

    written = skipped = failed = 0
    for fdid in icon_ids:
        out_path = os.path.join(icons_dir, f"{fdid}.png")
        if not force and os.path.isfile(out_path):
            skipped += 1
            continue
        if dry_run:
            written += 1
            continue
        raw = casc.read_file_id(fdid)
        if raw is None:
            failed += 1
            continue
        try:
            img = Image.open(io.BytesIO(raw)).convert("RGBA")
            img.save(out_path, "PNG")
            written += 1
        except Exception:
            failed += 1

    return len(icon_ids), written, skipped, failed


def generate_spells_text(dbc_dir, spells_file, out_dir, dry_run=False):
    if not spells_file or not os.path.isfile(spells_file):
        raise FileNotFoundError(f"spells file not found: {spells_file}")

    with open(spells_file) as handle:
        wanted_ids = {int(s["id"]) for s in json.load(handle).get("spells", [])}

    spell_db = DB2Reader(os.path.join(dbc_dir, "Spell.db2"))
    spell_texts = {}
    for i in range(len(spell_db.records)):
        sid = spell_db.get_id(i)
        subtext, desc, aura = spell_db.get_sparse_strings(i, 3)
        spell_texts[sid] = (subtext, desc, aura)
    for new_id, src_id in spell_db.copies:
        if new_id not in spell_texts and src_id in spell_texts:
            spell_texts[new_id] = spell_texts[src_id]

    rad_db = DB2Reader(os.path.join(dbc_dir, "SpellRadius.db2"))
    radii = {}
    for i in range(len(rad_db.records)):
        radii[rad_db.get_id(i)] = rad_db.get_float(i, 0)

    eff_db = DB2Reader(os.path.join(dbc_dir, "SpellEffect.db2"))
    effects_by_spell = {}
    for i in range(len(eff_db.records)):
        sid = eff_db.parent_ids.get(i)
        if sid not in wanted_ids:
            continue
        if eff_db.get_int(i, 1, signed=True) != 0:  # DifficultyID == 0
            continue
        eidx = eff_db.get_int(i, 2, signed=True)
        aperiod = eff_db.get_int(i, 6, signed=True)
        bp = eff_db.get_float(i, 22)
        ridxs = eff_db.get_int_array(i, 26)
        r_id = ridxs[0] if ridxs[0] else (ridxs[1] if len(ridxs) > 1 else 0)
        rad = radii.get(r_id, 0.0) if r_id else 0.0
        rad_val = int(rad) if rad == int(rad) else round(rad, 2)
        effects_by_spell.setdefault(sid, []).append((eidx, int(round(bp)), aperiod, rad_val))

    out = {}
    for sid in sorted(wanted_ids):
        subtext, desc, aura = spell_texts.get(sid, ("", "", ""))
        effs = sorted(effects_by_spell.get(sid, []), key=lambda x: x[0])
        out[str(sid)] = {
            "subtext": subtext,
            "desc": desc,
            "aura": aura,
            "values": [e[1] for e in effs],
            "periods": [e[2] for e in effs],
            "radius": [e[3] for e in effs],
        }

    out_path = os.path.join(out_dir, "spells-text.json")
    if not dry_run:
        os.makedirs(out_dir, exist_ok=True)
        with open(out_path, "w") as handle:
            json.dump(out, handle, separators=(",", ":"))

    return len(out), out_path


def generate_talent_tabs(dbc_dir, out_dir, dry_run=False):
    tt_db = DB2Reader(os.path.join(dbc_dir, "TalentTab.db2"))
    rows = []
    for i in range(len(tt_db.records)):
        tid = tt_db.get_id(i)
        rows.append((tid, {
            "name": tt_db.get_str(i, 0),
            "class_mask": tt_db.get_int(i, 5, signed=True),
            "order": tt_db.get_int(i, 3, signed=True),
            "icon": tt_db.get_int(i, 7, signed=True),
            "background": tt_db.get_str(i, 1),
        }))
    rows.sort(key=lambda x: x[0])
    out = {str(tid): obj for tid, obj in rows}

    out_path = os.path.join(out_dir, "talent-tabs.json")
    if not dry_run:
        os.makedirs(out_dir, exist_ok=True)
        with open(out_path, "w") as handle:
            json.dump(out, handle, separators=(",", ":"))

    return len(out), out_path


def default_forever_local_dir():
    here = os.path.dirname(os.path.abspath(__file__))
    return os.path.abspath(os.path.join(here, "..", "..", "..", "..", "..", "..", ".."))


def main():
    base = default_forever_local_dir()
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--client", default=os.environ.get("WOW_CLIENT"),
                        help="WoW Forever client root (containing .build.info and Data/); env WOW_CLIENT")
    parser.add_argument("--dbc-dir", default=os.path.join(base, "data", "dbc", "enUS"),
                        help="directory containing extracted .db2 files")
    parser.add_argument("--items-file", default=os.path.join(base, "logs", "tortoisebots-items.json"),
                        help="tortoisebots-items.json emitted by the module")
    parser.add_argument("--spells-file", default=os.path.join(base, "logs", "tortoisebots-spells.json"),
                        help="tortoisebots-spells.json emitted by the module")
    parser.add_argument("--names-file", default=os.path.join(base, "logs", "tortoisebots-names.json"),
                        help="tortoisebots-names.json emitted by the module")
    parser.add_argument("--out", default=os.path.join(base, "data", "forever-art"),
                        help="output directory (default: forever-local/data/forever-art)")
    parser.add_argument("--only", choices=["maps", "icons", "spells", "talents", "icons-all"],
                        help="extract only one target (default: all)")
    parser.add_argument("--icons-from-db2", action="store_true",
                        help="resolve item icons from ItemModifiedAppearance + ItemAppearance.db2 when items lack 'icon'")
    parser.add_argument("--casc-lib", help="path to prebuilt libcasc_wrap.so (auto-built if omitted)")
    parser.add_argument("--product", default="wowf", help="CASC product code (default: wowf)")
    parser.add_argument("--dry-run", action="store_true", help="report work without writing files")
    parser.add_argument("--force", action="store_true", help="overwrite existing output files")
    args = parser.parse_args()

    run_maps = args.only in (None, "maps")
    run_icons = args.only in (None, "icons", "icons-all")
    run_spells = args.only in (None, "spells")
    run_talents = args.only in (None, "talents")
    needs_casc = (run_maps or run_icons) and not args.dry_run

    if needs_casc and not args.client:
        parser.error("--client is required when extracting maps or icons (or set WOW_CLIENT)")

    t0 = time.monotonic()
    casc = None
    if needs_casc:
        casc_lib = args.casc_lib
        if not casc_lib:
            cached_so = os.path.join(tempfile.gettempdir(), "tortoisebots_libcasc_wrap.so")
            if not os.path.isfile(cached_so):
                casc_src = find_casc_src_dir(os.path.dirname(os.path.abspath(__file__)))
                if not casc_src:
                    sys.exit("could not locate dep/CascLib/src; pass --casc-lib")
                print(f"Building CascLib wrapper -> {cached_so}")
                build_casc_wrapper(casc_src, cached_so)
            casc_lib = cached_so
        casc = CascStorage(casc_lib, args.client, product=args.product)

    try:
        if run_maps:
            total, written, skipped, failed = generate_maps(
                casc, args.dbc_dir, args.out, dry_run=args.dry_run, force=args.force
            )
            print(f"maps:  {total} total, {written} written, {skipped} skipped, {failed} failed")

        if run_icons:
            total, written, skipped, failed = generate_icons(
                casc, args.items_file, args.dbc_dir, args.out,
                from_db2=args.icons_from_db2, dry_run=args.dry_run, force=args.force,
                spells_file=args.spells_file, names_file=args.names_file,
                include_all=(args.only in (None, "icons-all")),
            )
            print(f"icons: {total} total, {written} written, {skipped} skipped, {failed} failed")

        if run_spells:
            count, path = generate_spells_text(
                args.dbc_dir, args.spells_file, args.out, dry_run=args.dry_run
            )
            print(f"spells: {count} written -> {path}")

        if run_talents:
            count, path = generate_talent_tabs(
                args.dbc_dir, args.out, dry_run=args.dry_run
            )
            print(f"talents: {count} written -> {path}")
    finally:
        if casc is not None:
            casc.close()

    print(f"done in {time.monotonic() - t0:.2f}s -> {args.out}")


if __name__ == "__main__":
    main()
