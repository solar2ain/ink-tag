# u8g2-js source

This directory contains the dependency-free browser core from
createskyblue/u8g2-js, pinned at commit
e5ff4217ceb3f89ef07fc6d0f847b65255d3d4ef.

Only the runtime source and its BSD-2-Clause license are vendored so hosted and
standalone WebUI builds can render InkTag's firmware fonts without a network
request. InkTag font bytes are generated separately from
firmware/common/gui/inktag_gui_fonts.c.

InkTag applies two browser-only compatibility fixes: `drawButtonFrame` uses a
mutable `gapFrame`, and `U8g2Font.load` omits the Node filesystem branch. The
WebUI only loads generated device fonts from base64, so no runtime font fetch
or filesystem access is used by the product.
