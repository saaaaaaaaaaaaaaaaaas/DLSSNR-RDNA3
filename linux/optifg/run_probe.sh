#!/usr/bin/env bash
# Stage and run only the standalone probe in a project-owned prefix.
set -euo pipefail
cd -- "$(dirname -- "$0")/../.."
root=$(pwd)
selection=${1:-baseline}
mode=${selection%-dx10}
frames=${2:-600}
[[ "$mode" == reshade ]] && frames=${2:-1800}
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
    motion=${NR_OPTIFG_MOTION:-lumenite}
    case "$motion" in lumenite|vort) ;; *) echo 'NR_OPTIFG_MOTION must be lumenite or vort' >&2; exit 2;; esac
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
    cp "${NR_OPTIFG_PRODUCER:-artifacts/package-build/nr/dlssnr_amd.addon64}" "$out/"
    cp linux/optifg/optifg-bridge.ini "$out/"
    # Recreate this generated stage so reruns never mix shader revisions.
    rm -rf -- "$out/reshade-shaders"
    mkdir -p "$out/reshade-shaders/Shaders" "$out/reshade-shaders/Textures"
    cp -r artifacts/ref/reshade-shaders/Shaders/. "$out/reshade-shaders/Shaders/"
    cp -r artifacts/ref/vort_Shaders/Shaders/. "$out/reshade-shaders/Shaders/"
    cp -r artifacts/ref/vort_Shaders/Textures/. "$out/reshade-shaders/Textures/"
    cp artifacts/ref/DLSS5-Feeder/shaders/DLSS5_Feed.fx "$out/reshade-shaders/Shaders/"
    python3 linux/build/patch_reshade_effects.py "$out/reshade-shaders/Shaders"
    if [[ "$motion" == lumenite ]]; then
        # The shared patcher expects both providers. Do not compile inactive
        # VORT techniques during a Lumenite-only qualification run.
        rm -- "$out/reshade-shaders/Shaders/vort_Motion.fx" "$out/reshade-shaders/Shaders/vort_Static.fx"
        # The author's link is the only propagation route; never bundle these files.
        lumenite_pin=f80e98d716ad72e12af6f7baa727bcfeb4be5094
        lumenite_zip="artifacts/ref/downloads/LumeniteFX-$lumenite_pin.zip"
        if [[ ! -f "$lumenite_zip" ]]; then
            curl -fL --retry 2 "https://github.com/umar-afzaal/LumeniteFX/archive/$lumenite_pin.zip" -o "$lumenite_zip.part"
            mv -- "$lumenite_zip.part" "$lumenite_zip"
        fi
        python3 linux/package/lumenite.py install "$lumenite_zip" "$out/reshade-shaders/Shaders/DLSS5_Feed.fx" "$out/reshade-shaders"
    fi
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
    if [[ "$motion" == lumenite ]]; then
        python3 - "$out" <<'PY'
from pathlib import Path
import sys
root = Path(sys.argv[1])
p = root / 'ReShadePreset.ini'
s = p.read_text().replace('DLSS5_MV_PROVIDER=2', 'DLSS5_MV_PROVIDER=3')
s = s.replace('vort_MotionEffects@vort_Motion.fx', 'Lumenite_Kernel@lumenite_Kernel.fx')
p.write_text(s)
p = root / 'dlssnr-amd.ini'
p.write_text(p.read_text().replace('[DlssNr]\n', '[DlssNr]\nMotionVectors=lumenite\n'))
PY
    fi
fi
if [[ "${NR_OPTIFG_SR:-0}" == 1 && "$mode" != baseline ]]; then
    sr_backend=${NR_OPTIFG_UPSCALER:-ffx}
    case "$sr_backend" in ffx|fsr21|fsr22|xess) ;; *) echo 'Unsupported SR probe backend' >&2; exit 2;; esac
    if [[ "$sr_backend" == xess ]]; then
        7z x -y -o"$out" artifacts/ref/downloads/OptiScaler_v10.0.0-pre1_20261030.7z \
            'OptiScaler/libxess.dll' 'Licenses/XeSS_LICENSE.txt' > "$out/extract-xess.log"
    fi
    python3 - "$out/OptiScaler.ini" "$sr_backend" "${NR_OPTIFG_OUTPUT_WIDTH:-1920}" "${NR_OPTIFG_OUTPUT_HEIGHT:-1080}" "${NR_OPTIFG_FFX_INDEX:-1}" <<'PY'
from pathlib import Path
import sys
p = Path(sys.argv[1])
w, h = int(sys.argv[3]), int(sys.argv[4])
assert 64 <= w <= 16384 and 64 <= h <= 16384
index = int(sys.argv[5])
assert 0 <= index <= 32
s = p.read_text().replace('OverlayMenu=false', 'OverlayMenu=true\nShortcutKey=45').replace('DebugView=true', 'DebugView=false')
s = s.replace('[FSR]\n', f'[FSR]\nUpscalerIndex={index}\n')
s += f'\n[ReShadeSR]\nEnabled=true\nOutputWidth={w}\nOutputHeight={h}\n\n[Upscalers]\nDx12Upscaler={sys.argv[2]}\n'
p.write_text(s)
PY
fi
if [[ "$mode" != baseline ]]; then
    fg_output=${NR_OPTIFG_FG_OUTPUT:-fsrfg}
    case "$fg_output" in fsrfg|xefg) ;; *) echo 'NR_OPTIFG_FG_OUTPUT must be fsrfg or xefg' >&2; exit 2;; esac
    if [[ "$fg_output" == xefg ]]; then
        7z x -y -o"$out" artifacts/ref/downloads/OptiScaler_v10.0.0-pre1_20261030.7z \
            'OptiScaler/libxess_fg.dll' 'OptiScaler/libxell.dll' 'Licenses/XeSS_LICENSE.txt' > "$out/extract-xefg.log"
    fi
    python3 - "$out" "$fg_output" "${NR_OPTIFG_MULTIPLIER:-3}" "${NR_OPTIFG_ASI:-}" <<'PY'
from pathlib import Path
import configparser, shutil, sys
root = Path(sys.argv[1]); multiplier = int(sys.argv[3])
assert 2 <= multiplier <= 6
p = root / 'OptiScaler.ini'
c = configparser.ConfigParser(interpolation=None); c.optionxform = str; c.read(p)
c['FrameGen']['FGOutput'] = sys.argv[2]
if sys.argv[2] == 'xefg':
    c['XeFG'] = dict(InterpolationCount=str(multiplier-1), DepthInverted='false',
                     JitteredMV='false', HighResMV='false', UIComposition='false')
    c['Menu'] = dict(OverlayMenu='true', ShortcutKey='45')
    if not c.has_section('Plugins'): c.add_section('Plugins')
    c['Plugins']['LoadAsiPlugins'] = 'true' if sys.argv[4] else 'false'
    c['Plugins']['Path'] = r'.\OptiScaler\plugins'
    if sys.argv[4]:
        source = Path(sys.argv[4]).resolve(strict=True)
        assert source.suffix.lower() == '.asi'
        target = root / 'OptiScaler/plugins/MFGUnlocker.asi'; target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source, target)
with p.open('w') as f: c.write(f, space_around_delimiters=False)
PY
fi
if [[ "${NR_OPTIFG_NR:-0}" == 1 ]]; then
    [[ "$mode" == reshade ]] || { echo 'NR requires the full reshade probe' >&2; exit 2; }
    : "${NR_OPTIFG_NR_DATA:?Set NR_OPTIFG_NR_DATA to your private model/shaders folder}"
    python3 - "$out" "$NR_OPTIFG_NR_DATA" "${NR_OPTIFG_NR_MODEL:-CC_SMALL_FP8_EXPERIMENTAL}" "${NR_OPTIFG_NR_SCALE:-0.5}" <<'PY'
from pathlib import Path
import configparser, shutil, sys
root, data = Path(sys.argv[1]), Path(sys.argv[2]).resolve(strict=True)
model = sys.argv[3]
assert model in ('CC_SMALL_FP8', 'CC_SMALL_FP8_EXPERIMENTAL')
assert (data / (model + '.bin')).is_file() and (data / 'shaders/shader-constants.txt').is_file()
if model.endswith('_EXPERIMENTAL'): assert (data / 'experimental/shaders/shader-constants.txt').is_file()
shutil.copytree(data, root / 'dlssnr-amd', dirs_exist_ok=True)
p = root / 'dlssnr-amd.ini'
c = configparser.ConfigParser(interpolation=None); c.optionxform = str; c.read(p)
c['DlssNr']['Enabled'] = '1'
scale = float(sys.argv[4]); assert 0.25 <= scale <= 2.0
c['DlssNr']['WorkingScale'] = str(scale)
c['Network'] = dict(Model=model)
with p.open('w') as f: c.write(f, space_around_delimiters=False)
PY
fi
args=()
[[ "$mode" == baseline ]] && args+=(--baseline)
[[ "$mode" == reshade ]] && args+=(--reshade)
[[ "${NR_OPTIFG_10BIT:-0}" == 1 ]] && args+=(--10bit)
[[ "${NR_OPTIFG_HUD:-0}" == 1 ]] && args+=(--hud)
if [[ "${NR_OPTIFG_HUD:-0}" == 1 && "$mode" == reshade ]]; then
    sed -i 's/Mode=0/Mode=3/' "$out/dlssnr-amd.ini"
fi
[[ -n "${NR_OPTIFG_RECREATE:-}" ]] && args+=(--recreate "$NR_OPTIFG_RECREATE")
[[ -n "${NR_OPTIFG_GUIDE_GAP:-}" ]] && args+=(--guide-gap "$NR_OPTIFG_GUIDE_GAP")
cd -- "$out"
rm -f -- dlssnr-amd.log ReShade.log OptiScaler.log
echo "Standalone $mode probe: $out (private prefix: $prefix)"
env WINEPREFIX="$prefix" WINEDEBUG=-all DISABLE_LSFGVK=1 WINEDLLPATH="$vkd3d:$wine_lib" \
    LD_LIBRARY_PATH="$proton/files/lib/x86_64-linux-gnu${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
    WINEDLLOVERRIDES='dxgi,d3d10core,d3d11,d3d12,d3d12core=n;mscoree,mshtml=' \
    "$proton/files/bin/wine" "./$exe" "${args[@]}" --frames "$frames" > probe.log 2>&1
tail -5 probe.log
if [[ "$mode" == reshade ]]; then
    if ! rg -q '\[optifg\] captured [1-9][0-9]*, consumed presents [1-9]' dlssnr-amd.log; then
        echo 'ReShade did not prove a consumed bridge frame; inspect ReShade.log and dlssnr-amd.log' >&2
        exit 4
    fi
fi
if [[ "${NR_OPTIFG_SR:-0}" == 1 ]]; then
    rg -q 'ReShade SR reconstructed [1-9]' OptiScaler.log || { echo 'No temporal SR reconstruction proved' >&2; exit 5; }
fi
if [[ "${NR_OPTIFG_NR:-0}" == 1 ]]; then
    rg -q '\[nr\] frames: [1-9][0-9]* enhanced|color stage: NR applied' dlssnr-amd.log || {
        echo 'NR never processed a frame; inspect the model/build log or use a longer run' >&2; exit 6;
    }
fi
if [[ "${NR_OPTIFG_FG_OUTPUT:-fsrfg}" == xefg ]]; then
    rg -q 'ReShade XeFG SDK status:.*[1-9][0-9]* generated' OptiScaler.log || {
        echo 'XeFG did not report generated/presented frames; plugin loading alone is insufficient' >&2; exit 7;
    }
fi

if rg -qi 'GPUVM fault|context is lost|PROTECTION_FAULT' probe.log; then
    echo 'GPU fault during execution or teardown; qualification failed' >&2; exit 8
fi
if [[ "${NR_OPTIFG_FG_OUTPUT:-fsrfg}" == xefg ]]; then
    rg -q 'Destroy result: XEFG_SWAPCHAIN_RESULT_SUCCESS' OptiScaler.log || {
        echo 'XeFG context did not confirm clean destruction' >&2; exit 9;
    }
fi
