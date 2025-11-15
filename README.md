# NetSurf-reMarkable [![Build for reMarkable](https://github.com/alex0809/netsurf-reMarkable/actions/workflows/build.yml/badge.svg)](https://github.com/alex0809/netsurf-reMarkable/actions/workflows/build.yml)[![rm1](https://img.shields.io/badge/rM1-supported-green)](https://remarkable.com/store/remarkable)[![rm2](https://img.shields.io/badge/rM2-supported-green)](https://remarkable.com/store/remarkable-2)[![opkg](https://img.shields.io/badge/OPKG-netsurf-blue)](https://toltec-dev.org/)

NetSurf is a lightweight and portable open-source web browser. This project adapts NetSurf for the reMarkable E Ink tablet.
This repository contains the code for to building and releasing new versions.

## Installation

### Toltec

You can install neturf with [Toltec](https://toltec-dev.org) using the following command:

```
opkg install netsurf
```

### Github Release

On the [releases page](https://github.com/alex0809/netsurf-reMarkable/releases), you can find the latest release.
The release assets contain a file `netsurf_[version]_rmall.ipk` that allows for easy installation on device.

Example commands to download and install the ipk file:
```
version=0.4
wget https://github.com/alex0809/netsurf-reMarkable/releases/download/v$version/netsurf_$version-1_rmall.ipk
scp netsurf_$version-1_rmall.ipk root@10.11.99.1:
ssh root@remarkable opkg install netsurf_$version-1_rmall.ipk
```

To install a different release change the `version=` line to the version number for the release you wish to install.

## Usage

The 'a' in the bottom-right corner screen toggles the keyboard.

More usage information may be found on the [official NetSurf website](https://www.netsurf-browser.org/documentation/#User).

### Screen Orientation

NetSurf can be run in either portrait (default) or landscape mode. To change the orientation, edit the `~/.netsurf/Choices` file and set:

```
fb_orientation:landscape
```

or

```
fb_orientation:portrait
```

The default is portrait mode if the option is not specified. You will need to restart NetSurf for the change to take effect.

**Note:** In landscape mode, the screen dimensions are 1872x1404 (swapped from portrait's 1404x1872), and touch/pen input is automatically adjusted to match the orientation.

### Local build and installation

#### Requirements

The build itself is done in a Docker container, so apart from Docker, make, and git, there
should be no additional requirements.

`make` prints a list of all available commands by default.

#### Initial Setup

> **⚠️ IMPORTANT:** This project uses git submodules for `netsurf` and `libnsfb`. You **MUST** initialize the submodules before building!

After cloning this repository, run the following:

```bash
git submodule update --init
```

Or clone the repository with submodules in one step:
```bash
git clone --recurse-submodules <repository-url>
```

#### Build

`make image` to build the Docker image with all dependencies and toolchain. This only needs to be done once or when dependencies change.

Then `make build` to build netsurf and libnsfb from the submodules.
The resulting netsurf binary is `netsurf/nsfb`.

The Docker image contains all pre-built dependencies, so rebuilding after changes to `netsurf` or `libnsfb` is fast - just run `make build` again.

> MacOS note:
> There is an [open issue](https://github.com/alex0809/netsurf-reMarkable/issues/21) with the build when using a bind-mounted build directory.
> A workaround will be automatically enabled when running `make build` under MacOS, please see the ticket for details.

#### Installation to Device

`make install` to build and then install the updated binary to the device.
This will use `scp` to copy the binary and required files to the device.
Device address used is by default `10.11.99.1` (i.e. reMarkable connected to your PC via USB), but can be overridden with the `INSTALL_DESTINATION` variable.
The netsurf binary will be copied to `~/netsurf`, and the required resources are copied to `~/.netsurf`.

The font files defined in the configuration file `~/.netsurf/Choices` must exist.
You can either install the pre-configured fonts via opkg, or copy your own preferred fonts to the device and adapt the `Choices` file.

Installation of pre-configured fonts:
```
opkg install dejavu-fonts-ttf-DejaVuSans dejavu-fonts-ttf-DejaVuSans-Bold dejavu-fonts-ttf-DejaVuSans-BoldOblique dejavu-fonts-ttf-DejaVuSans-Oblique dejavu-fonts-ttf-DejaVuSerif dejavu-fonts-ttf-DejaVuSerif-Bold dejavu-fonts-ttf-DejaVuSerif-Italic dejavu-fonts-ttf-DejaVuSansMono dejavu-fonts-ttf-DejaVuSansMono-Bold
```

`make uninstall` to remove the binary and other installed files from the device.

## Local development

### Git Submodules Workflow

The `netsurf` and `libnsfb` repositories are included as git submodules. This allows you to:
- Make changes directly in the submodule directories
- Commit and push changes to the forked repositories
- Rebuild quickly without rebuilding the Docker image

To make changes:
1. Navigate to `netsurf/` or `libnsfb/` directory
2. Make your changes and commit them
3. Push to the respective repository
4. Run `make build` to rebuild with your changes

See [SUBMODULES.md](SUBMODULES.md) for detailed instructions on working with submodules.

### Quick Development Setup

`make checkout` to set up the workspace for local development.
This will initialize the submodules with the HEAD of master branches.

Any local changes in the `netsurf/` or `libnsfb/` directories will be picked up with the next `make build`.

### IDE Support (clangd)

To use clangd language server, you can run `make clangd-build`, which will prepare a Docker container with
clangd and compile-commands set up.
After the build is complete, you can start the container with `make clangd-start`, and access with
[clangd_docker.sh](scripts/clangd_docker.sh).

## Architecture

This project uses a multi-stage build approach:

1. **Docker Image** (`make image`): Contains the complete reMarkable cross-compilation toolchain and all NetSurf dependencies (libwapcaplet, libparserutils, libhubbub, libdom, libcss, etc.). This is built once and cached.

2. **Git Submodules**: The `netsurf` and `libnsfb` repositories are git submodules that are mounted into the Docker container at build time.

3. **Build Script** (`make build`): Mounts the submodules into the container and runs `scripts/build.sh`, which builds both `libnsfb` and `netsurf`.

This design allows for fast iteration: the heavy dependencies are pre-built in the Docker image, while the repositories you're actively developing are easy to modify and rebuild.

## Related repositories

- [libnsfb-reMarkable](https://github.com/idanov/libnsfb-reMarkable): fork of libnsfb with reMarkable-specific code for drawing to the screen and input handling (included as submodule)
- [netsurf-base-reMarkable](https://github.com/idanov/netsurf-base-reMarkable): fork of netsurf, with modifications to make it work better on the reMarkable (included as submodule)
