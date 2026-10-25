# Notices that live only in source headers

`wine.app` contains code whose licence notice is in its source files, not in a licence file of its own. Each section
names the component, the binaries it is compiled into, the file the notice is quoted from (in the source tree pinned in
`SOURCE`), and the notice itself. The licence files of the components (`wine/`, `fex/`, `llvm/`, `llvm-mingw/`,
`freetype/`, `gnutls/`, `nettle/`, `gmp/`, `ffmpeg/`, `lsteamclient/`, `macneutron/`, and `../DXMT/` for DXMT) are
beside this file; `README` says where each component's source is.

## SoftFloat-3e (in FEX: `libarm64ecfex.dll`)

From FEX's `External/SoftFloat-3e/include/SoftFloat-3e/primitiveTypes.h` (every file in `External/SoftFloat-3e`
carries the same notice):

```
This C header file is part of the SoftFloat IEEE Floating-Point Arithmetic
Package, Release 3e, by John R. Hauser.

Copyright 2011, 2012, 2013, 2014 The Regents of the University of California.
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

 1. Redistributions of source code must retain the above copyright notice,
    this list of conditions, and the following disclaimer.

 2. Redistributions in binary form must reproduce the above copyright notice,
    this list of conditions, and the following disclaimer in the documentation
    and/or other materials provided with the distribution.

 3. Neither the name of the University nor the names of its contributors may
    be used to endorse or promote products derived from this software without
    specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS "AS IS", AND ANY
EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE, ARE
DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE FOR ANY
DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
```

## VIXL utilities (in FEX: `libarm64ecfex.dll`)

From FEX's `CodeEmitter/CodeEmitter/VixlUtils.inl` ("Collection of utilities from vixl"):

```
Copyright 2015, VIXL authors
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

  * Redistributions of source code must retain the above copyright notice,
    this list of conditions and the following disclaimer.
  * Redistributions in binary form must reproduce the above copyright notice,
    this list of conditions and the following disclaimer in the documentation
    and/or other materials provided with the distribution.
  * Neither the name of ARM Limited nor the names of its contributors may be
    used to endorse or promote products derived from this software without
    specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS CONTRIBUTORS "AS IS" AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE
FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
```

## musl (in FEX's runtime: `libarm64ecfex.dll`)

From FEX's `Source/Windows/Common/CRT/Musl/fmod.c` (and the other musl files there; `strtoimax.c`, `strtoll.c`,
`strtoull.c` and `strtoumax.c` say 2005-2011), under the MIT licence below:

```
SPDX-License-Identifier: MIT
SPDX-FileCopyrightText: Copyright © 2005-2020 Rich Felker, et al.
```

## Arm optimized routines (in FEX's runtime: `libarm64ecfex.dll`)

From FEX's `Source/Windows/Common/CRT/Musl/exp2.c` (and `exp_data.c`, `exp_data.h`, `log2.c`, `log2_data.c`,
`log2_data.h`), under the MIT licence below:

```
SPDX-License-Identifier: MIT
SPDX-FileCopyrightText: Copyright (c) 2018, Arm Limited.
```

## Madeira (in FEX: `libarm64ecfex.dll`; in Wine: `ntdll.dll`)

FEX patches 0003 and 0004 come from Will Faust's Madeira commits (`willfaust/FEX`, branch `ios-port-2607`), and patch
0005 is derived from Madeira's dual-map commits; all are dated before 2026-08-28 and are used under the MIT licence
below. From
`LICENSE-MADEIRA.md` in `https://github.com/willfaust/FEX.git` (as of commit `26859e184a`):

```
- **Modifications published earlier, while this repository presented itself as
  MIT, were granted under MIT. That grant cannot be revoked.** Only
  contributions made from 2026-08-28 onward are GPL-only. Anyone who already has a
  copy keeps their MIT rights to it.
```

Wine patch 0009 is adapted from Madeira's Wine commits on its LGPL branch `madeira-lgpl`; it is LGPL-2.1+ like the rest
of Wine (`wine/LICENSE`).

## msync (in Wine: `ntdll.so`, `wineserver`)

Wine patch 0015 is CodeWeavers' CrossOver 26.3 msync, as carried on `dappermint/winecx` branch `cx/wine1117` with
millia ampora's msync commits there; it is LGPL-2.1+ like the rest of Wine (`wine/LICENSE`). From the patch's
`dlls/ntdll/unix/msync.c` (and its `msync.h`, `server/msync.c` and `server/msync.h`):

```
Copyright (C) 2018 Zebediah Figura
Copyright (C) 2023 Marc-Aurel Zent
```

Its inline `mach_msg2()` and `mach_msg2_internal()` helpers (in `dlls/ntdll/unix/msync.c` and `server/msync.c`) are, in
the patch's words, "taken and slightly adapted from xnu/libsyscall/mach/mach_msg.c": Apple's xnu, Copyright Apple
Inc., under the Apple Public Source License 2.0 (https://opensource.apple.com/apsl/).

## DXBCParser (in DXMT's Direct3D DLLs and `winemetal.so`)

From DXMT's `libs/DXBCParser/ShaderBinary.h` (and the other files in `libs/DXBCParser`), under the MIT licence below:

```
Copyright (c) Microsoft Corporation.
Licensed under the MIT License.
```

## constexpr GUID parsing (in DXMT's Direct3D DLLs)

From DXMT's `src/util/com/com_guid.hpp`, under the MIT licence below:

```
This file is part of DXMT, Copyright (c) 2023 Feifan He

Derived from a part of DXVK (originally under zlib License),
Copyright (c) 2017 Philip Rebohle
Copyright (c) 2019 Joshua Ashton

See <https://github.com/doitsujin/dxvk/blob/master/LICENSE>

constexpr GUID parsing
Original version is written by Alexander Bessonov
\see https://gist.github.com/AlexBAV/b58e92d7632bae5d6f5947be455f796f
Licensed under the MIT license.
```

## NVAPI headers (in DXMT's `nvapi64.dll`)

DXMT's `src/nvapi/nvapi.cpp` is built against NVIDIA's NVAPI headers (DXMT's `external/nvapi`), under the MIT licence
below:

```
SPDX-FileCopyrightText: Copyright (c) 2019-2024 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
SPDX-License-Identifier: MIT

Permission is hereby granted, free of charge, to any person obtaining a
copy of this software and associated documentation files (the "Software"),
to deal in the Software without restriction, including without limitation
the rights to use, copy, modify, merge, publish, distribute, sublicense,
and/or sell copies of the Software, and to permit persons to whom the
Software is furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
DEALINGS IN THE SOFTWARE.
```

## ConvertUTF (in LLVM 15, linked into `winemetal.so`)

From LLVM's `llvm/lib/Support/ConvertUTF.cpp`:

```
Copyright 2001-2004 Unicode, Inc.

Disclaimer

This source code is provided as is by Unicode, Inc. No claims are
made as to fitness for any particular purpose. No warranties of any
kind are expressed or implied. The recipient agrees to determine
applicability of information provided. If this file has been
purchased on magnetic or optical media from Unicode, Inc., the
sole remedy for any claim will be exchange of defective media
within 90 days of receipt.

Limitations on Rights to Redistribute This Code

Unicode, Inc. hereby grants the right to freely use the information
supplied in this file in the creation of products supporting the
Unicode Standard, and to make copies of this file in any form
for internal or external distribution as long as this notice
remains attached.
```

## Regular expressions (in LLVM 15, linked into `winemetal.so`)

From LLVM's `llvm/lib/Support/regcomp.c` (the full texts are in `llvm/COPYRIGHT.regex`):

```
This code is derived from OpenBSD's libc/regex, original license follows:

Copyright (c) 1992, 1993, 1994 Henry Spencer.
Copyright (c) 1992, 1993, 1994
     The Regents of the University of California.  All rights reserved.

This code is derived from software contributed to Berkeley by
Henry Spencer.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions
are met:
1. Redistributions of source code must retain the above copyright
   notice, this list of conditions and the following disclaimer.
2. Redistributions in binary form must reproduce the above copyright
   notice, this list of conditions and the following disclaimer in the
   documentation and/or other materials provided with the distribution.
3. Neither the name of the University nor the names of its contributors
   may be used to endorse or promote products derived from this software
   without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
SUCH DAMAGE.
```

## randomart (in gnutls: `libgnutls.30.dylib`)

From gnutls's `lib/extras/randomart.c` (OpenBSD's `key.c`, revision 1.98):

```
Copyright (c) 2000, 2001 Markus Friedl.  All rights reserved.
Copyright (c) 2008 Alexander von Gernler.  All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions
are met:
1. Redistributions of source code must retain the above copyright
   notice, this list of conditions and the following disclaimer.
2. Redistributions in binary form must reproduce the above copyright
   notice, this list of conditions and the following disclaimer in the
   documentation and/or other materials provided with the distribution.

THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
```

## Hashing (in FreeType: `libfreetype.6.dylib`)

From FreeType's `src/base/fthash.c` (its `ft_hash` functions; the BDF and PCF drivers' own licence texts are
`freetype/bdf-README` and `freetype/pcf-README`):

```
Copyright 2000 Computing Research Labs, New Mexico State University
Copyright 2001-2015
  Francesco Zappa Nardelli

Permission is hereby granted, free of charge, to any person obtaining a
copy of this software and associated documentation files (the "Software"),
to deal in the Software without restriction, including without limitation
the rights to use, copy, modify, merge, publish, distribute, sublicense,
and/or sell copies of the Software, and to permit persons to whom the
Software is furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
THE COMPUTING RESEARCH LAB OR NEW MEXICO STATE UNIVERSITY BE LIABLE FOR ANY
CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT
OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR
THE USE OR OTHER DEALINGS IN THE SOFTWARE.
```

## FFmpeg (in FFmpeg: the `libavutil`, `libavcodec`, `libavformat`, `libswscale` and `libswresample` dylibs)

FFmpeg's development head (8.0.git), from `https://git.ffmpeg.org/ffmpeg.git` at FFMPEG_COMMIT unmodified and built
with no GPL or nonfree part, is under the GNU Lesser General Public License 2.1 or later, whose text is
`ffmpeg/COPYING.LGPLv2.1`; `ffmpeg/LICENSE.md` names its files under other terms. The notice its files carry, from `libavcodec/avcodec.c`:

```
This file is part of FFmpeg.

FFmpeg is free software; you can redistribute it and/or
modify it under the terms of the GNU Lesser General Public
License as published by the Free Software Foundation; either
version 2.1 of the License, or (at your option) any later version.

FFmpeg is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
Lesser General Public License for more details.

You should have received a copy of the GNU Lesser General Public
License along with FFmpeg; if not, write to the Free Software
Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
```

`libavcodec.63.dylib` contains FFmpeg's `libavcodec/jrevdct.c`, `jfdctfst.c` and `jfdctint_template.c`, DCTs built in
with the video decoders' shared DSP code, from the Independent JPEG Group's software (unmodified by us). This software
is based in part on the work of the Independent JPEG Group. The notice of `jrevdct.c` (the other two carry the same
one, copyright 1994-1996 and 1991-1996):

```
This file is part of the Independent JPEG Group's software.

The authors make NO WARRANTY or representation, either express or implied,
with respect to this software, its quality, accuracy, merchantability, or
fitness for a particular purpose.  This software is provided "AS IS", and
you, its user, assume the entire risk as to its quality and accuracy.

This software is copyright (C) 1991, 1992, Thomas G. Lane.
All Rights Reserved except as specified below.

Permission is hereby granted to use, copy, modify, and distribute this
software (or portions thereof) for any purpose, without fee, subject to
these conditions:
(1) If any part of the source code for this software is distributed, then
this README file must be included, with this copyright and no-warranty
notice unaltered; and any additions, deletions, or changes to the original
files must be clearly indicated in accompanying documentation.
(2) If only executable code is distributed, then the accompanying
documentation must state that "this software is based in part on the work
of the Independent JPEG Group".
(3) Permission for use of this software is granted only if the user accepts
full responsibility for any undesirable consequences; the authors accept
NO LIABILITY for damages of any kind.

These conditions apply to any software derived from or based on the IJG
code, not just to the unmodified library.  If you use our work, you ought
to acknowledge us.

Permission is NOT granted for the use of any IJG author's name or company
name in advertising or publicity relating to this software or products
derived from it.  This software may be referred to only as "the Independent
JPEG Group's software".

We specifically permit and encourage the use of this software as the basis
of commercial products, provided that all warranty or liability claims are
assumed by the product vendor.
```

`libavcodec.63.dylib` also contains `libavcodec/faandct.c`, a floating-point DCT, under this notice:

```
Copyright (c) 2003 Michael Niedermayer <michaelni@gmx.at>
Copyright (c) 2003 Roman Shaposhnik

Permission to use, copy, modify, and/or distribute this software for any
purpose with or without fee is hereby granted, provided that the above
copyright notice and this permission notice appear in all copies.

THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
```

`libavutil.61.dylib` contains `libavutil/avsscanf.c` (`av_sscanf`, from musl), under the MIT licence below:

```
Copyright (c) 2005-2014 Rich Felker, et al.
```

## Intel XeSS headers (in Wine: `libxess.dll`)

Wine patch 0024's `dlls/libxess/xess.h` copies the XeSS declarations the bridge implements from Intel's public headers
`inc/xess/xess.h` and `inc/xess/xess_d3d12.h` (`https://github.com/intel/xess.git`, branch `main`, MIT since commit
`de0fb9c`), with their notice, under the MIT licence below:

```
Copyright (c) 2026 Intel Corporation

SPDX-License-Identifier: MIT
```

## CMAA2 (in MacNeutron: `libmacneutron-present.metallib`)

The presenter's post-process anti-aliasing, `presenter/cmaa2.metal` in the MacNeutron repository, is our Metal port of
Intel's Conservative Morphological Anti-Aliasing 2.0 (CMAA2), `Projects/CMAA2/CMAA2/CMAA2.hlsl` from
`https://github.com/GameTechDev/CMAA2.git` at commit `071c6b0`. We modified it (the port, and the changes listed in the
file's header). It is under the Apache License 2.0, whose text is `macneutron/CMAA2-LICENSE.txt`, not the MIT licence
below. The notice it carries:

```
Copyright (c) 2018, Intel Corporation

Licensed under the Apache License, Version 2.0 ( the "License" );
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
```

## Arm Optimized Routines (in Wine: msvcrt.dll, ucrtbase.dll, msvcr80.dll-msvcr120.dll)

Wine patch 0035's `dlls/msvcrt/aor_string.h` carries the ARM64 and ARM64EC memmove (and so memcpy), strlen, strnlen,
strchr, strrchr, strcmp and memchr of Arm Optimized Routines (`https://github.com/ARM-software/optimized-routines.git`,
tag `v26.07`, commit `4be260a5117480382690c6d8c300bc784e927d76`): `string/aarch64/memcpy-advsimd.S`, `strlen.S`,
`strnlen.S`, `strchr-mte.S`, `strrchr-mte.S`, `strcmp.S` and `memchr.S`. We modified them: rewritten as inline assembly
in C helpers, with x11 for x14 in `memcpy-advsimd.S` (the header lists the changes). Upstream offers them under "MIT OR
Apache-2.0 WITH LLVM-exception"; we take them under the MIT licence below. The repository's LICENSE gives the MIT
copyright line, and each file its own:

```
Copyright (c) 1999-2022, Arm Limited.    (LICENSE)
Copyright (c) 2019-2023, Arm Limited.    (memcpy-advsimd.S)
Copyright (c) 2020-2022, Arm Limited.    (strlen.S, strnlen.S, strchr-mte.S)
Copyright (c) 2020-2023, Arm Limited.    (strrchr-mte.S)
Copyright (c) 2012-2022, Arm Limited.    (strcmp.S)
Copyright (c) 2014-2022, Arm Limited.    (memchr.S)
```

## The MIT licence

The terms of the MIT entries above, with each entry's copyright line (the text of FEX's `LICENSE`):

```
Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```
