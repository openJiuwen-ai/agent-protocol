# ArkTS Build and Test Environment

English | [简体中文](README_zh.md)

This Dockerfile creates a reusable HarmonyOS 26.0.0 build environment image. Building the image installs system dependencies and downloads and extracts the toolchain. It does not copy project sources, install project dependencies, compile the project, or run unit tests. Mount the source directory when running a container to build or test the project.

## Build the environment image

Install Docker Buildx. The build automatically downloads and extracts **Linux x86_64 HarmonyOS 26.0.0 Command Line Tools** from the URL configured in the Dockerfile. No external toolchain package or download URL is required.

Official toolchain documentation: <https://developer.huawei.com/consumer/en/doc/harmonyos-guides/ide-commandline-get>

Run all commands below from the `A4P-arkts-sdk` root directory in a Linux / Bash environment:

```sh
docker buildx build \
  -f docker/Dockerfile \
  --load -t a4p-arkts-builder .
```

The default target is the build and test environment; `--target` is optional. You can also select it explicitly with `--target toolchain`. The `--load` option loads the image into local Docker for subsequent `docker run` commands.

The image includes JDK 17, OpenGL runtime libraries, and `lsof`. Node, OHPM, and Hvigor use the versions bundled with the toolchain. To use an alternative base image registry, pass `--build-arg BASE_IMAGE=<registry>/library/ubuntu:24.04`. Building the image requires access to the Ubuntu package repositories and the toolchain download URL. Project dependencies are installed from the OHPM / Hvigor repositories when the container runs.

## Build with the existing script

```sh
docker run --rm \
  -v "$PWD:/workspace" \
  a4p-arkts-builder \
  bash scripts/build.sh
```

The script installs dependencies, cleans previous outputs, and builds a release HAR and an on-device test HAP. It packages the device tests without executing test cases.

Outputs are written back to the host:

- HAR: `a4p/build/default/outputs/default/a4p.har`
- On-device test HAP: the `.hap` file under `a4p/build/default/outputs/ohosTest/`

## Build only the HAR

```sh
docker run --rm \
  -v "$PWD:/workspace" \
  a4p-arkts-builder \
  bash -c 'ohpm install --all &&
    hvigorw --mode module -p product=default -p module=a4p@default \
      -p buildMode=release assembleHar --no-daemon'
```

## Run local unit tests

```sh
docker run --rm \
  -v "$PWD:/workspace" \
  a4p-arkts-builder \
  bash -c 'ohpm install --all &&
    hvigorw --mode module -p product=default -p module=a4p@default \
      test --no-daemon --stacktrace'
```

This command runs local tests in `a4p/src/test` and requires the SDK Previewer component. Reports are typically written to `a4p/.test/` and retained on the host through the mounted source directory. Tests in `a4p/src/ohosTest` additionally require a HarmonyOS device or a compatible emulator to execute.

## Open an interactive container

```sh
docker run --rm -it -v "$PWD:/workspace" a4p-arkts-builder
```

The container starts Bash in `/workspace`. You can run `ohpm`, `hvigorw`, or project scripts manually. The container runs as root by default, so newly generated files in the mounted directory are typically owned by root.
