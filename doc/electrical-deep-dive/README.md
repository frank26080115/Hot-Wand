# Interactive schematic guides

Open either generated HTML page through a static website:

- [Hot-Wand, 13.56 MHz](electrical-deep-dive-13mhz.html)
- [Hot-Wand Lite, 470 kHz](electrical-deep-dive-lite-470khz.html)

The Markdown guides and EAGLE schematics are the source. Do not edit the generated HTML or tiles. Viewer behavior and styling live in `assets/deep-dive.js` and `assets/deep-dive.css`.

## Generate

From the repository root, with Python 3.10 or newer:

```powershell
python -m pip install -r tools/requirements-deep-dive.txt
python tools/generate_deep_dive.py
```

The first build requires EAGLE 7.6's `eaglecon.exe`, using the same export helper as the schematic PDF tool. Its default location is `C:\ProgramFiles\EAGLE-7.6.0\bin\eaglecon.exe`. Set `EAGLECON_EXE` or pass `--eaglecon PATH` if yours differs.

The default is 400 DPI. `--variant 13mhz` or `--variant lite-470khz` builds one page. `--dpi 300` changes the resolution, and `--force-render` bypasses the image cache. Rendering uses temporary schematic copies with registration marks; original schematics are never rewritten.

Rendering caches and full-size intermediate images live in the ignored `temp` directory. Markdown-only changes reuse cached imagery; they do not need EAGLE while the cache remains valid. Deleting `temp` is safe but makes the next build render again. Generated HTML and tiles are kept in Git.

Each component gets a hotspot derived from embedded symbol geometry. Grouped Markdown headings share content while retaining individual component hotspots. Large schematic titles map to Markdown's top-level sections; `Fan CTRL` is explicitly mapped to `Fan Control`. New unmatched sections or unsupported geometry produce a build error. A real component with no entry produces a warning and a metadata fallback, with its parent inferred from the nearest documented component.

## Publish

Copy this entire directory to your website by FTP. HTML, `assets`, and `tiles` must retain their relative paths. The generated pages contain all explanations and configuration; they need no backend or CDN. You may omit `temp` when uploading.

Links to supporting repository documents point to GitHub's `master` branch and open a new tab. Internet access is only needed when following these outbound links.

## Check changes

```powershell
python -m unittest discover -s tools -p test_generate_deep_dive.py
python -m pip install playwright
python -m playwright install chromium
python tools/test_deep_dive_viewer.py
```

The browser checks serve a temporary copy containing only the deployable HTML, assets, and tiles, then exercise both guides on desktop and touch-sized screens. Screenshots are saved under `temp/browser-checks` for visual inspection.

## Third-party software

`assets/vendor/openseadragon` contains OpenSeadragon **6.1.1**, distributed under the BSD 3-Clause license included in `LICENSE.txt`. The vendored minified distribution was obtained from [cdnjs](https://cdnjs.cloudflare.com/ajax/libs/openseadragon/6.1.1/openseadragon.min.js); its header identifies release commit `201d4783`. Project source and release notes are available from [OpenSeadragon](https://github.com/openseadragon/openseadragon/releases/tag/v6.1.1). No third-party server is contacted by the viewer.
