@echo off
REM Launches both synthetic_device demo profiles at once, each in its own
REM window, so you don't have to retype the long commands every time.
REM See docs/SYNTHETIC_DEVICE.md for the full explanation.
REM
REM Prerequisite: two com0com virtual port pairs already installed, e.g.
REM   command> install PortName=COM10 PortName=COM11
REM   command> install PortName=COM12 PortName=COM13
REM synthetic_device occupies one end of each pair (SOLAR_PORT/WEATHER_PORT
REM below); point TraceView's Devices tab at the OTHER end of each pair
REM (COM11 for the solar panel, COM13 for the weather station by default).
REM
REM Edit the two SET lines below if your com0com ports differ -- and if you
REM do, also update the matching "portName" values in example_dashboard.tvproj
REM (the OTHER end of each pair), or TraceView will silently open some
REM unrelated port: the connection dot still turns green (the port itself
REM opened fine) but nothing ever completes, since bytes never reach either
REM synthetic_device process. There is no error for this mismatch -- check
REM both files agree before assuming something else is broken.

setlocal
set SOLAR_PORT=COM10
set WEATHER_PORT=COM12
set EXE=%~dp0..\..\build\tools\synthetic_device.exe

if not exist "%EXE%" (
    echo Could not find %EXE%
    echo Build the project first, or edit EXE in this script to point at your build dir.
    pause
    exit /b 1
)

start "synthetic_device: solar_panel (%SOLAR_PORT%)" cmd /k "%EXE%" --port %SOLAR_PORT% --profile solar_panel
start "synthetic_device: weather_station (%WEATHER_PORT%)" cmd /k "%EXE%" --port %WEATHER_PORT% --profile weather_station

echo Launched solar_panel on %SOLAR_PORT% and weather_station on %WEATHER_PORT%.
echo In TraceView, add Devices pointed at the OTHER end of each com0com pair.
