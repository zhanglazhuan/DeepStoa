"""Recover SD resources from project sources without overwriting different files."""
import argparse
import hashlib
import re
import shutil
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--destination', type=Path, required=True)
    parser.add_argument('--zip', type=Path)
    args = parser.parse_args()
    dest = args.destination.resolve()
    sources = {}
    for source in (ROOT / 'components/epaper_lib/Fonts').rglob('*.FON'):
        name = source.name
        if name.startswith('UTF_'):
            target = 'font/UTF/' + name[4:]
        elif name.startswith('GBK_'):
            target = 'font/GBK/' + name[4:]
        else:
            target = 'font/ASICC/' + name
        if target in sources:
            raise RuntimeError('Duplicate target: ' + target)
        sources[target] = source
    for source in (ROOT / 'main/page_weather/Weather_img').iterdir():
        if source.is_file():
            sources['Weather_img/Weather_img/' + source.name] = source
    sources['city_code.txt'] = ROOT / 'main/page_weather/city_code.txt'
    # Preflight all conflicts before copying anything.
    for name, source in sources.items():
        target = dest / name
        if target.exists() and (not target.is_file() or sha(target) != sha(source)):
            raise RuntimeError('Different existing file preserved: ' + str(target))
    dest.mkdir(parents=True, exist_ok=True)
    for directory in ['GUI', 'music', 'fiction', 'picture', 'todolist',
                      'mistakebook', 'bookmarks/epub_cache', 'lunar']:
        (dest / directory).mkdir(parents=True, exist_ok=True)
    for name, source in sources.items():
        target = dest / name
        target.parent.mkdir(parents=True, exist_ok=True)
        if not target.exists():
            shutil.copy2(source, target)
        assert sha(source) == sha(target), name
    required = set()
    for folder in ['main', 'components/epaper_lib', 'components/sdcard_bsp']:
        for source in (ROOT / folder).rglob('*'):
            if source.suffix not in ['.c', '.cc', '.h']:
                continue
            for name in re.findall(r'"/sdcard/([^"\n]+)"', source.read_text(errors='replace')):
                if '%' not in name and Path(name).suffix.lower() in ['.fon', '.bmp', '.bin', '.txt']:
                    required.add(name)
    missing = sorted(name for name in required if not (dest / name).is_file())
    print('Verified resource files:', len(sources))
    print('Total bytes:', sum((dest / name).stat().st_size for name in sources))
    print('Missing referenced resources:', missing)
    if args.zip:
        args.zip.parent.mkdir(parents=True, exist_ok=True)
        # Exclusive creation protects an existing recovery archive.
        with zipfile.ZipFile(args.zip, 'x', zipfile.ZIP_DEFLATED) as archive:
            for path in sorted(dest.rglob('*')):
                archive.write(path, str(path.relative_to(dest)) + ('/' if path.is_dir() else ''))
        print('ZIP:', args.zip)


if __name__ == '__main__':
    main()
