/**

Charl Linssen <charl@itfromb.it>, December 2021

Inner hub needs to tapped internally at M3 to accept threaded rod. Rod is secured to the hub by one additional nut on top of the hub (towards the front of the clock face).

Outer hub needs to be tapped externally (at the front of the clock) at M5 to allow clock hand to be mounted (clamped between two thin M5 nuts).

**/

include <NopSCADlib/core.scad>
use <NopSCADlib/utils/thread.scad>


/**
 * OpenSCAD rendering parameters
**/

pi = 3.14159;

quality_factor = 10;

$fn = quality_factor * 10;
$fn2 = quality_factor * 10;

bevel_resolution = quality_factor;


/**
 * which parts to draw (set to 0 or 1)
**/

draw_motors = 0;
draw_mounting = 0;


/**
 * margins
**/

epsilon = .1; // [mm]
motor_axis_insert_margin = .1; // [mm]


/**
 * model parameters for BLDC motors
**/

r_eccentr_holes_bldc_stator = 19.1/2;  // [mm]
r_eccentr_holes_bldc_rotor = 12.3/2;  // [mm]
n_holes_bldc_stator = 4;
deg_per_hole = 360/n_holes_bldc_stator;
D_bldc_motor = 34.56; // diameter of the motor itself [mm]
h_bldc_motor = 14.22; // height of the motor itself [mm]
D_bldc_motor_cutout = 5; // diameter of the hollow axis of the BLDC motor [mm]
D_bldc_rotor_cutout = 15;  // cutout of the mounting plane for the rotor+hub to pass through [mm]


/**
 * model parameters for the hubs
**/

hub_base_thickness = 1.5; // [mm]
D_bldc_stator_holes = 2.7;   // diameter of the holes for mounting it to the motor [mm] (screw thread is M2.5)

h_outer_hub = 18; // [mm]
D_outer_hub_outer = 5; // diameter of the outer clock hand [mm]

h_inner_hub = 37; // [mm]
D_inner_hub_outer = 3; // diameter of the inner clock hand [mm]

inner_hub_h_motor_insert = 4; // length by which the rear end of the rotating plate sticks into the motor
outer_hub_h_motor_insert = .5; // length by which the rear end of the rotating plate sticks into the motor

outer_hub_thread_height = 12.5;


/**
 * model parameters for mounting plane standoffs
**/

h_standoffs = 18; // [mm]

D_standoffs_inner = 4; // [mm]
D_standoffs_outer = 7; // [mm]
r_ecc_standoffs = 25; // [mm]

h_thin_M4_nut = 2.1;  // [mm]
r_hub_cutout = 15/2; // [mm] XXX: was 4

z_second_platform = hub_base_thickness + h_standoffs;


// ----------------------------------------------------------------------------------------------------


/**
 * inner hub
**/

color([.5, .7, .7])
translate([0., 0., hub_base_thickness + h_bldc_motor])
union() {
    // base
    difference() {
        union() {
            cylinder(h=hub_base_thickness, r=r_eccentr_holes_bldc_rotor + D_bldc_stator_holes);

            // part that sticks out the back
            translate([0,0,-inner_hub_h_motor_insert])
            cylinder(h=inner_hub_h_motor_insert + epsilon, r=D_bldc_motor_cutout/2 - motor_axis_insert_margin);
        }
        
        union() {
            translate([0, 0, -inner_hub_h_motor_insert - epsilon])
            cylinder(h=inner_hub_h_motor_insert + h_inner_hub + 2*epsilon, r=D_inner_hub_outer/2 + 2*epsilon);   // XXX: slight bit of extra cutout for the thread

            // holes for rotor mounting screws
            for (hole_idx = [0:(n_holes_bldc_stator-1)]) {
                rotate(a=hole_idx * deg_per_hole, v=[0, 0, 1])
                translate([0., r_eccentr_holes_bldc_rotor, -epsilon])
                cylinder(h=hub_base_thickness + 2*epsilon, r=D_bldc_stator_holes/2);
            }
        }
    }
    translate([0, 0, -inner_hub_h_motor_insert+ (inner_hub_h_motor_insert + hub_base_thickness) / 2])
    female_metric_thread(3, metric_coarse_pitch(3), inner_hub_h_motor_insert + hub_base_thickness);
}


/**
 * outer hub
**/

color([.3, .5, .7])
translate([0., 0., z_second_platform + hub_base_thickness + h_bldc_motor])
difference() {
    union() {
        cylinder(h=h_outer_hub, r=D_outer_hub_outer/2 - .43);

        intersection() {
            difference() {
                translate([0., 0., outer_hub_thread_height/2 + h_outer_hub - outer_hub_thread_height])
                male_metric_thread(5, metric_coarse_pitch(5), outer_hub_thread_height);
        
                cylinder(h=h_outer_hub, r=D_outer_hub_outer/2 - .43 - 2 * epsilon);
            }
            cylinder(h=h_outer_hub, r=D_outer_hub_outer*2);
        }

        cylinder(h=h_outer_hub - outer_hub_thread_height + epsilon, r=D_outer_hub_outer/2);

        // part that sticks out the back
        translate([0,0,-outer_hub_h_motor_insert])
        cylinder(h=outer_hub_h_motor_insert + epsilon, r=D_bldc_motor_cutout/2 - motor_axis_insert_margin);
       
        // base
        difference() {
            cylinder(h=hub_base_thickness, r=r_eccentr_holes_bldc_rotor + D_bldc_stator_holes);

            union() {
                // holes for rotor mounting screws
                for (hole_idx = [0:(n_holes_bldc_stator-1)]) {
                    rotate(a=hole_idx * deg_per_hole, v=[0, 0, 1])
                    translate([0., r_eccentr_holes_bldc_rotor, -epsilon])
                    cylinder(h=hub_base_thickness + 2*epsilon, r=D_bldc_stator_holes/2);
                }
            }
        }

        if (draw_mounting) {
            /**
             * mounting screw heads
            **/

            for (hole_idx = [0:(n_holes_bldc_stator-1)]) {
                rotate(a=hole_idx * deg_per_hole, v=[0, 0, 1])
                translate([0., r_eccentr_holes_bldc_rotor, hub_base_thickness+.02])
                cylinder(h=1, r=2); // screw head height: 1mm, dia: 4mm
            }
        }
    }

    // cutout for the inner hub
    translate([0, 0, -h_outer_hub])
    cylinder(h=3*h_outer_hub, r=D_inner_hub_outer/2 + epsilon);
}

    
/**
 * PCBs, mounting, screws, etc.
**/

if (draw_mounting) {

    /**
     * mounting planes for BLDCs
    **/

    for (motor_idx = [0:2]) {
        translate([0., 0, motor_idx*z_second_platform])
        difference() {
            translate([0, 0, hub_base_thickness/2])
            cube([50,50,hub_base_thickness], center=true);

            union() {
                if (motor_idx < 2) {
                    // holes for mounting screws
                    for (hole_idx = [0:(n_holes_bldc_stator-1)]) {
                        rotate(a=hole_idx * deg_per_hole, v=[0, 0, 1])
                        translate([0., r_eccentr_holes_bldc_stator, -epsilon])
                        cylinder(h=hub_base_thickness + 2*epsilon, r=D_bldc_stator_holes/2);
                    }
                    // central rotor cutout hole
                    translate([0., 0., -epsilon])
                    cylinder(h=hub_base_thickness + 2*epsilon, r=D_bldc_rotor_cutout/2);
                }
                else {
                    // central rotor cutout hole
                translate([0., 0., -epsilon])
                cylinder(h=hub_base_thickness + 2*epsilon, r=r_hub_cutout);
                }
            }
        }
    }

        // mounting screw heads
        for (hole_idx = [0:(n_holes_bldc_stator-1)]) {
            rotate(a=hole_idx * deg_per_hole, v=[0, 0, 1])
            translate([0., r_eccentr_holes_bldc_stator, hub_base_thickness+.1])
    translate([0., 0, 2*z_second_platform-2.62])
            cylinder(h=1, r=2); // screw head height: 1mm, dia: 4mm
        }


    /**
     *  holes for mounting screws
    **/

    %
    for (z = [0:z_second_platform]) {
        for (hole_idx = [0:(n_holes_bldc_stator-1)]) {
            rotate(a=hole_idx * deg_per_hole + 45, v=[0, 0, 1])
            translate([0., -r_ecc_standoffs, z + hub_base_thickness])
            cylinder(h=h_standoffs, r=D_standoffs_outer/2);
        }
    }

    /**
     *  photo switches
    **/

    h_photo_switch = 2.1;

    rotate(a=45, v=[0, 0, 1])
    union() {
        translate([-12.5, 0, 37.5])
        cube([3.4, 4.2, h_photo_switch], center=true);

        translate([0., 0, z_second_platform])
        translate([-12.5, 0, -h_photo_switch/2 - .1])
        cube([3.4, 4.2, h_photo_switch], center=true);
    };
}

/**
 * BLDC motors 
**/

if (draw_motors) {
    for (motor_idx = [0:1]) {
        translate([0., 0, hub_base_thickness + motor_idx*z_second_platform])
        difference() {
        color([1,0,0],.2)
            cylinder(h=h_bldc_motor, r=D_bldc_motor/2);
             
            translate([0., 0, -epsilon])
            cylinder(h=h_bldc_motor + 2*epsilon, r=D_bldc_motor_cutout/2);
        }
    }
}
