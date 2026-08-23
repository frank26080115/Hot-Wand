/*
 * Hot-Wand 1:14:14 current transformer
 *
 * Units are millimetres.
 *
 * This is an assembly illustration, not a manufacturing-accurate model of
 * enamel deformation.  The turn count, winding direction, continuous wire
 * identities, and PCB-hole mapping are intentional.  The exact twist pitch
 * and the way the free leads settle are illustrative.
 *
 * Winding convention encoded here:
 *
 *   turquoise wire: S1A -> 14 turns -> S1B
 *   violet wire:    S2A -> 14 turns -> S2B
 *   yellow wire:    P1  -> one pass through the core -> P2
 *
 * Start both secondary wires at the A holes, twist the two insulated wires
 * together, and wind the resulting pair as one cable.  If each continuous
 * colored wire is inserted into its matching A/B holes, the PCB footprint
 * establishes the required electrical polarity.
 *
 * Physical polarity encoded by the PCB footprint:
 *
 *   dotted side:   P1, S1B, S2B
 *   undotted side: P2, S1A, S2A
 *
 * The positive primary-current reference is P1 -> P2.  Following either
 * secondary from A -> B, its passages through the aperture run in the
 * opposite direction.  Consequently, current entering a dotted B terminal
 * runs through the aperture in the same sense as current entering dotted P1.
 */

// ---------- Display controls ----------

show_core = true;
show_secondary_windings = true;
show_primary = true;
show_pcb = true;
show_pad_labels = true;
show_color_legend = true;

// 0 = quicker/faceted, 1 = normal, 2 = smoother/slower.
render_quality = 1;

// ---------- Physical dimensions ----------

core_outer_diameter = 16;
core_inner_diameter = 9.6;
core_thickness = 6.3;
core_edge_radius = 0.45;

wire_diameter = 0.6;
wire_radius = wire_diameter / 2;

// For a toroidal transformer, each conductor passage through the aperture is
// one electrical turn.  The model produces exactly this many passages.
secondary_turns = 14;

// The exact hand-twisted pitch is not electrically prescriptive.  This value
// makes the bifilar construction unambiguous in an instructional rendering.
pair_twists = 34;
pair_center_distance = wire_diameter * 1.04;
pair_center_offset = pair_center_distance / 2;

// Clearance between the modelled wire centreline envelope and the ferrite.
winding_clearance = 0.15;

// Lift the upright core enough for the widest orientation of the twisted pair
// to pass between it and the PCB.  In the physical assembly the wound core
// naturally rests on the pair rather than directly on the board.
core_lift_above_pcb =
    pair_center_distance + wire_diameter + winding_clearance + 0.1;

// ---------- PCB footprint dimensions ----------

pcb_thickness = 1.6;
pcb_fragment_size = [20, 16];

// ---------- Color legend layout (safe to tweak) ----------

// XY position of the first legend line.  The PCB extends from X=-10 to X=10,
// so the default X position puts the entire legend off its right edge.
color_legend_origin = [11.5, 2.0];

// Positive values place subsequent lines downward from the origin.
color_legend_line_spacing = 1.45;
color_legend_text_size = 0.82;
color_legend_text_height = 0.05;
color_legend_z = 0.07;
text_rotation = 180;

secondary_pad_diameter = 1.9304;
primary_pad_diameter = 2.54;
pad_drill = 1.0;

// These coordinates match XFORMER-K16X8X6-1:14:14 in hot-wand.lbr.
S2A = [-7.6,  4.0];
S1A = [-5.0,  4.0];
S2B = [-7.6, -4.0];
S1B = [-5.0, -4.0];
P2  = [ 0.0,  3.8];
P1  = [ 0.0, -3.8];

// ---------- Colors ----------

core_color = [0.12, 0.13, 0.15];
secondary_1_color = [0.00, 0.86, 0.66];  // turquoise
secondary_2_color = [0.56, 0.04, 1.00];  // violet
primary_color = [1.00, 0.78, 0.04];      // yellow
pcb_color = [0.04, 0.30, 0.16, 0.68];
copper_color = [0.88, 0.55, 0.16];
label_color = [0.94, 0.94, 0.94];

// ---------- Derived geometry ----------

core_outer_radius = core_outer_diameter / 2;
core_inner_radius = core_inner_diameter / 2;
core_major_radius = (core_outer_radius + core_inner_radius) / 2;
core_radial_half_width = (core_outer_radius - core_inner_radius) / 2;
core_center_height = core_outer_radius + core_lift_above_pcb;

// The secondary centreline follows a rounded-rectangle envelope around the
// approximately rectangular cross-section of the ferrite toroid.
winding_radial_radius =
    core_radial_half_width
    + pair_center_offset
    + wire_radius
    + winding_clearance;
winding_axial_radius =
    core_thickness / 2
    + pair_center_offset
    + wire_radius
    + winding_clearance;
winding_superellipse_power = 4;

// Use nearly the entire circumference so the 14 paired passages are visibly
// separated.  The remaining lower-left gap is only for the free leads.  It is
// slightly wider than the maximum projected width of the twisted pair.
winding_gap_center_angle = 235;
winding_gap_angle = 12;
winding_start_angle = winding_gap_center_angle + winding_gap_angle / 2;
winding_sweep_angle = 360 - winding_gap_angle;

// Start on the P2/A face and finish on the P1/B face.  A toroidal turn is
// counted by a passage through the aperture, not by requiring both free leads
// to finish at the same cross-section angle.  Starting at -90 degrees and
// sweeping 13.5 cross-section revolutions produces exactly 14 aperture
// passages, then leaves the finish lead naturally on the opposite face.
winding_minor_start_angle = -90;
winding_minor_sweep_angle = -(secondary_turns - 0.5) * 360;
twist_start_angle = 0;

path_segments = render_quality <= 0
    ? max(secondary_turns * 15, pair_twists * 5)
    : render_quality == 1
        ? max(secondary_turns * 24, pair_twists * 9)
        : max(secondary_turns * 34, pair_twists * 12);

wire_facets = render_quality <= 0 ? 7 : render_quality == 1 ? 10 : 14;
core_facets = render_quality <= 0 ? 96 : render_quality == 1 ? 160 : 240;
core_corner_facets = render_quality <= 0 ? 10 : render_quality == 1 ? 16 : 24;

tangent_step = 1 / path_segments;
lead_rise = 2.2;

// ---------- Vector and path functions ----------

function v_add(a, b) = [a[0] + b[0], a[1] + b[1], a[2] + b[2]];
function v_sub(a, b) = [a[0] - b[0], a[1] - b[1], a[2] - b[2]];
function v_scale(v, s) = [v[0] * s, v[1] * s, v[2] * s];
function v_dot(a, b) = a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
function v_cross(a, b) = [
    a[1] * b[2] - a[2] * b[1],
    a[2] * b[0] - a[0] * b[2],
    a[0] * b[1] - a[1] * b[0]
];
function v_length(v) = sqrt(v_dot(v, v));
function v_unit(v) = let(n = v_length(v)) n == 0 ? [0, 0, 0] : v_scale(v, 1 / n);

function clamp01(x) = min(1, max(0, x));
function signed_power(x, p) =
    x == 0 ? 0 : (x < 0 ? -1 : 1) * pow(abs(x), p);

function major_angle(u) = winding_start_angle + winding_sweep_angle * u;
function minor_angle(u) =
    winding_minor_start_angle + winding_minor_sweep_angle * u;

function radial_cross_section_offset(u) =
    winding_radial_radius
    * signed_power(cos(minor_angle(u)), 2 / winding_superellipse_power);

function axial_cross_section_offset(u) =
    winding_axial_radius
    * signed_power(sin(minor_angle(u)), 2 / winding_superellipse_power);

// Local toroid coordinates: the ring lies in XY and its thickness is along Z.
function winding_center_local(u) =
    let(
        phi = major_angle(u),
        radius = core_major_radius + radial_cross_section_offset(u)
    )
    [
        radius * cos(phi),
        radius * sin(phi),
        axial_cross_section_offset(u)
    ];

// Rotate the toroid upright and place it above the PCB.  After this transform,
// the ring lies in XZ and the toroid thickness is along Y.
function local_to_board(p) = [p[0], -p[2], p[1] + core_center_height];
function winding_center(u) = local_to_board(winding_center_local(u));

function winding_surface_normal(u) =
    let(
        phi = major_angle(u),
        radial = radial_cross_section_offset(u),
        axial = axial_cross_section_offset(u)
    )
    v_unit([
        radial * cos(phi),
        -axial,
        radial * sin(phi)
    ]);

function winding_tangent(u) =
    v_unit(v_sub(
        winding_center(clamp01(u + tangent_step)),
        winding_center(clamp01(u - tangent_step))
    ));

// Create a stable local frame normal to the bundle path, then rotate each
// conductor around the bundle centreline.  The 180-degree wire-index offset
// keeps the two insulated conductors on opposite sides of the twisted pair.
function secondary_wire_point(u, wire_index) =
    let(
        centre = winding_center(u),
        tangent = winding_tangent(u),
        raw_normal = winding_surface_normal(u),
        normal = v_unit(v_sub(
            raw_normal,
            v_scale(tangent, v_dot(raw_normal, tangent))
        )),
        binormal = v_unit(v_cross(tangent, normal)),
        twist_angle = twist_start_angle
            + pair_twists * 360 * u
            + wire_index * 180,
        offset_direction = v_add(
            v_scale(normal, cos(twist_angle)),
            v_scale(binormal, sin(twist_angle))
        )
    )
    v_add(centre, v_scale(offset_direction, pair_center_offset));

// ---------- Geometry helpers ----------

module polyline_tube(points, radius, facets = 10) {
    for (i = [0 : len(points) - 2]) {
        hull() {
            translate(points[i])
                sphere(r = radius, $fn = facets);
            translate(points[i + 1])
                sphere(r = radius, $fn = facets);
        }
    }
}

module ferrite_core() {
    color(core_color)
        translate([0, 0, core_center_height])
            rotate([90, 0, 0])
                rotate_extrude($fn = core_facets, convexity = 10)
                    hull()
                        for (r = [
                            core_inner_radius + core_edge_radius,
                            core_outer_radius - core_edge_radius
                        ])
                            for (z = [
                                -core_thickness / 2 + core_edge_radius,
                                 core_thickness / 2 - core_edge_radius
                            ])
                                translate([r, z])
                                    circle(
                                        r = core_edge_radius,
                                        $fn = core_corner_facets
                                    );
}

module secondary_wire(wire_index, start_pad, finish_pad, wire_color) {
    winding_points = [
        for (i = [0 : path_segments])
            secondary_wire_point(i / path_segments, wire_index)
    ];

    first_point = winding_points[0];
    last_point = winding_points[len(winding_points) - 1];

    start_route = [
        [start_pad[0], start_pad[1], -pcb_thickness - 0.5],
        [start_pad[0], start_pad[1], lead_rise],
        first_point
    ];

    winding_middle = [
        for (i = [1 : len(winding_points) - 2]) winding_points[i]
    ];

    finish_route = [
        last_point,
        [finish_pad[0], finish_pad[1], lead_rise],
        [finish_pad[0], finish_pad[1], -pcb_thickness - 0.5]
    ];

    color(wire_color)
        polyline_tube(
            concat(start_route, winding_middle, finish_route),
            wire_radius,
            wire_facets
        );
}

module primary_wire() {
    // A single conductor passes through the toroid aperture once.  This list
    // follows the positive RF-current reference from dotted P1 to P2.
    primary_points = [
        [P1[0], P1[1], -pcb_thickness - 0.5],
        [P1[0], P1[1], core_center_height],
        [P2[0], P2[1], core_center_height],
        [P2[0], P2[1], -pcb_thickness - 0.5]
    ];

    color(primary_color)
        polyline_tube(primary_points, wire_radius, wire_facets);
}

all_pads = [
    ["S2A", S2A, secondary_pad_diameter],
    ["S1A", S1A, secondary_pad_diameter],
    ["S2B", S2B, secondary_pad_diameter],
    ["S1B", S1B, secondary_pad_diameter],
    ["P2",  P2,  primary_pad_diameter],
    ["P1",  P1,  primary_pad_diameter]
];

module plated_pad(position, outside_diameter) {
    color(copper_color)
        translate([position[0], position[1], 0.01])
            difference() {
                cylinder(h = 0.05, d = outside_diameter, $fn = 32);
                translate([0, 0, -0.01])
                    cylinder(h = 0.07, d = pad_drill, $fn = 24);
            }
}

module pad_label(label, position) {
    color(label_color)
        translate([position[0], position[1], 0.07])
            rotate([0, 0, text_rotation])
                linear_extrude(height = 0.05)
                    text(
                        label,
                        size = 0.72,
                        halign = "center",
                        valign = "center"
                    );
}

module color_legend_line(label, line_number, wire_color) {
    color(wire_color)
        translate([
            color_legend_origin[0],
            color_legend_origin[1] - line_number * color_legend_line_spacing,
            color_legend_z
        ])
            rotate([0, 0, text_rotation])
                linear_extrude(height = color_legend_text_height)
                    text(
                        label,
                        size = color_legend_text_size,
                        halign = "right",
                        valign = "center"
                    );
}

module color_legend() {
    color_legend_line("S1: turquoise", 0, secondary_1_color);
    color_legend_line("S2: violet", 1, secondary_2_color);
    color_legend_line("Primary: yellow", 2, primary_color);
}

module pcb_fragment() {
    color(pcb_color)
        difference() {
            translate([
                -pcb_fragment_size[0] / 2,
                -pcb_fragment_size[1] / 2,
                -pcb_thickness
            ])
                cube([
                    pcb_fragment_size[0],
                    pcb_fragment_size[1],
                    pcb_thickness
                ]);

            for (pad = all_pads)
                translate([
                    pad[1][0],
                    pad[1][1],
                    -pcb_thickness - 0.1
                ])
                    cylinder(h = pcb_thickness + 0.2, d = pad_drill, $fn = 24);
        }

    for (pad = all_pads)
        plated_pad(pad[1], pad[2]);

    if (show_pad_labels) {
        pad_label("S2A", [-7.6,  5.35]);
        pad_label("S1A", [-5.0,  5.35]);
        pad_label("P2",  [ 0.0,  5.20]);
        pad_label("S2B *", [-7.6, -5.35]);
        pad_label("S1B *", [-5.0, -5.35]);
        pad_label("P1 *",  [ 0.0, -5.20]);

    }
}

// ---------- Assembly ----------

if (show_pcb)
    pcb_fragment();

if (show_color_legend)
    color_legend();

if (show_core)
    ferrite_core();

if (show_secondary_windings) {
    // Assign the pair sides to preserve the left-to-right pad order at both
    // ends.  Reversing these indices creates a needless crossover in the free
    // leads even though the electrical connections remain continuous.
    secondary_wire(1, S1A, S1B, secondary_1_color);
    secondary_wire(0, S2A, S2B, secondary_2_color);
}

if (show_primary)
    primary_wire();
