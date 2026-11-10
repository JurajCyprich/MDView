@echo off
rem Zostavenie MDView.exe (MSVC Build Tools). Vysledok je jedno standalone exe bez zavislosti.
rem Ikona (mdview.ico) a navod (NAVOD.md) sa vkladaju do exe cez mdview.rc.
rem "build.bat test" zostavi MDView_test.exe so samotestom (pozri MDV_TEST v mdview.c).
setlocal
cd /d "%~dp0"
call "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul
set OUT=MDView.exe
set DEFS=
if /i "%1"=="test" set OUT=MDView_test.exe& set DEFS=/DMDV_TEST
rc /nologo /fo mdview.res mdview.rc
cl /nologo /O2 /MT /W3 /utf-8 %DEFS% mdview.c mdview.res /Fe:%OUT% /link /SUBSYSTEM:WINDOWS /MANIFEST:EMBED /MANIFESTINPUT:app.manifest ^
  user32.lib gdi32.lib comdlg32.lib shell32.lib ole32.lib uuid.lib advapi32.lib comctl32.lib dwmapi.lib uxtheme.lib winhttp.lib
if exist mdview.obj del mdview.obj
if exist mdview.res del mdview.res
