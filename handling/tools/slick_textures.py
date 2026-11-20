"""Preview and replace the dry tread GTFs using the stock WAD as source."""
from io import BytesIO
from pathlib import Path
import argparse
import struct
import sys

from PIL import Image

TEST = Path(__file__).resolve().parents[1]
TREAD = 'Models/TextureSwaps/Wheels/Tred/Default_TyreTred_Dry_Blur0_'


def load_image(data):
    off, size = struct.unpack_from('>II', data, 16)
    fmt, mips = data[24:26]
    width, height = struct.unpack_from('>HH', data, 32)
    codec, block = {0x86: (b'DXT1', 8), 0x88: (b'DXT5', 16)}[fmt]
    assert (width, height, mips, off) == (256, 256, 9, 128)
    assert off + size <= len(data)
    first = width * height // 16 * block
    header = struct.pack('<7I44x2I4s40x', 124, 0x81007, height, width, first, 0, 1, 32, 4, codec)
    return Image.open(BytesIO(b'DDS ' + header + data[off:off + first])).convert('RGBA'), (off, size, codec, block)


def remove_grooves(img, channel):
    result = img.copy()
    for center, source in ((55, 79), (104, 128), (151, 175), (199, 221)):
        left, right = center - 11, center + 12
        pixels = result.load()
        stock = img.load()
        for y in range(img.height):
            for x in range(left, right):
                t = (x - left + 1) / (right - left + 1)
                if channel == 'N':
                    a, b = stock[left - 1, y], stock[right, y]
                    value = tuple(round(u + t * (v - u)) for u, v in zip(a, b))
                else:
                    original = stock[x, y]
                    rubber = stock[source + x - center, (y + 37 + center) % img.height]
                    weight = min(1.0, (x - left + 1) / 4, (right - x) / 4)
                    value = tuple(round(u + weight * (v - u)) for u, v in zip(original, rubber))
                pixels[x, y] = value
    return result


def replace(data, img, channel):
    _, (off, size, codec, block) = load_image(data)
    slick = remove_grooves(img, channel)
    levels = []
    for i in range(data[25]):
        n = img.width >> i
        mip = slick if i == 0 else slick.resize((n, n), Image.Resampling.BOX)
        output = BytesIO()
        mip.save(output, format='DDS', pixel_format=codec.decode('ascii'))
        encoded = output.getvalue()
        assert encoded[84:88] == codec and len(encoded) - 128 == ((n + 3) // 4) ** 2 * block
        levels.append(encoded[128:])
    payload = b''.join(levels)
    assert len(payload) == size
    return data[:off] + payload + data[off + size:]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('wad_tools', type=Path, help='Directory containing Recomp tools/wad.py')
    parser.add_argument('usrdir', type=Path)
    parser.add_argument('--build', action='store_true')
    args = parser.parse_args()
    sys.path.insert(0, str(args.wad_tools))
    from wad import Wad
    preview = TEST / 'Texture-Preview'
    preview.mkdir(exist_ok=True)
    for archive in ('offline.wad', 'stream.wad') if args.build else ('offline.wad',):
        wad = Wad(args.usrdir / archive)
        try:
            for channel in ('CA', 'N', 'S'):
                name = f'{TREAD}{channel}.gtf'
                data = wad.get(name)
                img, _ = load_image(data)
                if archive == 'offline.wad':
                    img.save(preview / f'Original-{channel}.png')
                if args.build:
                    new_data = replace(data, img, channel)
                    assert new_data[:128] == data[:128] and len(new_data) == len(data)
                    target = TEST / 'MODS' / 'SLICK_TEXTURES' / archive / name
                    target.parent.mkdir(parents=True, exist_ok=True)
                    target.write_bytes(new_data)
                    if archive == 'offline.wad':
                        load_image(new_data)[0].save(preview / f'Slick-{channel}.png')
        finally:
            wad.f.close()


if __name__ == '__main__':
    main()
