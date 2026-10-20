# Licensing and attribution

Copyright (c) 2026 Jonathan Yantis.

| Material | License |
| --- | --- |
| Original software, scripts, and ordinary repository documentation | [MIT](LICENSE) |
| Original research papers, proposal manuscripts, their LaTeX sources, and figures | [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/) |
| Imported files carrying Apache-2.0 SPDX notices | [Apache License 2.0](LICENSES/Apache-2.0.txt); existing notices remain authoritative |
| TinyCrypt HMAC copies in W12/SS3 | [Intel BSD-3-Clause notice](LICENSES/TinyCrypt-BSD-3-Clause.txt) retained in each file |
| Other third-party material and separately acquired dependencies | Their existing license and attribution |
| Included original evidence records and derived CSV datasets | [CC BY 4.0](evidence/LICENSE.md) |
| Future third-party or differently licensed datasets | Explicit terms recorded with the import |

The manuscript and figure license is Creative Commons Attribution 4.0
International. Its [legal terms](https://creativecommons.org/licenses/by/4.0/legalcode)
apply, including its warranty disclaimer. Credit Jonathan Yantis, identify the
work and version, retain supplied notices, link the license, and indicate changes
as required by those terms. This policy adds no custom license restrictions.

For example: Jonathan Yantis, *[paper title]*, draft dated 2 October 2026,
PToB, licensed under CC BY 4.0. Include a link to the relevant repository version
when sharing, and identify any modifications.

The MIT license requires preservation of its copyright and permission notice.
Use Jonathan Yantis as the copyright name on new original project material.
Manuscript LaTeX is covered by the manuscript license even though it is source
text; executable build and analysis tools remain MIT unless otherwise marked.

Draft status describes research maturity and does not change license permissions.
Licensing a paper does not claim ownership of cited research or relicense it.
Do not apply these defaults to inherited third-party material without review.

## Imported source and dependencies

The MIT default does not replace an explicit per-file license. Selected firmware
imports retain Apache-2.0 notices, including original project files historically
published under those terms. The complete license text is included in `LICENSES/`;
source changes are identified by the target import manifests and Git history.
The W12 and SS3 HMAC copies retain Intel's complete BSD-3-Clause notice and
modification note. Do not strip these notices when redistributing source.

See the [T096](firmware/t096/NOTICE.md), [W12](firmware/w12/NOTICE.md),
[SS3](firmware/ss3/NOTICE.md), [N30](firmware/n30/NOTICE.md) and [macOS](clients/macos/NOTICE.md) notices for
scope and dependency attribution. Zephyr, ESP-IDF and their components are
acquired separately; their license obligations also apply to a future linked
firmware distribution. Chrony is a separately built GPL-2.0 dependency, not MIT
code; its source archive and binaries are not tracked here. A future binary
release needs its own complete dependency notices and corresponding-source
review. This source repository does not grant rights to Apple's SDK/frameworks
or to third-party works cited by the papers.
