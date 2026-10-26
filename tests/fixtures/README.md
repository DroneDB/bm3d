# Test fixtures

`texture.gif` is the one texture the tests do not write themselves: no encoder for GIF is
among the libraries the tests use, and the program reads GIF through stb_image. It is a
16x16 picture with 16 colors, the color of a pixel being the index of its 4x4 block (index
`x / 4 + 4 * (y / 4)`; color `i` is `(16 i, 255 - 16 i, 8 i)`, the palette that
`paletteColor` in `tests/support/pictures.cpp` gives the other paletted texture formats).

It was made once with Python 3 and Pillow 12.1.1, and is committed as it is:

```python
from PIL import Image

palette = []
for i in range(16):
    palette += [i * 16, 255 - i * 16, i * 8]
palette += [0] * (256 * 3 - len(palette))
image = Image.new("P", (16, 16))
image.putpalette(palette)
image.putdata([x // 4 + 4 * (y // 4) for y in range(16) for x in range(16)])
image.save("texture.gif", format="GIF", optimize=False)
```
