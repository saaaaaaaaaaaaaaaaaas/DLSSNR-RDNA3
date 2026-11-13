#!/usr/bin/env bash
# Stage and run only the standalone probe in a project-owned prefix.
set -euo pipefail
cd -- "$(dirname -- "$0")/../.."
root=$(pwd)
selection=${1:-baseline}
mode=${selection%-dx10}
exe=optifg_probe.exe
[[ "$selection" == *-dx10 ]] && exe=optifg_probe_dx10.exe
case "$mode" in baseline|direct|reshade) ;; *) echo 'usage: run_probe.sh baseline|direct|reshade[-dx10] [frames]' >&2; exit 2;; esac
: "${NR_PROTON:?Set NR_PROTON to a Proton installation directory}"
proton=$(realpath -- "$NR_PROTON")
wine_lib="$proton/files/lib/wine"
[[ -d "$wine_lib" ]] || wine_lib="$proton/files/lib64/wine"
[[ -d "$wine_lib/dxvk/x86_64-windows" ]] || { echo "DXVK missing in $wine_lib" >&2; exit 1; }
out="$root/artifacts/optifg-probe-$selection"
prefix="$root/build/optifg-prefix"
mkdir -p "$out"
bash linux/build/build_optifg_probe.sh
cp "artifacts/optifg-probe/$exe" "$out/"
for name in d3d11 dxgi; do install -m644 "$wine_lib/dxvk/x86_64-windows/$name.dll" "$out/$name.dll"; done
for name in d3d12 d3d12core; do install -m644 "$wine_lib/vkd3d-proton/x86_64-windows/$name.dll" "$out/$name.dll"; done
[[ -f "$wine_lib/dxvk/x86_64-windows/d3d10core.dll" ]] && install -m644 "$wine_lib/dxvk/x86_64-windows/d3d10core.dll" "$out/d3d10core.dll"
vkd3d="$proton/files/lib/vkd3d"
[[ -d "$vkd3d" ]] || vkd3d="$proton/files/lib64/vkd3d"
if [[ -d "$vkd3d/x86_64-windows" ]]; then
    for dll in "$vkd3d/x86_64-windows/"*.dll; do install -m644 "$dll" "$out/$(basename "$dll")"; done
fi
if [[ "$mode" != baseline ]]; then
    consumer=${NR_OPTIFG_CONSUMER:-artifacts/optifg-consumer/OptiScaler.dll}
    [[ -f "$consumer" ]] || { echo 'Download the optifg-consumer-x86_64 CI artifact first' >&2; exit 1; }
    x86_64-w64-mingw32-objdump -p "$consumer" > "$out/consumer-exports.txt"
    rg -q 'OptiScaler_GetReShadeBridge' "$out/consumer-exports.txt" || { echo 'Consumer bridge export missing' >&2; exit 1; }
    cp "$consumer" "$out/OptiScaler.dll"
    cp linux/optifg/OptiScaler.ini "$out/"
    7z x -y -o"$out" artifacts/ref/downloads/OptiScaler_v10.0.0-pre1_20261030.7z \
        'OptiScaler/amd_fidelityfx*.dll' 'Licenses/*' > "$out/extract-runtime.log"
fi
if [[ "$mode" == reshade ]]; then
    [[ -d "$prefix/drive_c/windows/system32" ]] || { echo 'Run baseline first to create the private prefix' >&2; exit 1; }
    # Load ReShade after OptiScaler's hooks so it wraps the game-facing swapchain.
    # Replace a possible Wine symlink itself; never follow it into the Proton install.
    for name in dxgi d3d11; do
        install -m644 "$wine_lib/dxvk/x86_64-windows/$name.dll" "$prefix/drive_c/windows/system32/$name.dll.optifg-tmp"
        mv -f -- "$prefix/drive_c/windows/system32/$name.dll.optifg-tmp" "$prefix/drive_c/windows/system32/$name.dll"
    done
    # ReShade hooks the system32 D3D module, not an app-local DXVK copy.
    rm -- "$out/d3d11.dll"
    cp artifacts/ref/reshade-6.8.0/ReShade64.dll "$out/ReShade64.dll"
    cat >> "$out/OptiScaler.ini" <<'INI'

[Plugins]
LoadReShade=true
INI
    cp artifacts/package-build/nr/dlssnr_amd.addon64 "$out/"
    cp linux/optifg/optifg-bridge.ini "$out/"
    mkdir -p "$out/reshade-shaders/Shaders" "$out/reshade-shaders/Textures"
    cp -r artifacts/ref/reshade-shaders/Shaders/. "$out/reshade-shaders/Shaders/"
    cp -r artifacts/ref/vort_Shaders/Shaders/. "$out/reshade-shaders/Shaders/"
    cp -r artifacts/ref/vort_Shaders/Textures/. "$out/reshade-shaders/Textures/"
    cp artifacts/ref/DLSS5-Feeder/shaders/DLSS5_Feed.fx "$out/reshade-shaders/Shaders/"
    python3 linux/build/patch_reshade_effects.py "$out/reshade-shaders/Shaders"
    cat > "$out/ReShade.ini" <<'INI'
[GENERAL]
EffectSearchPaths=.\reshade-shaders\Shaders\**
TextureSearchPaths=.\reshade-shaders\Textures\**
PresetPath=.\ReShadePreset.ini
PerformanceMode=0
EffectLoadSkip=1
[OVERLAY]
TutorialProgress=4
[ADDON]
AddonPath=.\
INI
    cat > "$out/ReShadePreset.ini" <<'INI'
PreprocessorDefinitions=DLSS5_MV_PROVIDER=2,V_MV_MODE=1,V_ENABLE_MOT_BLUR=0,V_ENABLE_TAA=0,RESHADE_DEPTH_INPUT_IS_REVERSED=0
Techniques=vort_MotionEffects@vort_Motion.fx,DLSS5_Feed@DLSS5_Feed.fx
TechniqueSorting=vort_MotionEffects@vort_Motion.fx,DLSS5_Feed@DLSS5_Feed.fx
INI
    cat > "$out/dlssnr-amd.ini" <<'INI'
[DlssNr]
Enabled=0
[RenoDX]
Enabled=0
[HudFix]
Mode=0
INI
fi
args=()
[[ "$mode" == baseline ]] && args+=(--baseline)
[[ "$mode" == reshade ]] && args+=(--reshade)
cd -- "$out"
echo "Standalone $mode probe: $out (private prefix: $prefix)"
env WINEPREFIX="$prefix" WINEDEBUG=-all WINEDLLPATH="$vkd3d:$wine_lib" \
    LD_LIBRARY_PATH="$proton/files/lib/x86_64-linux-gnu${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
    WINEDLLOVERRIDES='dxgi,d3d10core,d3d11,d3d12,d3d12core=n;mscoree,mshtml=' \
    "$proton/files/bin/wine" "./$exe" "${args[@]}" --frames "${2:-600}" > probe.log 2>&1
tail -5 probe.log
if [[ "$mode" == reshade ]]; then
    if ! rg -q '\[optifg\] captured [1-9][0-9]*, consumed presents [1-9]' dlssnr-amd.log; then
        echo 'ReShade did not prove a consumed bridge frame; inspect ReShade.log and dlssnr-amd.log' >&2
        exit 4
    fi
fi
