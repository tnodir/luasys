@rem Open "Visual Studio .NET Command Prompt" to run this script

@setlocal
@set LUA=../../luajit-2.0
@set LSCOMPILE=cl /nologo /c /LD /MD /O2 /W3 /D_CRT_SECURE_NO_DEPRECATE /D_CRT_SECURE_NO_WARNINGS /D_SCL_SECURE_NO_WARNINGS /I%LUA%/src
@set LSLINK=link /nologo

@rem Check Windows Version is Vista+
@ver | findstr /i "Version 6\." > NUL
@if errorlevel 0 goto VERSION_VISTA
@set WIN32_VERSION=WIN32_COMMON
@goto VERSION_END
:VERSION_VISTA
@set WIN32_VERSION=WIN32_VISTA
@set LSCOMPILE=%LSCOMPILE% /arch:SSE2
:VERSION_END

%LSCOMPILE% /DWIN32 /DLUA_BUILD_AS_DLL /D%WIN32_VERSION% luasys.c sock/sys_sock.c isa/isapi/isapi_dll.c
@if errorlevel 1 goto :END
%LSLINK% /DLL /OUT:sys.dll /DEF:isa/isapi/isapi_dll.def *.obj %LUA%/src/lua*.lib kernel32.lib user32.lib winmm.lib shell32.lib advapi32.lib ws2_32.lib psapi.lib
@if errorlevel 1 goto :END

:END

@del *.obj *.manifest *.lib *.exp
