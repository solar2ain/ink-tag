# Hosted WebUI and local clients

[中文](../zh-CN/github-pages.md) | English

Open [InkTag WebUI](https://solar2ain.github.io/ink-tag/) and consult the [illustrated manual](https://solar2ain.github.io/ink-tag-manual/). If the site is not yet available or cannot be reached, use the downloaded standalone HTML or local-service package.

| Client | Use | Difference |
| --- | --- | --- |
| Hosted WebUI | Open in Chrome or Edge with Web Bluetooth | No frontend installation; initial loading and update checks require a network; no local backend runs with the hosted page |
| Standalone HTML | Download and open in a compatible browser | Frontend works offline; Bluetooth still depends on browser support and permission |
| Local service | Install and start using the [WebUI guide](webui.md) | Provides local assets and backend features needed by host tasks and Find, subject to actual support |

The page cannot bypass Bluetooth authorization. Mobile support depends on the OS and browser; use a supported desktop browser or CLI if unavailable. The local service can be configured for LAN access as described in the guide; browser Bluetooth still requires a supported secure access context.

Browsers normally separate device records, settings and assets by origin. Do not assume hosted, standalone and local-service clients share their local data. Never copy control credentials, account sessions or location databases into a public site.
