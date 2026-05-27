cmake_minimum_required(VERSION 3.16)

set(EXTRA_COMPONENT_DIRS
    components/modbus_core
    components/modbus_scanner
    components/protocol_analyzer
    components/waveform
    components/diag_report
    components/display
    components/input
    components/storage
    components/wifi_service
)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(modbus-diag)
