// Honeycomb speaker grill for Faital Pro 3FE22 (3" driver)
// Dimensions from 3FE22_drawing_4.pdf:
//   mounting holes: 4x dia 4.5 on dia 92 bolt circle, at 45deg diagonals
//   flange (ear-to-ear) dia 105.6, frame body dia 81

$fn = 96;

// --- driver dimensions ---
flange_d    = 105.6;
bolt_circle = 92;
bolt_hole_d = 4.5;

// --- grill parameters ---
plate_t     = 2.5;     // plate thickness
open_d      = 76;     // diameter of the honeycomb area (frame body is dia 81)
cell_d      = 7;      // hex cell size (flat-to-flat)
wall        = 1.4;    // wall between cells
bevel       = 2;      // chamfer on the front outer edge
csink       = true;   // countersink screw heads from the front
csink_d     = 8;

pitch = cell_d + wall;   // centre-to-centre distance between neighbouring cells

// hex cell (flat-to-flat = cell_d) as 2D; circle with $fn=6 uses circumradius
module hex() { circle(d = cell_d / cos(30), $fn = 6); }

// hex grid clipped to the round opening: edge cells become partial hexes.
// A cell is kept only if its centre is inside the circle (at least ~half of
// it remains), which avoids tiny slivers.
module honeycomb_2d() {
    row_h = pitch * sqrt(3) / 2;
    n = ceil(open_d / pitch / 2) + 2;
    intersection() {
        circle(d = open_d);
        for (r = [-n : n], c = [-n : n]) {
            x = (c + (abs(r) % 2 == 0 ? 0 : 0.5)) * pitch;
            y = r * row_h;
            if (norm([x, y]) <= open_d / 2)
                translate([x, y]) rotate(30) hex();
        }
    }
}

module grill() {
    difference() {
        hull() {
            cylinder(d = flange_d, h = plate_t - bevel);
            cylinder(d = flange_d - 2 * bevel, h = plate_t);
        }

        // honeycomb openings
        translate([0, 0, -1]) linear_extrude(plate_t + 2) honeycomb_2d();

        // mounting holes
        for (a = [45, 135, 225, 315])
            rotate(a) translate([bolt_circle / 2, 0, 0]) {
                translate([0, 0, -1]) cylinder(d = bolt_hole_d, h = plate_t + 2);
                if (csink)
                    translate([0, 0, plate_t - 1.5]) cylinder(d1 = bolt_hole_d, d2 = csink_d, h = 1.51);
            }
    }
}

grill();
