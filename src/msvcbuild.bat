@rem Open "Visual Studio .NET Command Prompt" to run this script

@setlocal
@set LUA=../../luajit-2.0
@set LSCOMPILE=cl /nologo /c /LD /MD /O2 /W3 /D_CRT_SECURE_NO_DEPRECATE /I%LUA%/src
@set LSLINK=link /nologo

@rem Check Windows Version is Vista+
@ver | findstr /i "Version 6\." > NUL
@IF %ERRORLEVEL% NEQ 0 goto END_VERSION
@set WIN32_VERSION=WIN32_VISTA
@goto END_VERSION
@set WIN32_VERSION=WIN32_COMMON
:END_VERSION

%LSCOMPILE% /DLUA_BUILD_AS_DLL /D%WIN32_VERSION% luasys.c sock/sys_sock.c isa/isapi/isapi_dll.c
@if errorlevel 1 goto :END
%LSLINK% /DLL /OUT:sys.dll /DEF:isa/isapi/isapi_dll.def *.obj %LUA%/src/lua*.lib kernel32.lib user32.lib winmm.lib shell32.lib advapi32.lib ws2_32.lib
@if errorlevel 1 goto :END

@del *.obj *.manifest *.lib *.exp

:END
