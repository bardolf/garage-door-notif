// box.scad — Enclosure for garage-door-notif
//   18650 battery (lower row), ESP module + MPU6050 (upper row, side by side).
//   M3 self-tap screws into 4 corner posts; flat-head countersunk in lid.
//
// Render in CLI:
//   openscad -D 'part="base"' -o box-base.stl box.scad
//   openscad -D 'part="lid"'  -o box-lid.stl  box.scad

part = "both";  // "base" | "lid" | "both"  (override with -D)

// ===== shell =====
wall          = 3.0;
floor_thk     = 3.0;
lid_thk       = 3.0;

// ===== battery (18650) =====
batt_len           = 70.0;
batt_dia           = 18.5;
batt_clearance     = 0.5;     // radial slop
batt_ring_len      = 10.0;    // closed ring section at far end (battery stop)
cradle_side        = 2.0;     // side wall outside battery diameter
cradle_under       = 1.0;     // material between cradle bottom and box floor
cradle_ring_ceil   = 1.5;     // material above battery in ring section

// ===== ESP module =====
esp_l           = 34.5;       // along X, USB-C on -X short edge
esp_w           = 25.6;       // along Y
esp_hole_sp     = 20.0;       // hole spacing on +X short edge (along Y) — MEASURED
esp_hole_inset  = 2.5;        // hole inset from +X edge
esp_pcb_thk     = 1.6;

// ===== MPU6050 =====
mpu_l           = 21.0;       // along X
mpu_w           = 15.0;       // along Y
mpu_hole_sp     = 15.0;       // hole spacing on +Y long edge (along X) — MEASURED
mpu_hole_inset  = 2.5;        // hole inset from +Y edge

// ===== module standoffs (M2.5 self-tap) =====
standoff_dia        = 5.0;
standoff_h          = 5.0;
standoff_pilot      = 2.1;
standoff_pilot_depth = 5.0;

// ===== lid screws (M3 socket cap / imbus, self-tap into corner posts) =====
screw_post_dia      = 7.0;
screw_post_pilot    = 2.5;
screw_post_depth    = 12.0;
screw_inset         = 5.0;    // from outer corner to screw axis
lid_clear           = 3.4;    // through hole for M3 shaft
lid_cbore_dia       = 6.0;    // counterbore Ø for M3 cap head (head Ø 5.5)
lid_cbore_depth     = 2.0;    // partial recess (M3 cap head is 3.0 mm; ~1 mm sticks out)

// ===== USB-C cutout (in -X wall, centered on ESP USB-C) =====
usb_w           = 12.0;
usb_h           = 6.0;

// ===== floor mounting holes (4× Ø 3 mm wood screw, countersunk inside the box) =====
// Box is screwed to the garage door from the inside (lid off, modules removed).
mount_screw_clear = 3.3;       // shaft clearance hole
mount_cs_dia      = 6.0;       // countersink Ø at inner floor surface
mount_cs_depth    = 1.5;       // depth (suits 90° flat-head wood screw)
mount_x_offset    = 14.0;      // hole center distance from outer X wall
mount_y_offset    = 6.0;       // hole center distance from outer Y wall

// ===== rocker switch (slot in -X wall next to USB-C, OPEN at top) =====
// Switch body protrudes in +X direction into the module zone (above battery),
// so even a deep switch body won't collide with the 18650.
// Solder wires first, then drop switch into the slot from above; lid closes it.
switch_w        = 20.0;       // cutout width along Y (along the -X wall)
switch_h        = 7.0;        // cutout height along Z (slot opens to top)
switch_y_offset = 0;          // shift from USB-C Y center (mm); + = toward +Y

// ===== lid half-cradle at -X (wire) end of battery — mirrors base U-channel =====
// Fully encircles the battery in this section + acts as orientation key vs +X ring.
lid_cradle_len   = 10.0;

// ===== variant C: zip-tie tunnels through cradle (under battery) =====
zip_tunnel_w     = 4.5;       // X width (zip-tie body)
zip_tunnel_h     = 3.0;       // Z height
zip_tunnel_floor_gap = 0.5;   // gap above the box floor where the tunnel bottom sits

// ===== ESP under-support (single central pillar, USB-C side, non-threaded) =====
esp_support_dia            = 4.0;   // MEASURED
esp_support_h              = 5.0;   // same as mounting standoff_h — MEASURED
esp_support_dx_from_mount  = 26.0;  // X dist from ESP mount hole (toward USB-C) — MEASURED

// ===== ESP lid pressure pin (single, protects PCB from USB-C insertion force) =====
esp_lid_pin_dia  = 4.0;       // MEASURED
esp_lid_pin_dx   = 31.0;      // X dist from mount hole to pin (toward USB-C) — MEASURED
esp_lid_pin_gap  = 5.5;       // Z gap between under-support top and lid pin bottom — MEASURED

// ===== LED viewing hole in lid (NeoPixel on ESP module shines through) =====
led_hole_dia              = 4.0;   // priezor pro indikacni RGB LED
led_x_from_mount          = 18.0;  // X dist from ESP mount-hole line (toward USB-C) — MEASURED
led_y_from_pcb_inner_edge = 6.0;   // Y dist from ESP PCB edge nearest to battery — MEASURED

// ===== gaps =====
mod_gap_y       = 3.0;        // cradle <-> modules
mod_gap_x       = 4.0;        // ESP <-> MPU

$fa = 2; $fs = 0.4;
EPS = 0.01;

// ===== derived layout (inner coordinates) =====
// post_intrude: how far a corner post sticks into the inner cavity
post_intrude = screw_inset + screw_post_dia/2 - wall;

cradle_y0    = post_intrude + 0.5;
cradle_w     = 2*cradle_side + 2*batt_clearance + batt_dia;
cradle_y1    = cradle_y0 + cradle_w;
batt_axis_z  = cradle_under + batt_dia/2;

mod_y0       = cradle_y1 + mod_gap_y;
mod_y1_esp   = mod_y0 + esp_w;
// MPU shifted to +Y side of module zone (flush with ESP +Y edge) — gives more
// wire room between battery and MPU, and offsets the standoffs from the middle.
// To bias MPU toward the battery instead, set mpu_y1 = mod_y0 + mpu_w.
mpu_y1       = mod_y1_esp;
mpu_y0       = mpu_y1 - mpu_w;

esp_x0       = 2.0;
esp_x1       = esp_x0 + esp_l;
mpu_x0       = esp_x1 + mod_gap_x;
mpu_x1       = mpu_x0 + mpu_l;

esp_hole_x   = esp_x1 - esp_hole_inset;
esp_hole_y1  = mod_y0 + (esp_w - esp_hole_sp)/2;
esp_hole_y2  = esp_hole_y1 + esp_hole_sp;

mpu_hole_y   = mpu_y1 - mpu_hole_inset;
mpu_hole_x1  = mpu_x0 + (mpu_l - mpu_hole_sp)/2;
mpu_hole_x2  = mpu_hole_x1 + mpu_hole_sp;

inner_x = max(batt_len + 4, mpu_x1 + 2);
inner_y = mod_y1_esp + post_intrude + 0.5;
inner_z = batt_dia + 3.5;   // 1 mm clearance over the +X ring section + 0.5 mm to lid

outer_x      = inner_x + 2*wall;
outer_y      = inner_y + 2*wall;
outer_z_base = inner_z + floor_thk;

// USB-C placement (height = top of PCB + connector half-height)
usb_z_center = floor_thk + standoff_h + esp_pcb_thk + 1.5;
usb_y_center = wall + mod_y0 + esp_w/2;

// Battery axis in global coords (for lid cradle + zip-tie tunnels)
batt_x_start_g = wall + 2;
batt_axis_y_g  = wall + cradle_y0 + cradle_w/2;
batt_axis_z_g  = floor_thk + batt_axis_z;
batt_top_z_g   = batt_axis_z_g + batt_dia/2;
// zip-tie tunnel X positions along battery length (5 mm clear of lid cradle / +X ring)
zip_x1_g       = batt_x_start_g + batt_len * 0.25;
zip_x2_g       = batt_x_start_g + batt_len * 0.75;

// ESP under-support (single pillar on ESP Y center)
esp_center_y_g   = wall + mod_y0 + esp_w/2;
esp_support_x_g  = wall + esp_hole_x - esp_support_dx_from_mount;

// ESP lid pressure pin (single, on ESP Y center)
esp_lid_pin_x_g  = wall + esp_hole_x - esp_lid_pin_dx;
// pin length: spans from lid bottom down to (under-support top + gap)
esp_lid_pin_h    = outer_z_base - floor_thk - esp_support_h - esp_lid_pin_gap;

// LED viewing hole (through lid, above NeoPixel on ESP module)
led_hole_x_g     = wall + esp_hole_x - led_x_from_mount;
led_hole_y_g     = wall + mod_y0 + led_y_from_pcb_inner_edge;

// ===== modules =====

module battery_cradle() {
  open_len = batt_len - batt_ring_len;

  // Open U-channel: top sliced off at battery equator
  difference() {
    cube([open_len, cradle_w, batt_axis_z]);
    translate([-EPS, cradle_w/2, batt_axis_z])
      rotate([0, 90, 0])
        cylinder(d = batt_dia + 2*batt_clearance, h = open_len + 2*EPS);
  }

  // Closed ring: full cylinder cavity, encloses far end of battery
  ring_top_z = batt_axis_z + batt_dia/2 + batt_clearance + cradle_ring_ceil;
  translate([open_len, 0, 0])
    difference() {
      cube([batt_ring_len, cradle_w, ring_top_z]);
      translate([-EPS, cradle_w/2, batt_axis_z])
        rotate([0, 90, 0])
          cylinder(d = batt_dia + 2*batt_clearance, h = batt_ring_len + 2*EPS);
    }
}

module standoff() {
  difference() {
    cylinder(d = standoff_dia, h = standoff_h);
    translate([0, 0, standoff_h - standoff_pilot_depth + EPS])
      cylinder(d = standoff_pilot, h = standoff_pilot_depth);
  }
}

module corner_post() {
  difference() {
    cylinder(d = screw_post_dia, h = inner_z);
    translate([0, 0, inner_z - screw_post_depth + EPS])
      cylinder(d = screw_post_pilot, h = screw_post_depth);
  }
}

module lid_screw_hole() {
  translate([0, 0, -EPS])
    cylinder(d = lid_clear, h = lid_thk + 2*EPS);
  translate([0, 0, lid_thk - lid_cbore_depth])
    cylinder(d = lid_cbore_dia, h = lid_cbore_depth + EPS);
}

// ===== parts =====

module base() {
  difference() {
    union() {
      // Hollow shell
      difference() {
        cube([outer_x, outer_y, outer_z_base]);
        translate([wall, wall, floor_thk])
          cube([inner_x, inner_y, inner_z + EPS]);
      }

      // Corner posts (4)
      for (px = [screw_inset, outer_x - screw_inset])
        for (py = [screw_inset, outer_y - screw_inset])
          translate([px, py, floor_thk]) corner_post();

      // Battery cradle (X start = inner 2, Y start = cradle_y0)
      translate([wall + 2, wall + cradle_y0, floor_thk])
        battery_cradle();

      // ESP standoffs (2, on +X edge of ESP PCB)
      for (sy = [esp_hole_y1, esp_hole_y2])
        translate([wall + esp_hole_x, wall + sy, floor_thk]) standoff();

      // MPU standoffs (2, on +Y edge of MPU PCB)
      for (sx = [mpu_hole_x1, mpu_hole_x2])
        translate([wall + sx, wall + mpu_hole_y, floor_thk]) standoff();

      // ESP under-support (USB-C side, single central pillar, no pilot hole)
      translate([esp_support_x_g, esp_center_y_g, floor_thk])
        cylinder(d = esp_support_dia, h = esp_support_h);
    }

    // USB-C cutout through -X wall
    translate([-EPS, usb_y_center - usb_w/2, usb_z_center - usb_h/2])
      cube([wall + 2*EPS, usb_w, usb_h]);

    // Rocker-switch slot through -X wall (next to USB-C), OPEN at top of wall
    switch_y_center = usb_y_center + switch_y_offset;
    translate([-EPS, switch_y_center - switch_w/2, outer_z_base - switch_h])
      cube([wall + 2*EPS, switch_w, switch_h + EPS]);

    // Floor mounting holes for Ø3 mm wood screws (to garage door)
    for (mx = [mount_x_offset, outer_x - mount_x_offset])
      for (my = [mount_y_offset, outer_y - mount_y_offset]) {
        translate([mx, my, -EPS])
          cylinder(d = mount_screw_clear, h = floor_thk + 2*EPS);
        translate([mx, my, floor_thk - mount_cs_depth])
          cylinder(d1 = mount_screw_clear, d2 = mount_cs_dia,
                   h = mount_cs_depth + EPS);
      }

    // Zip-tie tunnels through cradle (Y-direction, just above box floor)
    cradle_y0_g = wall + cradle_y0;
    for (tx = [zip_x1_g, zip_x2_g])
      translate([tx - zip_tunnel_w/2,
                 cradle_y0_g - EPS,
                 floor_thk + zip_tunnel_floor_gap])
        cube([zip_tunnel_w, cradle_w + 2*EPS, zip_tunnel_h]);
  }
}

module lid() {
  difference() {
    union() {
      cube([outer_x, outer_y, lid_thk]);

      // Lid half-cradle: 10 mm long block at -X end of battery with a half-cyl cut
      // (mirrors the base U-channel; together with +X ring fully encircles battery)
      lid_cradle_drop = outer_z_base - batt_axis_z_g;  // how far cradle hangs below lid
      difference() {
        translate([batt_x_start_g, batt_axis_y_g - cradle_w/2, -lid_cradle_drop])
          cube([lid_cradle_len, cradle_w, lid_cradle_drop]);
        translate([batt_x_start_g - EPS, batt_axis_y_g, -lid_cradle_drop])
          rotate([0, 90, 0])
            cylinder(d = batt_dia + 2*batt_clearance,
                     h = lid_cradle_len + 2*EPS);
      }

      // ESP pressure pin (single, protects PCB from USB-C insertion force)
      translate([esp_lid_pin_x_g, esp_center_y_g, -esp_lid_pin_h])
        cylinder(d = esp_lid_pin_dia, h = esp_lid_pin_h);
    }
    // Through screw holes with counterbore
    for (px = [screw_inset, outer_x - screw_inset])
      for (py = [screw_inset, outer_y - screw_inset])
        translate([px, py, 0]) lid_screw_hole();

    // LED viewing hole — průchozí, nad NeoPixelem na ESP modulu
    translate([led_hole_x_g, led_hole_y_g, -EPS])
      cylinder(d = led_hole_dia, h = lid_thk + 2*EPS);
  }
}

// ===== top-level =====
if      (part == "base") base();
else if (part == "lid")  lid();
else { base(); translate([outer_x + 15, 0, 0]) lid(); }

echo(str("Outer: ", outer_x, " x ", outer_y, " x ", outer_z_base, " mm (base only)"));
echo(str("Inner: ", inner_x, " x ", inner_y, " x ", inner_z, " mm"));
