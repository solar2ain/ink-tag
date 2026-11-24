# 节气资源生成

2000–2050 年节气按太阳视黄经每 15° 的交点计算，日期使用 UTC+08:00。
生成器使用 Skyfield 1.53 和 JPL DE440s，固定依赖、星历 SHA256 和方法见
[来源记录](solar-terms-source.json)。固件使用生成的 160 B 查表资源。

普通固件编译直接使用仓库内资源。重新生成时，在独立 Python 环境安装
[固定依赖](requirements.txt)，从 JPL 下载未修改的
[DE440s](https://naif.jpl.nasa.gov/pub/naif/generic_kernels/spk/planets/de440s.bsp)，然后运行：

```console
python -m pip install -r build-support/calendar/requirements.txt
python build-support/calendar/generate-solar-terms.py --kernel /path/to/de440s.bsp
```

默认逐字节核对已生成的资源、日期 CSV 和来源记录；增加 `--write` 可重新写入。
生成器在计算前检查星历 SHA256 和软件版本，不自动下载输入。

## Reproduction

Install the pinned dependencies and supply the unmodified JPL DE440s kernel.
The command above recomputes all 1,224 apparent solar-longitude crossings and
checks the UTC+08:00 dates, 160-byte resource and source record. Add `--write`
to regenerate them. Ordinary firmware builds use the checked-in resource.
