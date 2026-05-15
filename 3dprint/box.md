# box.scad — reference notes

Working notes for `box.scad`. The .scad file has inline comments on every parameter; this doc covers the things that aren't obvious from reading parameter declarations: **design intent, parameter dependencies, and how to change common things.**

## What's in the box

Spodek (`base`):
- **Battery cradle** along -Y half of the inner space, X axis. 64 mm open U-channel + 6 mm closed ring at +X end (battery stop).
- **4 lid screw posts** in corners (M3 self-tap, Ø 7 mm, pilot 2.5 mm).
- **ESP module mounts**: 2 standoffs (Ø 5, h 5, M2.5 pilot) on +X edge of PCB + 1 central under-support (Ø 4, h 5) on -X (USB-C) edge.
- **MPU6050 standoffs**: 2 on +Y edge of PCB, shifted toward +Y wall.
- **USB-C cutout** in -X wall, centered on ESP Y.
- **Antenna hole** Ø 6.5 mm in +X wall (opposite USB-C), Z offset +5 mm above USB-C center — sedí na závit RP-SMA / SMA bulkhead konektoru.
- **Rocker switch slot** in -X wall, OPEN at top (switch slides in from above). Y offset +2 mm from USB-C center toward +Y edge.
- **Zip-tie tunnels** through cradle at 25 % and 75 % of battery length.
- **4 floor mounting holes** (Ø 3 mm wood screws to garage door, countersunk inside).

Víko (`lid`):
- Flat plate with 4 counterbored holes for M3 socket-cap (imbus) screws.
- **Lid half-cradle** (10 mm long, centered along battery length): mirrors base U-channel in the middle of the cell, drží baterii dolů v U-kanálu. Konce baterie zůstávají volné pro vodiče.
- ESP pressure pin: **odstraněn** — kolidoval by s rocker switch v -X stěně.

Build:
```
openscad -D 'part="base"' -o box-base.stl box.scad
openscad -D 'part="lid"'  -o box-lid.stl  box.scad
```
Default `part="both"` renders side-by-side preview for GUI use.

## Parameter dependencies (must hold)

These are not enforced by the .scad — if you change a parameter, verify these still hold or you'll get clashes / non-printable geometry:

1. **`inner_z` ≥ `cradle_under + batt_dia + batt_clearance + cradle_ring_ceil + 0.5`**
   — otherwise the +X ring sticks above the box top and lifts the lid. Currently `inner_z = batt_dia + 3.5` gives 0.5 mm clearance.
2. **`inner_y` ≥ `mod_y1_esp + post_intrude + 0.5`**
   — keeps the +Y corner posts from colliding with the ESP module.
3. **Zip-tie tunnel X positions** (25 / 75 % of battery length) must be clear of the centered `lid_cradle_len` (at 50 %) and the closed ring (+X end). With `batt_len = 70` a `lid_cradle_len = 10`, tunnel centers are at 22.5 / 57.5 mm a lid cradle obsazuje 35–45 mm → ~12.5 mm mezery k oběma tunelům.
4. **Lid cradle Y span = base cradle Y span** (both use `cradle_w` via `batt_axis_y_g`). If you reposition the battery in Y, both follow automatically.
5. **`screw_inset + screw_post_dia/2 ≤ wall + post_intrude_clearance`** — corner posts must remain attached to the walls. Currently `5 + 3.5 - 3 = 5.5 mm` intrusion into inner space; cradle_y0 (= 6) and other features are designed around this.
6. **Battery in cradle: clearance with -Y corner posts requires `cradle_y0 ≥ post_intrude + 0.5`** — i.e. cradle has to start 6 mm from inner -Y wall to not collide with corner posts.

## How to change common things

| You want to… | Change |
|---|---|
| Use a 14500 / different cell | `batt_len`, `batt_dia`. inner_z, inner_y will recompute. |
| Make box wall thicker/thinner | `wall` (cradle Y_offset, corner post intrusion will follow) |
| Shift MPU closer to battery | `mpu_y1 = mod_y0 + mpu_w` (was `mod_y1_esp`) |
| Use socket-cap vs flat-head lid screws | `lid_cbore_dia` and `lid_cbore_depth` (currently set for socket cap; for flat head use cone `cylinder(d1=clear, d2=head_dia, h=...)`) |
| Move rocker switch | `switch_y_offset` (along -X wall) |
| Resize rocker switch | `switch_w`, `switch_h` |
| Skip USB-C cutout (no USB exposure) | comment out the USB-C subtraction block in `base()` |
| Resize antenna hole (jiný SMA gauge) | `ant_hole_dia` (default 6.5 mm pro RP-SMA bulkhead) |
| Posunout antenni otvor vyse / nize | `ant_hole_z_lift` (default 5 mm nad USB-C Z center) |
| Different battery retention | Lid cradle (`lid_cradle_len`) and/or zip-tie tunnels (`zip_x1_g`, `zip_x2_g`) — both currently active |
| Reposition module screws (after measuring actual board) | `esp_hole_sp`, `esp_hole_inset`, `mpu_hole_sp`, `mpu_hole_inset` |
| Reposition ESP under-support (single pillar) | `esp_support_dx_from_mount`, `esp_support_dia`, `esp_support_h` |
| Floor mounting holes elsewhere | `mount_x_offset`, `mount_y_offset` (from outer wall) |

## Things marked REMEASURE

ESP-related dimensions are MEASURED from a real board. The rest are educated guesses; measure and update as needed:

- `usb_w`, `usb_h` (12 × 6 mm) — USB-C cutout; widen if needed for thick cables
- `switch_w`, `switch_h` (20 × 7 mm) — rocker switch cutout

## Assembly procedure

1. Box base, lid off, modules NOT yet attached.
2. Screw the base onto the garage door through the 4 floor holes (3 mm wood screws, countersunk inside).
3. Solder battery wires (no holder; bare-wire 18650).
4. Drop battery into cradle: tilt with +X end first, insert into closed ring section, then lower -X end into the U-channel.
5. Solder wires to rocker switch (outside the box), then slide switch into the slot in -X wall from above.
6. Optionally: thread one or two zip-ties through the tunnels, over the battery, lock on -Y side.
7. Screw ESP module to its two +X-edge standoffs (M2.5 self-tap, ~5–6 mm length).
8. Screw MPU6050 to its two +Y-edge standoffs.
9. Place lid (lid half-cradle sedí ve středu baterie). Tighten 4 M3 cap screws.

## Print notes

- **Material**: PETG recommended for the ring + lid cradle (some flex needed). PLA works but is more brittle on thin overhangs.
- **Layer height**: 0.2 mm fine for most parts; closed ring section at +X has a ~10 mm bridge at the top (battery diameter); use bridging-friendly settings.
- **Orientation**:
  - Base: flat on bed (floor down). No supports needed except inside the closed ring at +X end (small bridge, usually OK without).
  - Lid: top side up (lid cradle protrudes from underside; print needs to be flipped from default OpenSCAD render — features sticking up).
- **Walls**: 3+ perimeters recommended; screw posts and standoffs benefit from solid extrusion.
- **First print**: do a draft (0.3 mm, 2 perimeters) of just the base to verify all the components fit before printing final.

## Known TODOs / future tweaks

- Battery insertion is angled (+X end first, then drop -X end). Lid cradle is added separately, so insertion stays the same as without it.
- Zip-tie head: ensure 6+ mm of strip free between -Y wall and cradle for the head to sit; current = 6 mm (cradle_y0). If you make the cradle thicker (`cradle_side`), this shrinks.
- ~~**Status LED window in lid**~~: hotovo — průchozí Ø 4 mm díra v lid plate, řízeno parametry `led_hole_dia`, `led_x_from_mount`, `led_y_from_pcb_inner_edge`. Pozice odměřena z reálné desky.

## Box dimensions (current defaults)

- Outer: **80 × 70.1 × 28 mm** (base 25 mm + lid 3 mm)
- Inner: 74 × 64.1 × 22 mm
