# 组件与资源来源 / Component and resource sources

项目许可见 [NOTICE.md](NOTICE.md)，依赖清单见 [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)。各组件的原始版权和许可声明保留在对应文件中。

- 农历年表：Lunar-Solar-Calendar-Converter 提交 `55d4e547947594736f7c9e4a35521ecc1c7342d8`，[MIT 正文](compat/common/gui/licenses/lunar-calendar-MIT.txt)。
- 当前使用的四组字模（文泉驿 9/12、Helvetica Bold 14/18）、历法文字表和 26 条固定节日记录取自 [EPD-nRF5 GUI 资源](https://github.com/tsl0922/EPD-nRF5/tree/7e3196193997bd6a4644610c41d54a76eff88760/GUI)。字体各自的版权与许可单独适用，GPL 许可文本见 [LICENSES/GPL-3.0.txt](LICENSES/GPL-3.0.txt)。
- 当前节气表：由 JPL DE440s／Skyfield 独立计算，方法、依赖和输入哈希见 [来源记录](build-support/calendar/solar-terms-source.json)，重建步骤见 [生成说明](build-support/calendar/README.md)。JPL 输入遵循 [NAIF 使用规则](https://naif.jpl.nasa.gov/naif/rules.html)，不随源码打包。

Original component licenses and prior grants are retained. Font data has its own terms, independent of the graphics-engine license. Ordinary builds use the checked-in solar-term resource; astronomy inputs are needed only for regeneration.
