$msbuild = "C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe"
$project = "VSProjects\BindstoneClient_Windows\BindstoneClient_Windows.vcxproj"
$args = @("/p:Configuration=Debug", "/p:Platform=x64", "/m")

Write-Host "Building BindstoneClient_Windows..."
& $msbuild $project $args
