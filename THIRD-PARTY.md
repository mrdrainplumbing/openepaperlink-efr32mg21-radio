# License scope and notices

Copyright © 2026 mrdrainplumbing. The [MIT license](LICENSE) covers original, independently written project contributions, including documentation, build tools and board integration. The following components retain their own terms.

| Component | Origin and terms |
|---|---|
| OEPL protocol, queue, transfer implementation and tests under `radio/oepl/` | Adapted from OpenEPaperLink, including the ESP32-C6 implementation by ATC1441 and other contributors. CC BY-NC-SA 4.0 applies to this adaptation. The notice lists the upstream revision and changes. [Attribution](radio/oepl/NOTICE) · [License](radio/oepl/LICENSE) |
| Silicon Labs Gecko SDK v4.5.1 and RAIL | The SDK default is the Silicon Labs Master Software License Agreement. Its explicit Zlib exception covers the xG21 archive in `platform/radio/rail_lib/autogen/librail_release` linked into the firmware. The EFR32 device support and emlib files used by the build also carry Zlib notices. [SDK terms and Zlib license](licenses/Gecko-SDK.txt) · [Pinned upstream terms](https://github.com/SiliconLabs/gecko_sdk/blob/8a2efd2cb191cb250fd4c1e655b16b8a80be1997/License.txt) |
| MSC flash programming sequence in `radio/tools/backup/program.c` | Based on Silicon Labs emlib, copyright 2018 Silicon Laboratories Inc., Zlib. This adaptation restricts addresses, compares words before programming, and checks the programmed values. [Original source and notice](https://github.com/SiliconLabs/gecko_sdk/blob/8a2efd2cb191cb250fd4c1e655b16b8a80be1997/platform/emlib/src/em_msc.c) · [Zlib license](licenses/Gecko-SDK.txt) |
| Arm CMSIS Core | The Cortex-M33 headers used in compilation are copyright Arm Limited, Apache License 2.0. [License](licenses/CMSIS-Apache-2.0.txt) · [Upstream header](https://github.com/SiliconLabs/gecko_sdk/blob/8a2efd2cb191cb250fd4c1e655b16b8a80be1997/platform/CMSIS/Core/Include/core_cm33.h) |
| GNU Arm GCC runtime | The release links GCC integer arithmetic helpers from libgcc. Applicable GCC runtime code carries GPLv3 or later with the GCC Runtime Library Exception 3.1, which permits distribution of eligible target code under independent module terms. [GCC runtime exception](https://github.com/gcc-mirror/gcc/blob/releases/gcc-14.2.0/COPYING.RUNTIME) |

## Redistributing the firmware

The MG21 release image contains the OEPL adaptation, Silicon Labs' precompiled RAIL implementation, and compiled runtime code. Retain the applicable notices and license information with redistributions. The OEPL rights granted here cover noncommercial use. When sharing adaptations of that material, credit the upstream contributors, identify modifications and preserve the CC BY-NC-SA licensing conditions. The MIT grant covers only the original contributions described above. Download the SDK and compiler separately to reproduce the firmware build. [OEPL license](radio/oepl/LICENSE) · [Silicon Labs SDK terms](licenses/Gecko-SDK.txt)
