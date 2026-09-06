param(
	[string]$OutputRoot = '',
	[int]$Jobs = 6
)
$ErrorActionPreference = 'Stop'
$jaiRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..\..'))
if ([string]::IsNullOrWhiteSpace($OutputRoot)) {
	$OutputRoot = Join-Path $jaiRoot 'out\astra'
}
$OutputRoot = [IO.Path]::GetFullPath($OutputRoot)
$outPrefix = [IO.Path]::GetFullPath((Join-Path $jaiRoot 'out')) + [IO.Path]::DirectorySeparatorChar
if (-not $OutputRoot.StartsWith($outPrefix, [StringComparison]::OrdinalIgnoreCase)) {
	throw 'The comparison build must be under Source/JaiScript/out/.'
}
if (-not (Get-Command cl.exe -ErrorAction SilentlyContinue)) {
	throw 'Run this script in an x64 Visual Studio Native Tools environment.'
}
$deps = Join-Path $OutputRoot 'deps'
& python (Join-Path $PSScriptRoot 'prepare_comparison.py') --deps $deps
if ($LASTEXITCODE -ne 0) { throw 'Dependency preparation failed.' }
$jitRoot = Join-Path $deps 'LuaJIT-24c20c94e7db195b640854619577441f9b4bc6be'
Push-Location (Join-Path $jitRoot 'src')
try {
	& cmd.exe /d /c 'msvcbuild.bat lua52compat static'
	if ($LASTEXITCODE -ne 0) { throw 'LuaJIT JIT build failed.' }
} finally {
	Pop-Location
}
$release = Join-Path $OutputRoot 'release'
$configure = @(
	'-S', $jaiRoot, '-B', $release, '-G', 'Ninja',
	'-DCMAKE_BUILD_TYPE=Release', '-DCMAKE_CXX_COMPILER=cl', '-DCMAKE_C_COMPILER=cl',
	'-DCMAKE_CXX_FLAGS_RELEASE=/O2 /DNDEBUG /Zi',
	'-DCMAKE_C_FLAGS_RELEASE=/O2 /DNDEBUG /Zi',
	'-DCMAKE_EXE_LINKER_FLAGS_RELEASE=/DEBUG:FULL /INCREMENTAL:NO',
	'-DJAISCRIPT_BUILD_TESTS=OFF', '-DJAISCRIPT_BUILD_RUNNER=OFF',
	'-DGLOOM_PORT_LUA=ON', "-DGLOOM_LUA_ROOT=$deps/lua-5.4.8",
	'-DGLOOM_PORT_LUA55=ON', "-DGLOOM_LUA55_ROOT=$deps/lua-5.5.1",
	'-DGLOOM_PORT_LUAJIT=ON', "-DGLOOM_LUAJIT_ROOT=$jitRoot",
	"-DGLOOM_LUAJIT_LIBRARY=$jitRoot/src/lua51.lib",
	"-DGLOOM_SOL2_ROOT=$deps/sol2-3.3.0", '-DGLOOM_COMPARISON_PADDING_CONTROLS=ON'
)
& cmake @configure
if ($LASTEXITCODE -ne 0) { throw 'CMake configuration failed.' }
& cmake --build $release --target jai_gloom jai_gloom_padding gloom_lua gloom_lua_padding gloom_lua55 gloom_lua55_padding gloom_luajit gloom_luajit_padding -j $Jobs
if ($LASTEXITCODE -ne 0) { throw 'Comparison build failed.' }
Write-Output "Built comparison and padding binaries in $release/bin"
