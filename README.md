# NetSurf for reMarkable

A lightweight web browser for reMarkable 1 and 2, maintained as one project.
The browser, framebuffer library, build scripts, and device configuration live
in this repository. A normal clone contains all application source; no submodules
or sibling checkouts are needed.

## Source layout

| Path | Purpose |
| --- | --- |
| `netsurf/` | Browser engine and framebuffer UI |
| `libnsfb/` | Framebuffer drawing, display refresh, pen and touch input |
| `scripts/build.sh` | Builds the library, then the browser |
| `Dockerfile` | Cross-compilation toolchain and third-party dependencies |
| `example/Choices` | Starting configuration for the tablet |

Edit both source directories directly and commit changes from this repository.
The original source revisions and licenses are recorded in [SOURCES.md](SOURCES.md).
Third-party dependencies are still downloaded when building the Docker image;
this is not an offline build.

## Build

Install Docker, Git, and make. Docker must be running. Clone normally:

```sh
git clone https://github.com/idanov/netsurf-reMarkable.git
cd netsurf-reMarkable
```

Build the dependency image once, then build the application:

```sh
make image
make build
```

On Apple Silicon, explicitly select the AMD64 toolchain image when creating it:

```sh
docker build --platform linux/amd64 -t netsurf-build:latest .
make build
```

Docker must support running AMD64 containers on that host. The result is the
32-bit ARM Linux executable `netsurf/nsfb`, for running on the tablet.
Subsequent edits to either source directory only require `make build`.
JavaScript is currently disabled at compile time by `scripts/build.sh`.

On macOS, make automatically uses the `netsurf-build` Docker volume for installed
dependencies. Source directories and their compiler outputs are still bind-mounted.
On other hosts, opt into this mode with `make build USE_VOLUME_MOUNT=YES`.
Use `BUILD_VOLUME=another-name` to select an independent dependency volume.
When changing dependency versions in the image, use a new volume or remove the
old one so its installed dependencies are refreshed.

Run `make` to list the available commands. `make clean` removes the top-level
`build/` directory, dependency volume, and clangd container; it does not remove
compiler outputs within `netsurf/` and `libnsfb/`.

## Install on a tablet

This port targets reMarkable 1 and 2. The reMarkable 2 requires an rm2fb display
server and client shim compatible with its OS version. This repository does not
install or configure that integration. Check your device's compatibility before
installing a package manager or display service; see the
[rm2fb documentation](https://github.com/ddvk/remarkable2-framebuffer) and
[Toltec compatibility information](https://toltec-dev.org/).

Once the build image exists and SSH access as root is configured:

```sh
make install INSTALL_DESTINATION=10.11.99.1
```

Replace the USB address with a Wi-Fi IP or SSH host alias if needed. An explicit
`INSTALL_DESTINATION` overrides any local environment setting, including mise.
The target builds the application, then copies:

- The executable to `/home/root/.netsurf/nsfb`.
- Resources directly into `/home/root/.netsurf/`.
- The sample configuration to `/home/root/.netsurf/Choices.example`.

On first installation, `Choices.example` is also copied to `Choices`. Subsequent
installs preserve an existing `Choices`. Transfers use SSH and SCP, not rsync.
For an already-built executable, use `make copy-resources copy-binary` with the
same `INSTALL_DESTINATION` setting.

Before launching, edit `/home/root/.netsurf/Choices`:

- Set every `fb_face_*` entry to a font file that exists on your tablet. The sample
  uses DejaVu fonts under `/opt/share/fonts/ttf-dejavu/`. You can copy your own fonts
  and change these paths.
- Set `ca_bundle` to an existing CA certificate bundle, such as
  `/etc/ssl/certs/ca-certificates.crt` if present. The sample's `rootCA.pem` is not
  supplied by this repository.
- Adjust `fb_xochitl_restart_command` for your launcher. The sample assumes remux.
- Start with `fb_orientation:portrait`, or set `landscape` and restart the browser.

If you already have a compatible Toltec installation, the sample fonts can be
installed with:

```sh
opkg install dejavu-fonts-ttf-DejaVuSans dejavu-fonts-ttf-DejaVuSans-Bold \
  dejavu-fonts-ttf-DejaVuSans-BoldOblique dejavu-fonts-ttf-DejaVuSans-Oblique \
  dejavu-fonts-ttf-DejaVuSerif dejavu-fonts-ttf-DejaVuSerif-Bold \
  dejavu-fonts-ttf-DejaVuSerif-Italic dejavu-fonts-ttf-DejaVuSansMono \
  dejavu-fonts-ttf-DejaVuSansMono-Bold
```

The executable also requires compatible shared libraries on the tablet. Check
for unresolved dependencies there with:

```sh
/lib/ld-linux-armhf.so.3 --list /home/root/.netsurf/nsfb
```

The current build links against libraries including libevdev, libcurl, OpenSSL
1.1, libpng, libexpat, and libuuid. Copying the executable does not install them.

## Run

The browser command on the tablet is:

```sh
/home/root/.netsurf/nsfb -f remarkable
```

Use a launcher that manages display ownership and restores the stock interface
when the browser exits. On reMarkable 2, the launcher must also arrange for the
rm2fb client shim to be loaded, with its server running. A manual invocation uses
`LD_PRELOAD=/path/to/librm2fb_client.so` with the actual installed library path.

The `a` in the bottom-right corner toggles the on-screen keyboard. Landscape mode
uses 1872 by 1404 logical pixels and adjusts pen and touch coordinates.

`make uninstall INSTALL_DESTINATION=...` removes the entire device-side
`/home/root/.netsurf/` directory, including your configuration. Back up any files
you want to keep first.

## Editor support

`make clangd-build` prepares a development container and compilation database.
Then use `make clangd-start` and [scripts/clangd_docker.sh](scripts/clangd_docker.sh)
to access clangd. These commands use the same source directories as the normal
build. `make clangd-stop` stops the container.
