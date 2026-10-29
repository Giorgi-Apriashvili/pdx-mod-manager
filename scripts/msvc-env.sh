#!/usr/bin/env bash
# Source this from Git Bash on Windows to import the Visual Studio x64 developer
# environment (cl.exe, link.exe, INCLUDE, LIB, ...) into the current shell:
#   source scripts/msvc-env.sh
_pmm_vswhere="/c/Program Files (x86)/Microsoft Visual Studio/Installer/vswhere.exe"
if [ ! -x "$_pmm_vswhere" ]; then
  echo "msvc-env: vswhere.exe not found; install Visual Studio with the C++ workload" >&2
  return 1 2>/dev/null || exit 1
fi
_pmm_vs=$("$_pmm_vswhere" -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath | tr -d '\r')
_pmm_vcvars="${_pmm_vs}\\VC\\Auxiliary\\Build\\vcvars64.bat"
_pmm_bat="$(mktemp -u).bat"
printf '@call "%s" >nul 2>&1\r\n@set\r\n' "$_pmm_vcvars" > "$_pmm_bat"
while IFS='=' read -r _k _v; do
  case "$_k" in
    PATH|Path) export PATH="$(cygpath -p "$_v")" ;;
    INCLUDE|LIB|LIBPATH|VCINSTALLDIR|VCToolsInstallDir|VCToolsVersion|WindowsSdkDir|WindowsSDKVersion|WindowsSdkBinPath|UCRTVersion|UniversalCRTSdkDir|VSINSTALLDIR|VisualStudioVersion|Platform)
      export "$_k=$_v" ;;
  esac
done < <(cmd.exe //d //c "$(cygpath -w "$_pmm_bat")" | tr -d '\r')
rm -f "$_pmm_bat"
unset _pmm_vswhere _pmm_vs _pmm_vcvars _pmm_bat _k _v
