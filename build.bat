@echo off
setlocal enabledelayedexpansion
cd /D "%~dp0"
:restart

:: --- Usage Notes (2024/1/10) ------------------------------------------------
::
:: This is a central build script for the RAD Debugger project, for use in
:: Windows development environments. It takes a list of simple alphanumeric-
:: only arguments which control (a) what is built, (b) which compiler & linker
:: are used, and (c) extra high-level build options. By default, if no options
:: are passed, then the main "raddbg" graphical debugger is built.
::
:: Below is a non-exhaustive list of possible ways to use the script:
:: `build raddbg`
:: `build raddbg clang`
:: `build raddbg release`
:: `build raddbg asan telemetry`
:: `build radbin`
::
:: For a full list of possible build targets and their build command lines,
:: search for @build_targets in this file.
::
:: Below is a list of all possible non-target command line options:
::
:: - `asan`: enable address sanitizer
:: - `ubsan`: enable undefined-behavior sanitizer
:: - `telemetry`: enable RAD telemetry profiling support
:: - `spall`: enable spall profiling support

:: --- Unpack Arguments -------------------------------------------------------
for %%a in (%*) do set "%%~a=1"
if not "%msvc%"=="1" if not "%clang%"=="1" set msvc=1
if not "%release%"=="1" set debug=1
if "%debug%"=="1"   set release=0 && echo [debug mode]
if "%release%"=="1" set debug=0 &&   echo [release mode]
if "%msvc%"=="1"    set clang=0 &&   echo [msvc compile]
if "%clang%"=="1"   set msvc=0 &&    echo [clang compile]
if "%~1"==""                         echo [default mode, assuming `raddbg` build] && set raddbg=1 && set com_shim=1 && set raddbg_com_shim=1
if "%~1"=="release" if "%~2"==""     echo [default mode, assuming `raddbg` build] && set raddbg=1 && set com_shim=1 && set raddbg_com_shim=1

:: --- Unpack Command Line Build Arguments ------------------------------------
set auto_compile_flags=
if "%telemetry%"=="1"               set auto_compile_flags=%auto_compile_flags% -DPROFILE_TELEMETRY=1        && echo [telemetry profiling enabled]
if "%spall%"=="1"                   set auto_compile_flags=%auto_compile_flags% -DPROFILE_SPALL=1            && echo [spall profiling enabled]
if "%asan%"=="1"                    set auto_compile_flags=%auto_compile_flags% -fsanitize=address           && echo [asan enabled]
if "%ubsan%"=="1"                   set auto_compile_flags=%auto_compile_flags% -fsanitize=undefined         && echo [ubsan enabled]
if "%opengl%"=="1"                  set auto_compile_flags=%auto_compile_flags% -DR_BACKEND=R_BACKEND_OPENGL && echo [opengl render backend]
if "%dwarf%"=="1" if "%clang%"=="1" set auto_compile_flags=%auto_compile_flags% -gdwarf                      && echo [dwarf debug info]
if "%dwarf%"==""  if "%clang%"=="1" set auto_compile_flags=%auto_compile_flags% -gcodeview
if "%pgo%"=="1" (
  where llvm-profdata /q || echo llvm-profdata is not in the PATH || exit /b 1 
  if "%clang%"=="1" (
    if "%pgo_run%" == "1" (
      call llvm-profdata merge %LLVM_PROFILE_FILE% -output=%~dp0build\build.profdata || exit /b 1
      set auto_compile_flags=%auto_compile_flags% -fprofile-use=%~dp0build\build.profdata
      set pgo_run=0
    ) else (
      echo [pgo enabled]
      set auto_compile_flags=%auto_compile_flags% -fprofile-generate -mllvm -vp-counters-per-site=5
      set LLVM_PROFILE_FILE=%~dp0build\build.profraw
      set pgo_run=1
    )
  ) else (
    echo ERROR: PGO build is not supported with current compiler
    exit /b 1
  )
)

:: --- Compile/Link Line Definitions ------------------------------------------
set cl_common=       /I..\src\ /I..\local\ /nologo /FC /Z7 /Zc:preprocessor
set cl_debug=        /Od /Ob1 /DBUILD_DEBUG=1 %cl_common% %auto_compile_flags% 
set cl_release=      /O2 /DBUILD_DEBUG=0 %cl_common% %auto_compile_flags%
set clang_common=    --driver-mode=cl %cl_common% -fuse-ld=lld -D_USE_MATH_DEFINES -Dgnu_printf=printf -Dstrdup=_strdup -msha -mcx16 -fdiagnostics-absolute-paths -Wno-unused-command-line-argument -Wno-initializer-overrides -Wno-unknown-warning-option -Wno-missing-braces -Wno-unused-function -Wno-unused-parameter -Wno-initializer-overrides -Wno-writable-strings -Wno-missing-field-initializers -Wno-unused-value -Wno-unused-variable -Wno-unused-local-typedef -Wno-deprecated-register -Wno-deprecated-declarations -Wno-unused-but-set-variable -Wno-single-bit-bitfield-constant-conversion -Wno-compare-distinct-pointer-types -Wno-initializer-overrides -Wno-incompatible-pointer-types-discards-qualifiers -Xclang -flto-visibility-public-std
set clang_debug=     %clang_common% %cl_debug%   
set clang_release=   %clang_common% %cl_release% 
set link_common=     /link /MANIFEST:EMBED /INCREMENTAL:NO /NOEXP /PDBALTPATH:%%%%_PDB%%%% /NATVIS:"%~dp0\src\natvis\base.natvis"
if "%msvc%"=="1"     set link_common=%link_common% /NOCOFFGRPINFO
set link_debug=      %link_common% /DYNAMICBASE:NO
set link_release=    %link_common% /OPT:REF /OPT:ICF

:: --- Per-Build Settings -----------------------------------------------------
set link_icon=logo.res
if "%msvc%"=="1"    set rc=call rc
if "%clang%"=="1"   set rc=call llvm-rc

:: --- Choose Compile/Link Lines ----------------------------------------------
if "%msvc%"=="1"      set compile_debug=call cl %cl_debug%
if "%msvc%"=="1"      set compile_release=call cl %cl_release%
if "%clang%"=="1"     set compile_debug=call clang %clang_debug%
if "%clang%"=="1"     set compile_release=call clang %clang_release%
if "%debug%"=="1"     set compile=%compile_debug%
if "%debug%"=="1"     set compile_link=%link_debug%
if "%release%"=="1"   set compile=%compile_release%
if "%release%"=="1"   set compile_link=%link_release%

:: --- Prep Directories -------------------------------------------------------
if not exist build mkdir build
if not exist local mkdir local

:: --- Produce Logo Icon File -------------------------------------------------
pushd build
%rc% /nologo /fo logo.res ..\data\logo.rc || exit /b 1
popd

:: --- Get Current Git Commit Id ----------------------------------------------
for /f %%i in ('call git describe --always --dirty')   do set compile=%compile% -DBUILD_GIT_HASH=\"%%i\"
for /f %%i in ('call git rev-parse HEAD')              do set compile=%compile% -DBUILD_GIT_HASH_FULL=\"%%i\"

:: --- Build & Run Metaprogram ------------------------------------------------
pushd build
if "%meta%"=="1" (
  echo [building metagen]
  %compile_debug% ..\src\metagen\metagen_main.c %compile_link% /OUT:metagen.exe || exit /b 1
)
if "%no_meta%"=="" if exist metagen.exe (
  echo [running metagen]
  metagen.exe || exit /b 1
)
popd

:: --- Build Everything (@build_targets) --------------------------------------
pushd build
if "%raddbg%"=="1"                     set didbuild=1 && %compile% ..\src\raddbg\raddbg_main.c                               %compile_link%          /OUT:raddbg.exe %link_icon%                                         || exit /b 1
if "%raddbg_non_graphical%"=="1"       set didbuild=1 && %compile% ..\src\raddbg\raddbg_main.c -DWM_STUB=1 -DR_BACKEND=R_BACKEND_STUB %compile_link% /OUT:raddbg_non_graphical.exe %link_icon%                           || exit /b 1
if "%com_shim%"=="1"                   set didbuild=1 && %compile% ..\src\com_shim\com_shim_main.c                           %compile_link%          /OUT:com_shim.exe                                                   || exit /b 1
if "%radlink%"=="1"                    set didbuild=1 && %compile% ..\src\linker\lnk.c                                       %compile_link%          /OUT:radlink.exe /NOIMPLIB /NATVIS:"%~dp0\src\linker\linker.natvis" || exit /b 1
if "%radbin%"=="1"                     set didbuild=1 && %compile% ..\src\radbin\radbin_main.c                               %compile_link%          /OUT:radbin.exe                                                     || exit /b 1
if "%raddump%"=="1"                    set didbuild=1 && %compile% ..\src\raddump\raddump_main.c                             %compile_link%          /OUT:raddump.exe                                                    || exit /b 1
if "%ryan_scratch%"=="1"               set didbuild=1 && %compile% ..\src\scratch\ryan_scratch.c                             %compile_link%          /OUT:ryan_scratch.exe                                               || exit /b 1
if "%textperf%"=="1"                   set didbuild=1 && %compile% ..\src\scratch\textperf.c                                 %compile_link%          /OUT:textperf.exe                                                   || exit /b 1
if "%convertperf%"=="1"                set didbuild=1 && %compile% ..\src\scratch\convertperf.c                              %compile_link%          /OUT:convertperf.exe                                                || exit /b 1
if "%debugstringperf%"=="1"            set didbuild=1 && %compile% ..\src\scratch\debugstringperf.c                          %compile_link%          /OUT:debugstringperf.exe                                            || exit /b 1
if "%parse_inline_sites%"=="1"         set didbuild=1 && %compile% ..\src\scratch\parse_inline_sites.c                       %compile_link%          /OUT:parse_inline_sites.exe                                         || exit /b 1
if "%strip_lib_debug%"=="1"            set didbuild=1 && %compile% ..\src\strip_lib_debug\strip_lib_debug.c                  %compile_link%          /OUT:strip_lib_debug.exe                                            || exit /b 1
if "%mule_main%"=="1"                  set didbuild=1 && del vc*.pdb mule*.pdb && %compile_release% /c ..\src\mule\mule_inline.cpp       /Fo:mule_inline.obj && %compile_release% %only_compile% ..\src\mule\mule_o2.cpp /Fo:mule_o2.obj && %compile_debug% /EHsc ..\src\mule\mule_main.cpp ..\src\mule\mule_c.c mule_inline.obj mule_o2.obj %compile_link% /DYNAMICBASE:NO /OUT:mule_main.exe || exit /b 1
if "%mule_module%"=="1"                set didbuild=1 && %compile% ..\src\mule\mule_module.cpp                               %compile_link%          /OUT:mule_module.dll /DLL                                           || exit /b 1
if "%mule_hotload%"=="1"               set didbuild=1 && %compile% ..\src\mule\mule_hotload_main.c %compile_link% %out%mule_hotload.exe & %compile% ..\src\mule\mule_hotload_module_main.c %compile_link% /DLL /OUT:mule_hotload_module.dll || exit /b 1
if "%torture%"=="1"                    set didbuild=1 && %compile% ..\src\torture\torture_main.c                             %compile_link%          /OUT:torture.exe                                                    || exit /b 1
if "%dwarf_expr_test%"=="1"            set didbuild=1 && %compile% ..\src\torture\dwarf_expr_test.c                          %compile_link%          /OUT:dwarf_expr_test.exe                                            || exit /b 1
if "%unit_sizes_from_pdb%"=="1"        set didbuild=1 && %compile% ..\src\scratch\unit_sizes_from_pdb.c                      %compile_link%          /OUT:unit_sizes_from_pdb.exe                                        || exit /b 1
if "%mule_peb_trample%"=="1" (
  set didbuild=1
  if exist mule_peb_trample.exe move mule_peb_trample.exe mule_peb_trample_old_%random%.exe
  if exist mule_peb_trample_new.pdb move mule_peb_trample_new.pdb mule_peb_trample_old_%random%.pdb
  if exist mule_peb_trample_new.rdi move mule_peb_trample_new.rdi mule_peb_trample_old_%random%.rdi
  %compile% ..\src\mule\mule_peb_trample.c %compile_link% /OUT:mule_peb_trample_new.exe || exit /b 1
  move mule_peb_trample_new.exe mule_peb_trample.exe
)
popd

:: --- Set Up Debugger Com Shim -----------------------------------------------
pushd build
if "%raddbg_com_shim%"=="1" copy /y com_shim.exe raddbg.com
popd

:: --- Warn On No Builds ------------------------------------------------------
if "%didbuild%"=="" (
  echo [WARNING] no valid build target specified; must use build target names as arguments to this script, like `build raddbg` or `build radbin`.
  exit /b 1
)

:: --- PGO Run ----------------------------------------------------------------
if "%pgo_run%"=="1" (
  if "%radlink%"=="1" (
    pushd local\lyra_pgo
    call %~dp0build\radlink @lyra.rsp || exit /b 1
    popd
  )
  goto restart
)
