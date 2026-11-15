"""Adds logged entry names (F1CE_WAD_NAMES=<file>, the probe in runtime/mods.cpp) to tools/wad_names.txt.

  python -I tools/wad_names_merge.py <USRDIR> <logged names.txt>...

Keeps the names that match an entry of an archive in USRDIR and are not known yet; also tries the same folders'
siblings the game did not ask for in that session (the same path in every driver's, team's, car's and track's folder).
"""
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
import wad  # noqa: E402


def main(argv):
    if len(argv) < 2:
        print(__doc__); return 1
    usrdir = Path(argv[0])
    hashes = {}
    for p in sorted(usrdir.glob('*.wad')):
        for e in wad.Wad(p).entries:
            hashes.setdefault(e[0], []).append(p.name)
    known_file = Path(__file__).with_name('wad_names.txt')
    known = {wad.name_hash(n) for n in known_file.read_text(encoding='latin1').splitlines() if n.strip()}
    logged = []
    for f in argv[1:]:
        logged += [n.strip() for n in Path(f).read_text(encoding='latin1').splitlines() if n.strip()]
    # sibling folders: a path under drivers/<id>/, teams/<id>/, vehicles/<id>/ or tracks/<id>/ is tried for every id:
    # those seen in any logged path, the database's folders (entities.bin) and the track archives' names
    pat = re.compile(r'(drivers|teams|vehicles|tracks)/([^/]+)/(.+)$', re.I)
    ids = {}
    for n in logged:
        m = pat.match(n)
        if m:
            ids.setdefault(m.group(1).lower(), set()).add(m.group(2))
    ents = wad.Wad(usrdir / 'offline.wad').get('entities.bin')
    for s in re.findall(rb'(drivers|teams|vehicles)/([\w\-]+)\0', ents):
        ids.setdefault(s[0].decode().lower(), set()).add(s[1].decode())
    for s in re.findall(rb'\0([A-Z][A-Za-z]+)\0', ents):
        ids.setdefault('tracks', set()).add(s.decode())
    ids.setdefault('tracks', set()).update(p.stem for p in usrdir.glob('*.wad'))
    ids['tracks'].update({i.capitalize() for i in ids['tracks']})
    tracks = ids['tracks'] | {p.name for p in (usrdir / 'tracks').iterdir() if p.is_dir()}
    tracks |= {t.lower() for t in tracks} | {t.capitalize() for t in tracks}
    cands = list(logged)
    for n in logged:
        m = pat.match(n)
        if m:
            cands += [f'{m.group(1)}/{i}/{m.group(3)}' for i in ids.get(m.group(1).lower(), ())]
        # a track's name anywhere in the path (Tracks/Silverstone/Silverstone_Normal.sg, MapSilverstone128.gtf)
        for t in tracks:
            if len(t) > 3 and t in n:
                cands += [n.replace(t, u) for u in tracks]
    new = []
    for n in dict.fromkeys(cands):
        h = wad.name_hash(n)
        if h in hashes and h not in known:
            known.add(h)
            new.append(n)
    with open(known_file, 'a', encoding='latin1', newline='\n') as f:
        for n in new:
            f.write(n + '\n')
    total = len(known_file.read_text(encoding='latin1').splitlines())
    print(f'{len(new)} new names ({len(logged)} logged); {total} lines in {known_file.name}, {len(hashes)} entries')
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
