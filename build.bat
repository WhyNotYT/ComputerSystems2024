@echo off
REM Change directory to CCS project path
cd /d "C:\Users\yumis\Downloads\empty_CC2650STK_TI_2023\empty_CC2650STK_TI_2023"
   
REM Set the path to the CCS installation (adjust if needed)
set CCS_PATH="D:\Softwares\TiShit2\ccs"

REM Build the project using CCS command-line tools
"D:\Softwares\TiShit2\ccs\utils\bin\gmake"

REM Check if build was successful
IF %ERRORLEVEL% NEQ 0 (
    echo Build failed
    exit /b %ERRORLEVEL%
)
echo Build succeeded

REM Path to the DSLite tool
set DSLITE_PATH="D:\Softwares\TiShit2\ccs\ccs_base\DebugServer\bin\DSLite.exe"

REM Path to the ccxml file (configuration file for your SensorTag)
set CCXML_PATH="C:\Users\yumis\Downloads\empty_CC2650STK_TI_2023\empty_CC2650STK_TI_2023\targetConfigs\CC2650F128.ccxml"

REM Flash the built binary and run it
"%DSLITE_PATH%" --config="%CCXML_PATH%" --load -r "Debug/empty_CC2650STK_TI_2023.out"

REM Exit
exit /b 0
