"""Offline SEQ checker: compares every active plugin's SEQ file against what xEdit's
"Create SEQ File" would generate, without launching the game.

Usage:
    python seqcheck.py --mo2 "<MO2 base folder>" --profile "<profile>" --game-data "<game>/Data" [-v]
    python seqcheck.py --data "<game>/Data" --plugins "<path to plugins.txt>" [-v]

Exit code is 1 when any SEQ file is stale or missing. SEQ files inside BSAs are read too.

Rule (mirrors xEdit's GenerateSEQFileForFile): a plugin's SEQ lists, as file-relative
FormIDs, every QUST record with the Start Game Enabled flag that is either new in the plugin
or sets SGE on a master quest that did not have it.
"""
import argparse, os, struct, sys, zlib

BETHESDA = {"skyrim.esm", "update.esm", "dawnguard.esm", "hearthfires.esm", "dragonborn.esm"}


def subrecords(data):
    i, big = 0, None
    while i + 6 <= len(data):
        t = data[i:i + 4]
        sz = struct.unpack_from("<H", data, i + 4)[0]
        i += 6
        if t == b"XXXX":
            big = struct.unpack_from("<I", data, i)[0]
            i += sz
            continue
        if big is not None:
            sz, big = big, None
        yield t, data[i:i + sz]
        i += sz


class Plugin:
    def __init__(self, path, want_dialogue):
        self.masters, self.quests, self.dialogue_owners, self.light = [], {}, set(), False
        with open(path, "rb") as f:
            hdr = f.read(24)
            if hdr[:4] != b"TES4":
                raise ValueError("not a plugin")
            size, flags = struct.unpack_from("<II", hdr, 4)
            self.light = bool(flags & 0x200)
            for t, d in subrecords(f.read(size)):
                if t == b"MAST":
                    self.masters.append(d.rstrip(b"\0").decode("cp1252"))
            end = os.fstat(f.fileno()).st_size
            pos = 24 + size
            while pos + 24 <= end:
                f.seek(pos)
                g = f.read(24)
                gsize = struct.unpack_from("<I", g, 4)[0]
                label = g[8:12]
                if label == b"QUST" or (want_dialogue and label == b"DIAL"):
                    self._read_group(f, pos + 24, pos + gsize)
                pos += gsize

    def _read_group(self, f, pos, end):
        while pos + 24 <= end:
            f.seek(pos)
            h = f.read(24)
            t = h[:4]
            size, rflags, fid = struct.unpack_from("<III", h, 4)
            if t == b"GRUP":
                pos += size  # nested groups (topic children etc.) are not needed
                continue
            if t in (b"QUST", b"DIAL"):
                data = f.read(size)
                if rflags & 0x00040000:
                    data = zlib.decompress(data[4:])
                if t == b"QUST":
                    edid, qflags = "", None
                    for st, sd in subrecords(data):
                        if st == b"EDID":
                            edid = sd.rstrip(b"\0").decode("cp1252", "replace")
                        elif st == b"DNAM" and qflags is None:
                            qflags = struct.unpack_from("<H", sd, 0)[0]
                    if qflags is not None:
                        self.quests[self.fix(fid)] = (edid, qflags)
                else:
                    for st, sd in subrecords(data):
                        if st == b"QNAM":
                            self.dialogue_owners.add(self.fix(struct.unpack_from("<I", sd, 0)[0]))
            pos += 24 + size

    def fix(self, fid):
        # xEdit's FixedFormID: an out-of-range file index means "this file"
        n = len(self.masters)
        return fid if (fid >> 24) <= n else (n << 24) | (fid & 0xFFFFFF)


class Checker:
    def __init__(self, resolve):
        self.resolve, self.cache = resolve, {}

    def load(self, name, want_dialogue=False):
        key = (name.lower(), want_dialogue)
        if key not in self.cache:
            path = self.resolve(name)
            self.cache[key] = Plugin(path, want_dialogue) if path else None
        return self.cache[key]

    def expected(self, p):
        out = []
        for fid, (edid, fl) in p.quests.items():
            if not fl & 1:
                continue
            idx = fid >> 24
            if idx < len(p.masters):
                m = self.load(p.masters[idx])
                if m:
                    mq = m.quests.get((len(m.masters) << 24) | (fid & 0xFFFFFF))
                    if mq and mq[1] & 1:
                        continue  # master quest was already SGE
            out.append(fid)
        return out


def lz4_block(src, out):
    i = 0
    while i < len(src):
        token = src[i]; i += 1
        n = token >> 4
        if n == 15:
            while True:
                b = src[i]; i += 1; n += b
                if b != 255:
                    break
        out += src[i:i + n]; i += n
        if i >= len(src):
            break
        offset = src[i] | (src[i + 1] << 8); i += 2
        n = (token & 15) + 4
        if (token & 15) == 15:
            while True:
                b = src[i]; i += 1; n += b
                if b != 255:
                    break
        start = len(out) - offset
        for k in range(n):  # byte by byte: matches may overlap
            out.append(out[start + k])


def lz4_frame(data):
    flg = data[4]
    i = 7 + (8 if flg & 0x08 else 0) + (4 if flg & 0x01 else 0)
    out = bytearray()
    while True:
        size = struct.unpack_from("<I", data, i)[0]; i += 4
        if size == 0:
            return bytes(out)
        block = data[i:i + (size & 0x7FFFFFFF)]; i += size & 0x7FFFFFFF
        if size & 0x80000000:
            out += block
        else:
            lz4_block(block, out)
        if flg & 0x10:
            i += 4


def bsa_seq_files(path):
    """Returns {"name.seq": bytes} for every file in an archive's seq folder."""
    out = {}
    with open(path, "rb") as f:
        hdr = f.read(36)
        if hdr[:4] != b"BSA\0":
            return out
        version, _, aflags, nfolders, nfiles = struct.unpack_from("<5I", hdr, 4)
        rec = 24 if version == 105 else 16
        folders = [struct.unpack_from("<QI", f.read(rec)) for _ in range(nfolders)]
        entries = []  # (folder, size, offset) in file-name order
        for _, count in folders:
            folder = ""
            if aflags & 1:
                n = f.read(1)[0]
                folder = f.read(n).rstrip(b"\0").decode("cp1252").lower()
            for _ in range(count):
                _, size, offset = struct.unpack("<QII", f.read(16))
                entries.append((folder, size, offset))
        names = f.read(struct.unpack_from("<I", hdr, 28)[0]).split(b"\0") if aflags & 2 else []
        for (folder, size, offset), name in zip(entries, names):
            if folder != "seq":
                continue
            f.seek(offset)
            data = f.read(size & 0x3FFFFFFF)
            if aflags & 0x100:  # embedded file names
                data = data[1 + data[0]:]
            if bool(aflags & 4) != bool(size & 0x40000000):
                data = lz4_frame(data[4:]) if version == 105 else zlib.decompress(data[4:])
            out[name.decode("cp1252").lower()] = data
    return out


class FileMap:
    """Resolves plugins and SEQ files the way the game sees them. For SEQ files a copy inside a
    BSA beats a loose file (observed in game), and the BSA of the later-loading plugin wins."""

    def __init__(self, folders):
        self.plugins, self.loose_seq, self.bsa_seq, self.load_index = {}, {}, {}, {}
        for priority, folder in enumerate(folders):
            if not os.path.isdir(folder):
                continue
            for f in os.listdir(folder):
                full, low = os.path.join(folder, f), f.lower()
                if low.endswith((".esp", ".esm", ".esl")):
                    self.plugins[low] = full
                elif low.endswith(".bsa"):
                    owner = os.path.splitext(low)[0].removesuffix(" - textures")
                    try:
                        for name, data in bsa_seq_files(full).items():
                            self.bsa_seq.setdefault(name, []).append((owner, priority, data))
                    except Exception as e:
                        print(f"warning: could not read {full}: {e}")
                elif low == "seq" and os.path.isdir(full):
                    for s in os.listdir(full):
                        self.loose_seq[s.lower()] = os.path.join(full, s)

    def set_load_order(self, plugins):
        self.load_index = {os.path.splitext(p)[0].lower(): i for i, p in enumerate(plugins)}

    def plugin(self, name):
        return self.plugins.get(name.lower())

    def seq(self, name):
        name = name.lower()
        if name in self.bsa_seq:
            return max(self.bsa_seq[name], key=lambda e: (self.load_index.get(e[0], -1), e[1]))[2]
        if name in self.loose_seq:
            return open(self.loose_seq[name], "rb").read()
        return None


def mo2_folders(base, profile, game_data):
    mods = [l[1:].strip() for l in open(os.path.join(base, "profiles", profile, "modlist.txt"), encoding="utf-8")
            if l.startswith("+")]
    # modlist.txt lists the highest priority mod first
    return [game_data] + [os.path.join(base, "mods", m) for m in reversed(mods)] + [os.path.join(base, "overwrite")]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--mo2"); ap.add_argument("--profile"); ap.add_argument("--game-data")
    ap.add_argument("--data"); ap.add_argument("--plugins")
    ap.add_argument("-v", action="store_true", help="list every quest behind each problem")
    a = ap.parse_args()
    if a.mo2:
        if not a.game_data:
            ap.error("--mo2 also needs --game-data (the game's Data folder)")
        files = FileMap(mo2_folders(a.mo2, a.profile, a.game_data))
        plugins_txt = os.path.join(a.mo2, "profiles", a.profile, "plugins.txt")
        ccc = os.path.join(os.path.dirname(a.game_data), "Skyrim.ccc")
    else:
        files, plugins_txt = FileMap([a.data]), a.plugins
        ccc = os.path.join(os.path.dirname(a.data), "Skyrim.ccc")
    skip = set(BETHESDA)
    if os.path.isfile(ccc):
        skip |= {l.strip().lower() for l in open(ccc, encoding="utf-8") if l.strip()}
    active = [l[1:].strip() for l in open(plugins_txt, encoding="utf-8") if l.startswith("*")]
    files.set_load_order(active)

    c = Checker(files.plugin)
    counts = {}
    for name in active:
        if name.lower() in skip:
            continue
        try:
            p = c.load(name, want_dialogue=True)
        except Exception as e:
            print(f"ERROR    {name}: {e}"); counts["ERROR"] = counts.get("ERROR", 0) + 1
            continue
        if not p:
            continue
        exp = c.expected(p)
        raw = files.seq(os.path.splitext(name)[0] + ".seq")
        on_disk = None if raw is None else list(struct.unpack(f"<{len(raw) // 4}I", raw[:len(raw) // 4 * 4]))
        dialogue = [x for x in exp if x in p.dialogue_owners]
        if not exp:
            status = "NOT-NEEDED"
        elif on_disk is None:
            status = "MISSING" if dialogue else "NOT-NEEDED"
        elif set(exp) <= set(on_disk):
            status = "OK"  # extra entries are harmless; the plugin leaves those files alone too
        else:
            status = "STALE"
        counts[status] = counts.get(status, 0) + 1
        if status in ("MISSING", "STALE"):
            print(f"{status:8} {'ESL' if p.light else '   '} {name}  (expected {len(exp)}, on disk "
                  f"{'none' if on_disk is None else len(on_disk)}, dialogue quests {len(dialogue)})")
            if a.v:
                for x in sorted(set(exp) - set(on_disk or [])):
                    print(f"           needs   {x:08X} {p.quests[x][0]}")
                for x in sorted(set(on_disk or []) - set(exp)):
                    print(f"           bad     {x:08X}")
    print("\nSummary:", ", ".join(f"{k} {v}" for k, v in sorted(counts.items())))
    return 1 if counts.get("STALE") or counts.get("MISSING") else 0


if __name__ == "__main__":
    sys.exit(main())
