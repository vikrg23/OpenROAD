source "helpers.tcl"

read_lef ../../../test/Nangate45/Nangate45.lef
read_def gcd_M7_pin/floorplan.def

# Stdcell power/ground pins
add_global_connection -net VDD -pin_pattern {^VDD$} -power
add_global_connection -net VSS -pin_pattern {^VSS$} -ground

# RAM power ground pins
add_global_connection -net VDD -pin_pattern {^VDDPE$} 
add_global_connection -net VDD -pin_pattern {^VDDCE$}
add_global_connection -net VSS -pin_pattern {^VSSE$}

set_voltage_domain -name CORE -power VDD  -ground VSS
set_voltage_domain -name VIN  -region_name TEMP_ANALOG -power VPWR -ground VSS

define_pdn_grid   -name main_grid -pins {metal7} -voltage_domains {CORE VIN}
add_pdn_stripe -name main_grid -layer metal1 -width 0.17 -followpins
add_pdn_stripe -name main_grid -layer metal2 -width 0.17 -followpins
add pdn_stripe -name main_grid -layer metal4 -width 0.48 -pitch 56.0 -offset 2 -starts_with POWER
add_pdn_stripe -name main_grid -layer metal7 -width 1.40 -pitch 40.0 -offset 2 -starts_with POWER
add_pdn_ring   -name main_grid -layer metal6 -width 5.0 -spacing  3.0 -core_offset 5
add_pdn_ring   -name main_grid -layer metal7 -width 5.0 -spacing  3.0 -core_offset 5

add_pdn_connect -name main_grid -layers {metal1 metal2} -cut_pitch 0.16
add_pdn_connect -name main_grid -layers {metal2 metal4}
add_pdn_connect -name main_grid -layers {metal4 metal7}

define_pdn_grid -macro -name ram -orient {R0 R180 MX MY} -starts_with POWER -pin_direction vertical -block metal6
add_pdn_stripe -name ram -layer metal5 -width 0.93 -pitch 10.0 -offset 2
add_pdn_stripe -name ram -layer metal6 -width 0.93 -pitch 10.0 -offset 2
add_pdn_connect -name ram -layers {metal4 metal5}
add_pdn_connect -name ram -layers {metal5 metal6}
add_pdn_connect -name ram -layers {metal6 metal7}

define_pdn_grid -macro -name rotated_rams -orient {R90 R270 MXR90 MYR90} -starts_with POWER -pin_direction horizontal -block metal6
add_pdn_stripe -name rotated_rams -layer metal6 -width 0.93 -pitch 10.0 -offset 2
add_pdn_connect -name rotated_rams -layers {metal4 metal6}
add_pdn_connect -name rotated_rams -layers {metal6 metal7}

pdngen -verbose

set def_file results/test_gcd_M7_pin.def
write_def $def_file 

diff_files $def_file test_gcd_M7_pin.defok
