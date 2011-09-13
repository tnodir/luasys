@rem Open "Visual Studio .NET Command Prompt" to run this script

@setlocal
@set LUA=../../luajit-2.0
@set LSCOMPILE=cl /nologo /c /LD /MD /O2 /W3 /D_CRT_SECURE_NO_DEPRECATE /I%LUA%/src
@set LSLINK=link /nologo

%LSCOMPILE% /DLUA_BUILD_AS_DLL luasys.c sock/sys_sock.c
@if errorlevel 1 goto :END
%LSLINK% /DLL /out:sys.dll *.obj %LUA%/src/lua*.lib kernel32.lib user32.lib winmm.lib shell32.lib advapi32.lib ws2_32.lib
@if errorlevel 1 goto :END

@del *.obj *.manifest

:END
