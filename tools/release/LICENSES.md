# Third-party notices

This directory collects notices for bundled components; it does not grant
additional rights. Retain these notices when redistributing the package.

- Chromium: BSD-style license in Chromium.txt; Chromium-Credits.html contains
  generated third-party notices and is also available through chrome://credits.
- WebRTC: BSD-style license in WebRTC.txt; dependency notices are included in
  Chromium credits.
- OpenH264: BSD license. re links OpenH264 from the Chromium tree (source build);
  no Cisco binary is downloaded or bundled by this packaging script. Cisco binary
  licensing/patent terms must not be assumed to cover this source build. An
  externally supplied Cisco DLL must meet its own redistribution terms. Use
- nv-codec-headers: MIT license in nv-codec-headers.txt. NVIDIA driver runtime
  libraries are supplied by the installed graphics driver, not this package.
- oneVPL: MIT license in oneVPL.txt. Intel graphics drivers supply the hardware
  runtime; the dispatcher is linked into re.
- Node.js: Node-LICENSE.txt includes Node and bundled dependency notices.
- Bundled JavaScript: ws.txt, React.txt and React-DOM.txt retain the MIT notices.
- Widevine: proprietary Google CDM. Its LICENSE is retained in rb/WidevineCdm
  and copied here when present. Bundling does not establish redistribution
  permission or grant access to protected services. Confirm applicable Google
  CDM distribution/service terms before publication.
- Microsoft runtime DLLs: Microsoft Visual C++ redistributable components; their
  redistribution is subject to the applicable Visual Studio license terms.

Final distribution/legal review remains release qualification work; these notes
are not a complete legal assessment.
