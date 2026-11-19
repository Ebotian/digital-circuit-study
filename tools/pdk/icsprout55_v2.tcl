set FOUNDARY_PATH "$PROJ_HOME/pdk/icsprout55"
set STDCELL_PATH  "$FOUNDARY_PATH/IP/STD_cell/ICsprout55_SC9T_BASIC_SVT"

set LIB_FILES         [list "$STDCELL_PATH/liberty/ICsprout55_9TSVT_tt_v1p2_25c_basic_nldm.lib"]
set STDCELL_LEF_FILES [list "$STDCELL_PATH/lef/ICsprout55_9TSVT_basic.lef"]
set TECH_LEF_FILE     "$FOUNDARY_PATH/prtech/techLEF/N551P6M.lef"

set BLACKBOX_V_FILE   ""
set CLKGATE_MAP_FILE  ""
set LATCH_MAP_FILE    ""
set BLACKBOX_MAP_TCL  ""

set TIEHI_CELL_AND_PORT "TIEHIX1_9TSVT Y"
set TIELO_CELL_AND_PORT "TIELOX1_9TSVT Y"
set BUF_CELL            "BUFX8_9TSVT"
set DONT_USE_CELLS      [list "LAT*"]
