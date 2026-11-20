# ArkTS 构建与测试环境

[English](README.md) | 简体中文

此 Dockerfile 生成可重复使用的 HarmonyOS 26.0.0 构建环境镜像。镜像构建时仅安装系统依赖并下载解压工具链，不复制工程源码、不安装工程依赖、不执行工程编译或 UT。启动容器后，通过挂载源码运行构建或测试命令。

## 构建环境镜像

准备 Docker Buildx。构建时会自动从 Dockerfile 中已配置的下载地址获取并解压 **Linux x86_64 HarmonyOS 26.0.0 Command Line Tools**，无需额外提供工具包或下载地址。

官方工具链说明：<https://developer.huawei.com/consumer/en/doc/harmonyos-guides/ide-commandline-get>

以下命令均在 `A4P-arkts-sdk` 根目录、Linux / Bash 环境执行：

```sh
docker buildx build \
  -f docker/Dockerfile \
  --load -t a4p-arkts-builder .
```

不需要指定 `--target`；默认镜像即为构建与测试环境，也可显式指定 `--target toolchain`。`--load` 将镜像加载到本地 Docker，供后续 `docker run` 使用。

镜像包含 JDK 17、OpenGL 运行库、`lsof`，Node、OHPM、Hvigor 使用工具包内的版本。可用 `--build-arg BASE_IMAGE=<镜像源>/library/ubuntu:24.04` 指定基础镜像源。构建镜像需要访问 Ubuntu 软件源和工具包下载地址；工程依赖在容器运行时从 OHPM / Hvigor 仓库安装。

## 使用现有脚本构建

```sh
docker run --rm \
  -v "$PWD:/workspace" \
  a4p-arkts-builder \
  bash scripts/build.sh
```

该脚本会安装依赖、清理旧输出，然后构建 release HAR 和设备测试 HAP。它只打包设备测试，不执行测试用例。

产物写回宿主机：

- HAR：`a4p/build/default/outputs/default/a4p.har`
- 设备测试 HAP：`a4p/build/default/outputs/ohosTest/` 下的 `.hap` 文件

## 仅构建 HAR

```sh
docker run --rm \
  -v "$PWD:/workspace" \
  a4p-arkts-builder \
  bash -c 'ohpm install --all &&
    hvigorw --mode module -p product=default -p module=a4p@default \
      -p buildMode=release assembleHar --no-daemon'
```

## 运行本地 UT

```sh
docker run --rm \
  -v "$PWD:/workspace" \
  a4p-arkts-builder \
  bash -c 'ohpm install --all &&
    hvigorw --mode module -p product=default -p module=a4p@default \
      test --no-daemon --stacktrace'
```

该命令运行 `a4p/src/test` 本地测试，依赖 SDK 中的 Previewer 组件。报告通常位于 `a4p/.test/`，通过挂载目录保留在宿主机。业务测试位于 `a4p/src/ohosTest`，执行这些测试还需要 HarmonyOS 设备或兼容模拟器。

## 进入交互式容器

```sh
docker run --rm -it -v "$PWD:/workspace" a4p-arkts-builder
```

容器默认在 `/workspace` 中启动 Bash，可手动运行 `ohpm`、`hvigorw` 或工程脚本。默认使用 root，挂载目录中新生成的文件通常归 root 所有。
