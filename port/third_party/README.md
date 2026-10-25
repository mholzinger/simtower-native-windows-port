# Vendored third-party files

Used only by the software canvas (`simtower_canvas`: SDL frontend and the
portable tests). The Win32 host does not link them. License texts are in
`../../THIRD_PARTY_LICENSES/`.

| File | Version | Source | SHA-256 |
|---|---|---|---|
| `stb/stb_truetype.h` | v1.26, commit `6e9f34d5429cf16790ec43c9bac3f1ee4ad1f760` | https://raw.githubusercontent.com/nothings/stb/6e9f34d5429cf16790ec43c9bac3f1ee4ad1f760/stb_truetype.h | `ecd30b05e0dd4fea3a13c26810dd9e1992dc379049482c393d5a19e6b5090aab` |
| `liberation-fonts/LiberationSans-Regular.ttf` | Liberation Fonts 2.1.5 | https://github.com/liberationfonts/liberation-fonts/files/7261482/liberation-fonts-ttf-2.1.5.tar.gz (release 2.1.5) | `76d04c18ea243f426b7de1f3ad208e927008f961dc5945e5aad352d0dfde8ee8` |

The Liberation archive itself has SHA-256
`7191c669bf38899f73a2094ed00f7b800553364f90e2637010a69c0e268f25d0`. Both
files are unmodified. The font is embedded into the canvas library at build
time by `../cmake/embed_font.cmake`.
