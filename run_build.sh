#!/bin/bash
#

debug=""
# Parse options
while getopts "d" opt; do
  case $opt in
    d)
      debug=1
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

if [[ -z "$debug" ]]; then
  conan install . -pr:a ${WLSIM_CONAN_PR} -s build_type=Release --build="missing"
  cmake --preset conan-release
else
  conan install . -pr:a ${WLSIM_CONAN_PR} --build="missing" -s build_type=Release -s "&:build_type=Debug"
  cmake --preset conan-debug
fi
