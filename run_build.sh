#!/bin/bash
#

set -euo pipefail

do_install=""
do_package=""
build_type="release"

# Parse arguments
while [[ $# -gt 0 ]]; do
  case $1 in
    -i | --install)
      do_install="1"
      shift
      ;;
    -p | --package)
      do_package="1"
      shift
      ;;
    -*)
      echo "Unknown option: $1" >&2
      exit 1
      ;;
    *)
      build_type="$1"
      shift
      ;;
  esac
done

build_type_lower="${build_type,,}"

case "${build_type_lower}" in
  release)
    build_type="Release"
    ;;
  debug)
    build_type="Debug"
    ;;
  relwithdebinfo)
    build_type="RelWithDebInfo"
    ;;
  *)
    echo "Invalid build type: ${build_type}. Must be one of: release, debug, relwithdebinfo" >&2
    exit 1
    ;;
esac

function build_preset() {
  local preset=$1
  local build_dir=$2
  cmake -G Ninja --preset "${preset}"
  cmake --build --preset "${preset}"
  if [[ -n "${do_package}" ]]; then
    cmake --build --preset "${preset}" -t package
  fi
  if [[ -n "${do_install}" ]]; then
    cmake --install "${build_dir}"
  fi
}

build_pr="wlsim/${WLSIM_ENV_KEY}/Release"
host_pr="wlsim/${WLSIM_ENV_KEY}/${build_type}"
build_preset="${WLSIM_ENV_KEY}-${build_type_lower}"

conan install . -pr:b "${build_pr}" -pr:h "${host_pr}" --build="missing"

build_preset "conan-${build_preset}" "build/${build_preset}"

