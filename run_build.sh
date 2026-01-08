#!/bin/bash
#

build_type="Release"
# Parse options
while getopts "di" opt; do
  case $opt in
    d)
      build_type=Debug
      ;;
    i)
      build_type=RelWithDebInfo
      ;;
    \?)
      echo "Invalid option: -$OPTARG" >&2
      exit 1
      ;;
    :)
      echo "Option -$OPTARG requires an argument." >&2
      exit 1
      ;;
  esac
done

# Shift past the parsed options
shift $((OPTIND-1))

profile="${WLSIM_CONAN_PR}/${build_type}"
build_type_lower=$(echo "${build_type}" | tr '[:upper:]' '[:lower:]')
preset="conan-${build_type_lower}"

conan install . -pr:a ${profile} --build="missing"
cmake -G Ninja --preset "${preset}"
cmake --build --preset "${preset}"

