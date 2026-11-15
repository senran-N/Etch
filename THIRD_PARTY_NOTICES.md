# Third-party notices

Etch's [MIT license](LICENSE) covers Etch's original code. Third-party code
and data retain their own licenses. The summaries below were checked against
the bundled files; retain the applicable full licenses and notices when
redistributing those components, including in binary packages.

## SDL3

[third_party/SDL3/LICENSE.txt](third_party/SDL3/LICENSE.txt) contains the zlib
license and `Copyright (C) 1997-2026 Sam Lantinga <slouken@libsdl.org>`.
Commercial use, modification, and redistribution are permitted. Do not
misrepresent origin; mark altered source versions and preserve the notice in
source distributions.

## HarfBuzz

[third_party/harfbuzz/COPYING](third_party/harfbuzz/COPYING) identifies the
Old MIT license and lists the copyright holders, including Google, Ebrahim
Byagowi, Facebook, Mozilla, Codethink, Nokia, Keith Stribley, Martin Hosken and
SIL International, Chris Wilson, Behdad Esfahbod, Red Hat, David Turner and
Werner Lemberg, Igalia, Matthias Clasen, Khaled Hosny, Adobe, and Alexei
Podtelezhnikov. Preserve that file's full copyright notices and both
disclaimer paragraphs in copies. It also directs readers to individual files
and subdirectory COPYING files for additional notices or different licenses.

## Clay

[third_party/clay.h](third_party/clay.h), version 0.14, includes its
zlib/libpng license at the end of the header:
`Copyright (c) 2024 Nic Barker`. Do not misrepresent origin; mark altered
source versions and preserve the notice in source distributions.

## stb headers

- [stb_image.h](third_party/stb_image.h) v2.32, Copyright (c) 2017 Sean Barrett. MIT / Public Domain. Used for static JPEG/GIF/TGA image decoding.

Sean Barrett's [stb_truetype.h](third_party/stb_truetype.h) v1.28 and
[stb_image_write.h](third_party/stb_image_write.h) v1.18 each include their
full license at the end of the header. Choose either Alternative A (MIT,
`Copyright (c) 2017 Sean Barrett`) or Alternative B (Public Domain / Unlicense).
The MIT alternative requires retaining its copyright and permission notice.
Etch uses stb_truetype for font parsing and stb_image_write for image output.

## Unicode Character Database

[src/etch_bidi_data.h](src/etch_bidi_data.h) and
[src/etch_grapheme_data.h](src/etch_grapheme_data.h) contain tables derived
from Unicode 17.0.0 Character Database data (DerivedBidiClass,
GraphemeBreakProperty, and emoji-data). The applicable
[third_party/UNICODE-LICENSE.txt](third_party/UNICODE-LICENSE.txt) is
Unicode License V3, `Copyright (c) 1991-2026 Unicode, Inc.`
(rendered with the copyright symbol in the source). Preserve its copyright
and permission notice with copies of the data/software or in accompanying
documentation.

## Slug

[third_party/slug/LICENSE](third_party/slug/LICENSE) offers a choice of
[MIT](third_party/slug/LICENSE-MIT) (`Copyright (c) 2017 Eric Lengyel`) or
[Apache License 2.0](third_party/slug/LICENSE-APACHE). Retain the notices and
license text required by the chosen license, including the applicable Apache
NOTICE attribution when distributing under Apache-2.0.

[third_party/slug/NOTICE](third_party/slug/NOTICE) supplies this credit:

> Slug shader code Copyright 2017 by Eric Lengyel.

[third_party/slug/README.md](third_party/slug/README.md) states that the patent
has been dedicated to the public domain and requires credit when distributing
software that uses the code. Etch retains the credit above for its Slug GPU
Bezier rendering implementation. No patent number is specified in these
bundled license, notice, or README files.
