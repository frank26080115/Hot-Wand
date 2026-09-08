/*
 * Coreless inductor L8 winding post
 *
 * Units are millimetres. This is a disposable winding mandrel: wind ten
 * turns of 22 AWG magnet wire in the spiral groove, then cut or snap the
 * 2 mm neck to remove the square footing.
 */

// ---------- Winding adjustment ----------

// IMPORTANT: Change this value to tune the winding spacing (for example,
// 1.05 or 1.1). This is the vertical rise of one complete spiral turn.
winding_pitch = 1.0;

// ---------- Physical dimensions ----------

footing_size = 20;
footing_thickness = 0.8;

neck_diameter = 2.0;
post_diameter = 5.4;
post_height = 12;
cone_side_angle = 45;

// The groove is cut by a complete 0.8 mm-diameter circular tube following
// the requested 5.4 mm-diameter helical path.
wire_diameter = 0.8;
spiral_path_diameter = 5.4;

// Higher values make the helical groove smoother but take longer to render.
segments_per_turn = 32;
wire_facets = 24;
post_facets = 96;

// ---------- Derived dimensions ----------

wire_radius = wire_diameter / 2;
spiral_path_radius = spiral_path_diameter / 2;

// OpenSCAD trigonometric functions use degrees. At 45 degrees, the cone
// height is the same as the change in radius: (5.8 - 2.0) / 2 = 1.9 mm.
cone_height =
    (post_diameter - neck_diameter) / 2 / tan(cone_side_angle);
cone_bottom_z = footing_thickness;
cone_top_z = cone_bottom_z + cone_height;
post_top_z = cone_top_z + post_height;

// Begin below the first possible cone intersection and finish above the top
// edge so the same uninterrupted helix cleanly opens at both ends.
spiral_start_z = cone_bottom_z;
spiral_end_z = post_top_z + wire_radius;
spiral_height = spiral_end_z - spiral_start_z;
spiral_segments = ceil(
    spiral_height / winding_pitch * segments_per_turn
);

function spiral_point(index) =
    let(
        z = spiral_start_z
            + spiral_height * index / spiral_segments,
        angle = 360 * (z - spiral_start_z) / winding_pitch
    )
    [
        spiral_path_radius * cos(angle),
        spiral_path_radius * sin(angle),
        z
    ];

// ---------- Geometry ----------

module footing() {
    translate([-footing_size / 2, -footing_size / 2, 0])
        cube([footing_size, footing_size, footing_thickness]);
}

module winding_post() {
    union() {
        translate([0, 0, cone_bottom_z])
            cylinder(
                h = cone_height,
                d1 = neck_diameter,
                d2 = post_diameter,
                $fn = post_facets
            );

        translate([0, 0, cone_top_z])
            cylinder(
                h = post_height,
                d = post_diameter,
                $fn = post_facets
            );
    }
}

module round_spiral_cutter() {
    for (i = [0 : spiral_segments - 1]) {
        hull() {
            translate(spiral_point(i))
                sphere(r = wire_radius, $fn = wire_facets);
            translate(spiral_point(i + 1))
                sphere(r = wire_radius, $fn = wire_facets);
        }
    }
}

module spiral_groove() {
    round_spiral_cutter();
}

union() {
    footing();

    difference() {
        winding_post();
        spiral_groove();
    }
}
