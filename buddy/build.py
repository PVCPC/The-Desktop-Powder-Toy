# Builds the buddy library (Ruffle + our C interface) for meson: build.py <output library> <cargo target dir>
# Ruffle is fetched at the pinned commit with our small patch (ruffle-host.patch) the first time.
# RUFFLE_GIT=<path or url> fetches it from somewhere else (a local clone, say).
import os, pathlib, shutil, subprocess, sys

here = pathlib.Path(__file__).resolve().parent
rev = '36bd357f3f9f7066706200ab6a5fd705c32081ac'
ruffle = here / 'vendor' / 'ruffle'
if not (ruffle / 'Cargo.toml').exists():
    ruffle.mkdir(parents=True, exist_ok=True)
    def git(*args):
        subprocess.run(['git', '-C', str(ruffle), *args], check=True)
    git('init', '-q')
    git('fetch', '-q', '--depth', '1', os.environ.get('RUFFLE_GIT', 'https://github.com/ruffle-rs/ruffle.git'), rev)
    git('checkout', '-q', 'FETCH_HEAD')
    git('apply', str(here / 'ruffle-host.patch'))
out, target = pathlib.Path(sys.argv[1]), pathlib.Path(sys.argv[2])
subprocess.run(['cargo', 'build', '--release', '--locked', '--manifest-path', str(here / 'Cargo.toml'), '--target-dir', str(target)], check=True)
shutil.copyfile(target / 'release' / out.name, out)
