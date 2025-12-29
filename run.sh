#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

BUILD_DIR="${ROOT_DIR}/build"
BUILD_TYPE="Release"
CLEAN=0
NO_BUILD=0
FORCE_BUILD=0
WERROR=0

print_help() {
  cat <<'EOF'
用法:
  ./run.sh [--run-* 运行脚本参数] [--] [github-ipr2md 参数...]

脚本行为:
  - 如果已构建过且可执行文件存在：直接运行（不重复 cmake/编译）
  - 否则：自动 CMake 配置 + 编译，然后运行
  - 最终执行 build/github-ipr2md，并把参数原样转发

脚本专用参数(不会传给程序):
  --run-build-dir <dir>     指定构建目录(默认: ./build)
  --run-build-type <type>   Debug/Release/RelWithDebInfo/MinSizeRel (默认: Release)
  --run-build               强制重新配置+编译（不删除构建目录）
  --run-clean               干净重建：删除构建目录后重新配置+编译
  --run-no-build            只运行，不构建（要求可执行文件已存在）
  --run-werror              构建时开启 -DENABLE_WERROR=ON（把告警当错误；CI 默认会开启）
  --run-help                 显示此帮助

示例:
  ./run.sh --repo owner/name --out ./output.md
  ./run.sh --run-build-type Debug -- --help
EOF
}

forward_args=()
while [[ $# -gt 0 ]]; do
  case "$1" in
    --run-build-dir)
      BUILD_DIR="$2"
      shift 2
      ;;
    --run-build-type)
      BUILD_TYPE="$2"
      shift 2
      ;;
    --run-build)
      FORCE_BUILD=1
      shift
      ;;
    --run-clean)
      CLEAN=1
      shift
      ;;
    --run-reconfigure|--run-force-build)
      echo "错误: 脚本参数 $1 已移除。请改用:" >&2
      if [[ "$1" == "--run-force-build" ]]; then
        echo "  --run-build" >&2
      else
        echo "  --run-build   (重新配置+编译，不删除构建目录)" >&2
        echo "  --run-clean   (干净重建，删除构建目录)" >&2
      fi
      exit 2
      ;;
    --run-no-build)
      NO_BUILD=1
      shift
      ;;
    --run-werror)
      WERROR=1
      shift
      ;;
    --run-help|-h)
      print_help
      exit 0
      ;;
    --)
      shift
      forward_args+=("$@")
      break
      ;;
    *)
      forward_args+=("$1")
      shift
      ;;
  esac
done

if [[ $CLEAN -eq 1 ]]; then
  rm -rf "${BUILD_DIR}"
fi

EXE="${BUILD_DIR}/github-ipr2md"

need_build=0
if [[ $NO_BUILD -eq 1 ]]; then
  if [[ $CLEAN -eq 1 || $FORCE_BUILD -eq 1 ]]; then
    echo "错误: --run-no-build 不能与 --run-clean/--run-build 同时使用。" >&2
    exit 2
  fi
  need_build=0
elif [[ $CLEAN -eq 1 ]]; then
  need_build=1
elif [[ $FORCE_BUILD -eq 1 ]]; then
  need_build=1
elif [[ ! -x "${EXE}" ]]; then
  need_build=1
else
  need_build=0
fi

if [[ $need_build -eq 1 ]]; then
  if ! command -v cmake >/dev/null 2>&1; then
    echo "错误: 未找到 cmake，请先安装 CMake。" >&2
    exit 127
  fi
  cmake_args=()
  if [[ $WERROR -eq 1 ]] || [[ -n "${CI:-}" ]] || [[ -n "${GITHUB_ACTIONS:-}" ]]; then
    cmake_args+=(-DENABLE_WERROR=ON)
  fi
  cmake -S "${ROOT_DIR}" -B "${BUILD_DIR}" -DCMAKE_BUILD_TYPE="${BUILD_TYPE}" "${cmake_args[@]}"
  cmake --build "${BUILD_DIR}" --config "${BUILD_TYPE}" -j
fi

if [[ ! -x "${EXE}" ]]; then
  echo "错误: 未找到可执行文件: ${EXE}" >&2
  echo "提示: 你可以去掉 --run-no-build，或检查构建是否成功。" >&2
  exit 1
fi

exec "${EXE}" ${forward_args[@]+"${forward_args[@]}"}


