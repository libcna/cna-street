#!/usr/bin/env python3
"""Derive the street's sound set from the NOX Sound Essentials packs.

    python3 scripts/prepare-audio.py [--pack <dir>] [--out <dir>] [--check]

The packs are CC0 but are not fetchable by URL -- they are a manual download
from https://asoundeffect.com / the Unity Asset Store -- so, unlike every
other asset this project uses, `scripts/fetch-assets.sh` cannot get them.
Point `--pack` at an unpacked `Essentials_Series_NOX_SOUND` folder (default:
`$CNA_STREET_NOX_SOUND`, then `/rv/tmp/Essentials_Series_NOX_SOUND`) and this
writes what the demo plays into `assets/external/downloads/derived/audio/`:

  * every file 16-bit PCM, because the packs are 24-bit and a 30-second
    24-bit loop is eight megabytes for a sound nobody hears past the first
    eight seconds of;
  * every loop cut to a working length with a 60 ms fade at each end, so
    the cut cannot click;
  * everything an engine, a footstep or a voice needs mono, so `Apply3D`
    can pan it; the ambience keeps its stereo.

`--check` reports what is present without writing anything, and is what
`validate-assets.py` runs. A tree with no pack derives nothing and the demo
runs silent, exactly as one with no models runs with generated props.
"""
import argparse
import hashlib
import json
import os
import pathlib
import shutil
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
OUT = ROOT / "assets" / "external" / "downloads" / "derived" / "audio"

# name, pack-relative source, seconds to keep (0 = whole file), channels.
CAR = "Vehicle_Essentials_NOX_SOUND/Vehicle_Essential_Car/"
NATURE = "Nature_Essentials_NOX_SOUND/"
STEPS = "Footsteps_Essentials_NOX_SOUND/Footsteps_Tile/Footsteps_Tile_Walk/"
VOICE_M = "Voices_Essentials_NOX_SOUND/Voice_Essential_Male/Voice_Male_Expressions/"
VOICE_F = "Voices_Essentials_NOX_SOUND/Voice_Essential_Female/Voice_Female_Expressions/"
SOUNDS = [
    ("engine-idle",  CAR + "Vehicle_Car_Engine_Idle_Exterior_Loop_Mono_01.wav", 9.0, 1),
    ("engine-low",   CAR + "Vehicle_Car_Engine_1000_RPM_Front_Exterior_Loop_Mono.wav", 9.0, 1),
    ("engine-high",  CAR + "Vehicle_Car_Engine_2000_RPM_Front_Exterior_Loop_Mono.wav", 9.0, 1),
    ("engine-drive", CAR + "Vehicle_Car_Drive_Exterior_Loop_Mono.wav", 9.0, 1),
    ("horn",         CAR + "Vehicle_Car_Horn_Exterior_Mono.wav", 0.0, 1),
    ("ambience-wind",  NATURE + "Ambiance_Wind_Calm_Loop_Stereo.wav", 20.0, 2),
    ("ambience-birds", NATURE + "Ambiance_Forest_Birds_Loop_Stereo.wav", 20.0, 1),
] + [
    (f"step-{i}", STEPS + f"Footsteps_Tile_Walk_{i:02d}.wav", 0.0, 1) for i in range(1, 9)
] + [
    (f"voice-m-{i}", VOICE_M + f"Voice_Male_V1_Laugh_Short_Mono_{i:02d}.wav", 0.0, 1)
    for i in range(1, 5)
] + [
    (f"voice-f-{i}", VOICE_F + f"Voice_Female_V1_Laugh_Long_Mono_{i:02d}.wav", 0.0, 1)
    for i in range(1, 4)
]


def default_pack() -> pathlib.Path:
    env = os.environ.get("CNA_STREET_NOX_SOUND")
    if env:
        return pathlib.Path(env)
    return pathlib.Path("/rv/tmp/Essentials_Series_NOX_SOUND")


def derive(pack: pathlib.Path, out: pathlib.Path, name: str, rel: str, seconds: float,
           channels: int) -> pathlib.Path | None:
    source = pack / rel
    if not source.is_file():
        print(f"  {name}: source missing ({rel})")
        return None
    target = out / f"{name}.wav"
    filters = []
    if seconds > 0.0:
        fade = 0.06
        filters.append(f"afade=t=in:st=0:d={fade}")
        filters.append(f"afade=t=out:st={seconds - fade}:d={fade}")
    command = ["ffmpeg", "-v", "error", "-y", "-i", str(source)]
    if seconds > 0.0:
        command += ["-t", f"{seconds}"]
    if filters:
        command += ["-af", ",".join(filters)]
    command += ["-ac", str(channels), "-acodec", "pcm_s16le", "-ar", "48000", str(target)]
    subprocess.run(command, check=True)
    return target


def digest(path: pathlib.Path) -> str:
    h = hashlib.sha256()
    with open(path, "rb") as handle:
        for chunk in iter(lambda: handle.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--pack", default=str(default_pack()))
    parser.add_argument("--out", default=str(OUT))
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    out = pathlib.Path(args.out)

    if args.check:
        present = [name for name, *_ in SOUNDS if (out / f"{name}.wav").is_file()]
        print(f"prepare-audio: {len(present)} of {len(SOUNDS)} derived sounds present in {out}")
        return 0

    pack = pathlib.Path(args.pack)
    if not pack.is_dir():
        print(f"prepare-audio: no pack at {pack}; nothing derived, the demo runs silent")
        return 0
    if shutil.which("ffmpeg") is None:
        print("prepare-audio: ffmpeg is not on the PATH", file=sys.stderr)
        return 1
    out.mkdir(parents=True, exist_ok=True)
    table = []
    for name, rel, seconds, channels in SOUNDS:
        target = derive(pack, out, name, rel, seconds, channels)
        if target is None:
            continue
        table.append({"name": name, "file": f"derived/audio/{target.name}",
                      "sha256": digest(target), "bytes": target.stat().st_size,
                      "from": rel})
        print(f"  {name}: {target.stat().st_size // 1024} KiB")
    (out / "derived.json").write_text(json.dumps(table, indent=1) + "\n")
    print(f"prepare-audio: {len(table)} of {len(SOUNDS)} sounds derived into {out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
