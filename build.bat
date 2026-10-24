@echo off
rem Build the compiler on Windows (MinGW g++ needed; WinFlexBison optional)
rem compiler\minic.exe is already included: you only need this after changing the source.
rem The new binary is built as minic_new.exe and replaces minic.exe only if the build succeeds.
cd /d "%~dp0compiler"
if not exist build mkdir build
where win_bison >nul 2>nul
if %errorlevel%==0 (
  echo Regenerating parser with win_bison and scanner with win_flex ...
  win_bison -d -v -o build\parser.cpp src\parser.y || goto :fail
  win_flex -o build\lexer.cpp src\lexer.l || goto :fail
) else (
  echo win_bison not found - using the generated files already in compiler\build
)
g++ -std=gnu++17 -O2 -static -Wl,--stack,16777216 -Isrc -Ibuild -o minic_new.exe build\parser.cpp build\lexer.cpp src\hand_lexer.cpp src\hand_parser.cpp src\grammar.cpp src\semantic.cpp src\irgen.cpp src\analysis.cpp src\vm.cpp src\compile.cpp src\threads_win32.cpp src\main.cpp -pthread || goto :fail
move /y minic_new.exe minic.exe >nul || goto :fail
echo.
echo Built compiler\minic.exe   Now run:  run.bat
exit /b 0
:fail
echo Build failed. The ready-built compiler\minic.exe was not changed, so run.bat still works.
exit /b 1
