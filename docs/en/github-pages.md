# Online and local editions

Use Chrome or Edge on desktop. On iPhone/iPad, use [Bluefy](https://apps.apple.com/app/id1492822055). On Android, prefer Chrome. Open the site in that browser rather than an embedded WeChat/in-app window, and grant Bluetooth permission.

[简体中文](../zh-CN/github-pages.md) | English

Open [InkTag WebUI](https://solar2ain.github.io/ink-tag/) without installing anything. Chrome or Edge with Web Bluetooth is recommended. Click Connect and authorize the correct device on the first connection.

| Edition | Best for | Find |
| --- | --- | --- |
| Online WebUI | Connecting, images, canvas editing and device management | Enrollment and broadcast control; location queries require the local service |
| Standalone ZIP | Keeping the WebUI offline | Same device controls; extract everything and keep assets beside the HTML |
| Static ZIP | Hosting the website yourself | No query backend |
| CLI ZIP with local WebUI | Local data, Find queries and automation | Local query backend; follow first-use dependency setup |

Local operation requires Python 3.11 or later, but not Node.js. See [WebUI setup](webui.md) and [Find migration](find.md).

For phone access to a computer's local service, use the same LAN and trusted HTTPS as shown by the launcher. Ordinary LAN HTTP generally cannot use browser Bluetooth. The phone browser must support Web Bluetooth too.

If the online site cannot load, try again later or use a local edition. Changing origins or browsers, or clearing site data, can remove saved settings and Bluetooth permissions; export device information first. Updating a client never automatically flashes firmware.
