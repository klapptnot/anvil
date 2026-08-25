#!/usr/bin/bash

set -u
shopt -s gnu_errfmt nullglob

readonly LIBS_DIR_NAME="libs/"

read -r PARENT_DIR < <(realpath -LE "${0}")
readonly PARENT_DIR="${PARENT_DIR%/*}"
readonly BUILD_DIR="${PARENT_DIR}/target"
readonly LIBS_DIR="${PARENT_DIR}/src/libs"

readonly CC='clang'
readonly CFLAGS=(
  -I"${LIBS_DIR}"
  -x c
  -std=c23
  -Wall
  -Werror
  -Wdangling
  -Wextra
  -pedantic
)

function get-sources {
  local headers
  IFS=$'\n' read -d '' -ra headers < <(clang "${CFLAGS[@]:0:4}" -MM "${@}" 2> /dev/null | grep -oE '[^ \\]+\.h')
  headers=("${headers[@]//"${LIBS_DIR_NAME}"/}")
  headers=("${headers[@]/%.h/.[c]}")

  local source
  for source in ${headers[@]}; do
    if [[ ! -v "sources_read[${source}]" ]]; then
      sources_read["${source}"]=0
      sources+=("${source}")
      ((sources_len++))
    fi
  done
}

function main {
  if ((${#} < 1)); then
    printf 'znvmake.sh: no file given\n' >&2
    exec false
  fi

  local file="${!#}"           # last arg is always the current file
  read -r file < <(realpath -LE "${file}")
  local args=("${@:1:${#}-1}") # everything before it is user args

  if [[ ! -r "${file}" ]]; then
    printf 'znvmake.sh: no such file: %s\n' "${file}" >&2
    exec false
  fi

  case "${file}" in
    *.cpp | *.c) ;;
    *)
      printf 'znvmake.sh: refusing to build non C file: %s\n' "${file}" >&2
      exec false
      ;;
  esac

  local out_file="${file##*/}"
  out_file="${BUILD_DIR}/${out_file%.*}"

  mkdir -p "${BUILD_DIR}" || {
    printf 'znvmake.sh: failed to create build dir: %s\n' "${BUILD_DIR}" >&2
    exec false
  }

  local -a sources=("${file}")
  local -A sources_read=(["${file}"]=1)
  local -i i=0 sources_len=1
  for ((; i < sources_len; i++)); do
    get-sources "${args[@]}" "${sources[i]}"
  done

  "${CC}" "${CFLAGS[@]}" "${args[@]}" -o "${out_file}" "${sources[@]}" || exec false

  printf 'znvmake.sh: %s\n' "${out_file}"
}

main "${@}"
