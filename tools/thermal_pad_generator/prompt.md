write a script as `tools\thermal_pad_generator\thermal_pad_generator.py`

the project involves a PCB that is mounted on the bottom of an aluminum enclosure, and I want to make a laser thermal pad that conducts the heat from the bottom of the PCB to the enclosure's bottom surface.

the goal for the script is to generate a SVG file that reads in the board design file at `electrical\hot-wand.brd` and then generates a design for a laser cut sheet that has cutouts which avoids things like through-hole pins and tall components.

the board file uses millimeter coordinates, and so should the resulting SVG file. Coordinate [0, 0] in the board file is the bottom left corner, X positive goes right, Y positive goes up. Remember that SVG uses Y positive goes down, and I want to use a 150mm x 100mm canvas, and [0, 0] of the board file should be [0, 100] on the SVG canvas. (if I have misunderstood this, then apply the appropriate method yourself)

the board file's XML has a few major sections, the big `<board>` tag contains everything, including `<libraries>` and `<elements>`, but also loose subelements that could be objects we care about, these are under `<plain>`.

there's also a `<layers>` section where it maps layer numbers to text names. The children are `<layer>` elements and they have attributes `number` and `name`. Make a dictionary out of this, with `number` as the key. Also, remember the rule that a name starting with a lowercase "t" means top, lowercase "b" means bottom, "Top" still means top and "Bottom" still means bottom, use this logic when performing a mirror transformation.

within `<libraries>` there will be children named `<library>` (identified by attribute `name`), and then under that, `<packages>`, and under that, individual `<package>` children identified by attribute `name`. These should be all parsed out and stored in a dictionary with the name as the dictionary key.

within `<elements>` there will be children as `<element>` and they will reference a package by the `package` attribute, and each element also uses attributes for their `x`, `y`, and `rot` (rotation).

for the rotation `rot` attribute, the notation `MR90` means "mirrored, rotated 90 degrees". Mirrored means essentially, if something is on a top layer, now it is on the bottom layer. `R90` means simply "rotated 90 degrees" without mirroring.

at this point, start generating the SVG

first iterate through elements of `<plain>`

## Handling of Specific Elements within `<plain>`

elements might exist within `<plain>` or within `<package>`

`<wire x1="15.5" y1="48.5" x2="15.5" y2="48.7" width="0" layer="20"/>`, translate that into a SVG path (layer 20 maps to `Dimension`, use the dictionary we generated earlier).

`<polygon>` with children `<vertex>`, like below

```
<polygon width="0.1524" layer="20" spacing="0.2032">
<vertex x="45" y="72"/>
<vertex x="45" y="61"/>
<vertex x="51" y="61"/>
<vertex x="51" y="72"/>
</polygon>
```

generate a SVG enclosed path with the data inside, use the width parameter but ignore spacing.

`<hole>` elements like `<hole x="8" y="3" drill="3"/>`, which means generate a SVG circle (0 width path) at the specified center coordinate, the `drill` attribute is the diameter of the circle.

`<pad>` elements are through hole pads that always exist on both side of the board so we don't filter by layer. Example is `<pad name="P1" x="0" y="3.8" drill="1" diameter="2.54" shape="octagon"/>`, first determine the shape, "circle" or "octagon" means we use a circle and "diameter" is the diameter of the circle. "square" means use a square centered on the coordinate and the "diameter" becomes the width. Treat all other shapes as if they are circles.

## For Every Package

Iterate through all packages from all libraries, we want to compute twi bounding rectangle for each package, one for top and one for bottom.

For each side, start tracking the left, right, top, and bottom coordinates, all 0 at the start, and we'll grow these numbers as we analyze each child of the package.

We only care about these layers

 * Top / Bottom
 * Dimension
 * Milling
 * tStop / bStop
 * tPlace / bPlace
 * tRestrict / bRestrict
 * tKeepout / bKeepout

if all bounding rectangle coordinates are 0, then this does not become SVG object later.

`<pad>` elements can manipulate the boundary using just the center coordinate and the diameter (you'd calculate a radius first) specified without caring about the shape, and remember, `pad` is on both layers. `<hole>` elements are handled the same way except `drill` size is used in the same manner as diameter.

`<rectangle>` elements describe itself using two corner coordinates as x1 y1 and x2 y2.

`<circle>` elements are described by `x`, `y`, and `radius` and `width`, use these to compute the boundary extrusion, if on the layers we care about.

`<wire>` elements can be treated like rectangles as they are described as two endpoints, x1 y1 and x2 y2.

`<smd>` elements are rectangles of copper and are described by a center coordinate x and y, with width and height as `dx` and `dy` respectively, but also they can be rotated with an optional `rot` attribute, you really only need to handle `rot="R90"` or `rot="R270"` by swapping width for height and vice versa. If `rot` actually does have a `M` inside, then just turn Top into Bottom and vice versa.

`<polygon>` on layers we care about will have `<vertex>` described by `x` and `y`.

we don't care about text even if they are on layers we care about

## Handling Each Part `<element>`

Then move on to iterating through all `<element>`.

First, an exception is the element with package `PCB-OUTLINE`, treat the contents of this as if it was inside `<plain>`.

For each element, look up the corresponding package. We already computed the bounding rectangles for each package, one for each side. Translate it using `x` and `y` and then apply the rotation, and then apply the mirroring, and after the mirroring, we only care about the bounding rectangle that ends up on the bottom. (note, I don't actually remember if we apply mirroring first before rotation, or apply rotation first and then mirror, if know how to correct the order, then correct it, and I can easily test this later too)

If the bounding rectangle is non-zero, generated a SVG rectangle with 0.2mm width lines, but expand it by 1mm to each side. The SVG element should have a metadata attribute `partref` and use the `<element>` attribute `name` for it, so I can see which rectangle is which part.

## Output the SVG

Output the SVG file, saved as `mechanical\thermal-pad-YYYYMMDDHHmmss.svg` where `YYYYMMDDHHmmss` is the date and time when the script is run.

============================

that's an ok start, thanks, but the results is showing me glaring flaws in my initial plan.

I need some big changes to the script

thanks for taking the initiative of placing things in groups. Make them layers instead, and instead of deciding what to keep and what not to keep, simply there will be layers for everything and I can manually delete a layer later. The layers:

 * PCB outline
 * stuff from `<plain>` but top layer only
 * stuff from `<plain>` but bottom layer only
 * all through hole pad solder areas
 * top mechanical
 * bottom mechanical

The strategy of using two bounding boxes is very wasteful in terms of available area for heat transfer. Keep the algorithm that calculates the bounding boxes. But, for each package, we need to also parse each through-hole pad into a list of rectangles (yes, rectangles, even if circular, use squares). So this gives each package kind of 3 virtual layers.

Then, for each package, we need boolean properties: "mechanically exists on top", "mechanically exists on bottom". These two booleans ignores existance of through-hole pads, but the existence of

 * SMD pads
 * tPlace (or bPlace)
 * tKeepout (or bKeepout)
 * tRestrict (or bRestrict)

will mark it as mechanically existing on top, or on bottom, or both, as appropriate.

Now, when iterating through `<element>`, when we consider a package, if it "mechanically exists on top" but not bottom, then we only care about the top bounding box, not the bottom bounding box, and also add in the through-hole-list-of-rectangles. Apply the translations and transformations. Use the appropriate layers.

Remove the logic of making the rectangles 1mm bigger on all sides.

All rectangles are now using 0.2mm stroke width but also filled. Give each layer a distinct color for both fill and stroke, and everything is 60% alpha transparent.
