"""Run with Vitis 2025.2: vitis.bat -s scripts/build_platform.py."""
from pathlib import Path
from zipfile import ZipFile
import vitis

root = Path(__file__).resolve().parents[1]
(root / "build").mkdir(exist_ok=True)
with ZipFile(root / "usb_audio.xsa") as archive:
    init = next(n for n in archive.namelist() if n.endswith("ps7_init.tcl"))
    (root / "build/ps7_init.tcl").write_bytes(archive.read(init))
client = vitis.create_client()
try:
    client.set_workspace(path=str(root / "vitis"))
    if (root / "vitis/audio_platform/vitis-comp.json").exists():
        platform = client.get_component(name="audio_platform")
        platform.update_hw(hw_design=str(root / "usb_audio.xsa"))
    else:
        platform = client.create_platform_component(
            name="audio_platform", hw_design=str(root / "usb_audio.xsa"),
            os="standalone", cpu="ps7_cortexa9_0", domain_name="baremetal")
    platform.build()
finally:
    vitis.dispose()
