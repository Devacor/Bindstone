# Writes image_fixtures.hpp: small PNG, JPEG and GIF files made by Windows' own codecs (WIC through WPF, and GDI+), each
# with the pixels those codecs decode it to. The image:: tests hold the in-house decoders to these files, so the reference
# is independent of the code under test and no third-party code enters the repository.
#
#   powershell.exe -ExecutionPolicy Bypass -File image_fixtures.ps1 [-Header <path>] [-Dump <dir>]
#
# Run with Windows PowerShell (the .NET Framework's WPF). -Dump also writes each file and its reference pixels to a folder.
param(
	[string]$Header = (Join-Path $PSScriptRoot "image_fixtures.hpp"),
	[string]$Dump = ""
)
$ErrorActionPreference = "Stop"
Add-Type -AssemblyName PresentationCore, WindowsBase, System.Xaml, System.Drawing
$source = Get-Content -Raw (Join-Path $PSScriptRoot "image_fixtures.cs")
Add-Type -TypeDefinition $source -ReferencedAssemblies PresentationCore, WindowsBase, System.Xaml, System.Drawing
[ImageFixtures]::Run($Header, $Dump)
