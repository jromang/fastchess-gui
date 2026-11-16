# Assets

## 3D chess pieces (`board3d/pieces.bin`)

The Staunton pieces of clarkerubber's Staunton-Pieces (https://github.com/clarkerubber/Staunton-Pieces,
`Source/Staunton/*/*.STL`), simplified and packed by `tools/board3d/bake.cpp`. MIT license:

    The MIT License (MIT)

    Copyright (c) 2014 clarkerubber

    Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated
    documentation files (the "Software"), to deal in the Software without restriction, including without limitation
    the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to
    permit persons to whom the Software is furnished to do so, subject to the following conditions:

    The above copyright notice and this permission notice shall be included in all copies or substantial portions of
    the Software.

    THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE
    WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
    COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
    OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

## The 3D board's tone mapping (`gui/src/board3d/shaders/display.frag`)

AgX, Troy Sobotka's display transform (https://github.com/sobotka/AgX), as Benjamin Wrensch fitted it in his
"Minimal AgX implementation" (https://iolite-engine.com/blog_posts/minimal_agx_implementation), its look changed. MIT
license:

    MIT License

    Copyright (c) 2024 Missing Deadlines (Benjamin Wrensch)

    Permission is hereby granted, free of charge, to any person obtaining a copy
    of this software and associated documentation files (the "Software"), to deal
    in the Software without restriction, including without limitation the rights
    to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
    copies of the Software, and to permit persons to whom the Software is
    furnished to do so, subject to the following conditions:

    The above copyright notice and this permission notice shall be included in
    all copies or substantial portions of the Software.

    THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
    IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
    FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
    AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
    LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
    OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
    SOFTWARE.

    All values used to derive this implementation are sourced from Troy's initial AgX implementation/OCIO config
    file available here: https://github.com/sobotka/AgX

## Chess pieces (`pieces/<set>/*.svg`)

Four sets, embedded in the program and chosen in **Settings** > Appearance. Each is the version the lichess project
optimized for the web (https://github.com/lichess-org/lila, `public/piece/<set>/`, whose `COPYING.md` lists their
authors and licenses), unchanged.

| Set | Author | License |
|---|---|---|
| Cburnett (the default) | Colin M.L. Burnett | BSD 3-clause, below |
| Chessnut | Alexis Luengas (https://github.com/LexLuengas/chessnut-pieces) | Apache 2.0: [licenses/Apache-2.0.txt](licenses/Apache-2.0.txt) |
| Celtic | Maurizio Monge (https://github.com/maurimo/chess-art) | MIT, below |
| Fantasy | Maurizio Monge (https://github.com/maurimo/chess-art) | MIT, below |

### Cburnett (`pieces/cburnett/`)

"Cburnett" set by Colin M.L. Burnett, from Wikimedia Commons
(https://commons.wikimedia.org/wiki/Category:SVG_chess_pieces), as optimized by the lichess project
(https://github.com/lichess-org/lila, `public/piece/cburnett/`).

The author offers the choice of GFDL 1.2+, CC BY-SA 3.0, BSD 3-clause or GPL 2+. They are used here under
the **BSD 3-clause license**:

    Copyright (c) 2006, Colin M.L. Burnett
    All rights reserved.

    Redistribution and use in source and binary forms, with or without modification, are permitted provided
    that the following conditions are met:

    1. Redistributions of source code must retain the above copyright notice, this list of conditions and
       the following disclaimer.
    2. Redistributions in binary form must reproduce the above copyright notice, this list of conditions and
       the following disclaimer in the documentation and/or other materials provided with the distribution.
    3. Neither the name of the copyright holder nor the names of its contributors may be used to endorse or
       promote products derived from this software without specific prior written permission.

    THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED
    WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
    PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR
    ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED
    TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
    HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
    NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
    POSSIBILITY OF SUCH DAMAGE.

### Chessnut (`pieces/chessnut/`)

"Chessnut" set by Alexis Luengas (https://github.com/LexLuengas/chessnut-pieces), as optimized by the lichess
project (`public/piece/chessnut/`), under the **Apache License 2.0**:
[licenses/Apache-2.0.txt](licenses/Apache-2.0.txt).

    Copyright Alexis Luengas

    Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except in compliance
    with the License. You may obtain a copy of the License at http://www.apache.org/licenses/LICENSE-2.0

    Unless required by applicable law or agreed to in writing, software distributed under the License is distributed
    on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. See the License for
    the specific language governing permissions and limitations under the License.

### Celtic and Fantasy (`pieces/celtic/`, `pieces/fantasy/`)

"Celtic" and "Fantasy" sets by Maurizio Monge (https://github.com/maurimo/chess-art), as optimized by the lichess
project (`public/piece/celtic/`, `public/piece/fantasy/`), under the **MIT license**:

    Copyright (c) Maurizio Monge

    Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated
    documentation files (the "Software"), to deal in the Software without restriction, including without limitation
    the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and
    to permit persons to whom the Software is furnished to do so, subject to the following conditions:

    The above copyright notice and this permission notice shall be included in all copies or substantial portions of
    the Software.

    THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO
    THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
    AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF
    CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
    IN THE SOFTWARE.

## Sounds

The move sounds are made by the program as it runs (`gui/src/sound.cpp`): no sound file is shipped.

## Fonts (fetched at build time and embedded)

- **Inter** 4.1 by Rasmus Andersson (https://rsms.me/inter/), the text and titles (Regular, SemiBold, and Italic
  for the guide),
  under the SIL Open Font License 1.1: [licenses/Inter-OFL-1.1.txt](licenses/Inter-OFL-1.1.txt).
- **Lucide** 1.59 icon font (https://lucide.dev), the icons, under the ISC license:
  [licenses/Lucide-ISC.txt](licenses/Lucide-ISC.txt).

A system font (DejaVu Sans or Segoe UI Symbol) is merged in when present, for the few signs Inter lacks.

## Libraries fetched at build time

| Library | License |
|---|---|
| SDL 3.4.18 | zlib |
| Dear ImGui 1.92.9 (docking) | MIT |
| ImPlot 1.0 | MIT |
| nanosvg | zlib |
| stb_image (PNG and JPEG reading, for the engines' logos), stb_image_write | MIT / public domain |
| libssh2 (master, e75fa9a), modified for the tunnel's memory (see cmake/patch_libssh2.cmake in the sources), with a cryptography backend for Mbed TLS 4 written for fastchess-gui | BSD-3-Clause, below |
| Mbed TLS 4.1.1 (TLS for lichess, cryptography for the tunnel) | Apache-2.0: [licenses/Apache-2.0.txt](licenses/Apache-2.0.txt) |
| QR Code generator 1.8.0 by Project Nayuki | MIT, below |
| md4c 0.5.2 by Martin Mitáš (the guide's Markdown) | MIT, below |
| tinygettext | zlib, below |

libssh2's license:

    Copyright (C) 2004-2007 Sara Golemon <sarag@libssh2.org>
    Copyright (C) 2005,2006 Mikhail Gusarov <dottedmag@dottedmag.net>
    Copyright (C) 2006-2007 The Written Word, Inc.
    Copyright (C) 2007 Eli Fant <elifantu@mail.ru>
    Copyright (C) 2009-2023 Daniel Stenberg
    Copyright (C) 2008, 2009 Simon Josefsson
    Copyright (C) 2000 Markus Friedl
    Copyright (C) 2015 Microsoft Corp.
    All rights reserved.

    Redistribution and use in source and binary forms, with or without modification, are permitted provided that
    the following conditions are met:

    1. Redistributions of source code must retain the above copyright notice, this list of conditions and the
       following disclaimer.
    2. Redistributions in binary form must reproduce the above copyright notice, this list of conditions and the
       following disclaimer in the documentation and/or other materials provided with the distribution.
    3. Neither the name of the copyright holder nor the names of its contributors may be used to endorse or promote
       products derived from this software without specific prior written permission.

    THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED
    WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
    PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY
    DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
    PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
    CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
    OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH
    DAMAGE.

QR Code generator's license:

    Copyright © 2022 Project Nayuki. (MIT License)
    https://www.nayuki.io/page/qr-code-generator-library

    Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated
    documentation files (the "Software"), to deal in the Software without restriction, including without limitation
    the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and
    to permit persons to whom the Software is furnished to do so, subject to the following conditions:

    * The above copyright notice and this permission notice shall be included in all copies or substantial portions
      of the Software.

    * The Software is provided "as is", without warranty of any kind, express or implied, including but not limited
      to the warranties of merchantability, fitness for a particular purpose and noninfringement. In no event shall
      the authors or copyright holders be liable for any claim, damages or other liability, whether in an action of
      contract, tort or otherwise, arising from, out of or in connection with the Software or the use or other
      dealings in the Software.

md4c's license:

    Copyright © 2016-2024 Martin Mitáš

    Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated
    documentation files (the “Software”), to deal in the Software without restriction, including without limitation
    the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and
    to permit persons to whom the Software is furnished to do so, subject to the following conditions:

    The above copyright notice and this permission notice shall be included in all copies or substantial portions
    of the Software.

    THE SOFTWARE IS PROVIDED “AS IS”, WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED
    TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
    THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF
    CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
    DEALINGS IN THE SOFTWARE.

tinygettext's license (zlib):

    Copyright (c) 2004-2016 Bastiaan Zapf, Christoph Sommer, Georg Kilzer, Ingo Ruhnke, Mathnerd314, Matt McCutchen,
    Matthias Braun, Nathan Phillip Brink, Poren Chiang, Ravu al Hemio, Ryan Flegel, Tim Goya, Tobias Markus,
    Wolfgang Becker

    This software is provided 'as-is', without any express or implied warranty. In no event will the authors be held
    liable for any damages arising from the use of this software.

    Permission is granted to anyone to use this software for any purpose, including commercial applications, and to
    alter it and redistribute it freely, subject to the following restrictions:

    1. The origin of this software must not be misrepresented; you must not claim that you wrote the original
       software. If you use this software in a product, an acknowledgement in the product documentation would be
       appreciated but is not required.
    2. Altered source versions must be plainly marked as such, and must not be misrepresented as being the original
       software.
    3. This notice may not be removed or altered from any source distribution.
