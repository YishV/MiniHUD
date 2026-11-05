"""Generate MiniHUD's vector cat ICO using the Windows drawing API (no packages)."""
import subprocess
from pathlib import Path
subprocess.run(["powershell", "-NoProfile", "-ExecutionPolicy", "Bypass", "-File",
                str(Path(__file__).with_suffix(".ps1"))], check=True)
