The previous effort to automatically generate a massive thermal pad under the PCB does work but it is not practical in real life.

I've simplified the task. Inside `electrical\hot-wand.brd` there is a layer "cooling" as layer number 254. On the board I've simply defined a few polygons.

I want a python script `tools\thermal_pads_extractor\thermal_pads_extractor.py` to pull those polygons out and put them all into one SVG file, saved as `mechanical\thermal-pads-YYYYMMDDHHmmss.svg` where `YYYYMMDDHHmmss` is the date and time when the script is run.

The SVG enclosed paths are to be using no fill and 0.2mm stroke width.

The board file uses millimeter coordinates, and so should the resulting SVG file. Coordinate [0, 0] in the board file is the bottom left corner, X positive goes right, Y positive goes up. Remember that SVG uses Y positive goes down, and I want to use a 150mm x 100mm canvas, and [0, 0] of the board file should be [0, 100] on the SVG canvas. (if I have misunderstood this, then apply the appropriate method yourself)

Only process children under the `<plain>` element, the polygons that are on the relevant layer.
