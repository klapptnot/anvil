#!/usr/bin/bash

read -r THIS_SCRIPT < <(realpath -- "${0}")
read -r THIS_NAME < <(basename -- "${THIS_SCRIPT}")
read -r THIS_PARENT < <(dirname -- "${THIS_SCRIPT}")

readonly LIBS_DIR="${THIS_PARENT}/src/libs"
readonly TARGET_DIR="${THIS_PARENT}/target"
readonly OUT_BIN="${TARGET_DIR}/main"
readonly -a RECIPE=(
  "${THIS_PARENT}/src/main.c"
  "${THIS_PARENT}/src/build.c"
  "${THIS_PARENT}/src/config.c"
  "${THIS_PARENT}/src/yaml.c"
)

function main {
  [ ! -d "${TARGET_DIR}" ] && mkdir -p "${TARGET_DIR}"

  local operation="${1:?Argument required, one of <exp|run|val>}"
  shift 1

  readonly -a C_FLAGS=(
    "-I${LIBS_DIR}"
    "-x"
    "c"
    "-std=c23"
    "-Wall"
    "-Werror"
    "-Wdangling"
    "-Wextra"
    "-pedantic"
    "-ggdb"
    "-O1"
    # "-fstack-protector-strong"
    # "-D_FORTIFY_SOURCE=2"
    # "-fsanitize=address,undefined,leak"
    # "-fno-omit-frame-pointer"
  )

  if [ "${operation}" == 'exp' ]; then
    exec clang -E "-I${LIBS_DIR}" -x c -std=c23 \
      "${RECIPE[@]}"
  fi

  local REBUILD=false
  if [ -f "${OUT_BIN}" ]; then
    for f in "${RECIPE[@]}"; do
      if [ "${f}" -nt "${OUT_BIN}" ]; then
        REBUILD=true
        break
      fi
    done
  else
    REBUILD=true
  fi

  if "${REBUILD}"; then
    printf ' Compiling...\r'
    clang "${C_FLAGS[@]}" \
      -o "${OUT_BIN}" "${RECIPE[@]}" || exit
    printf '\x1b[0K Done\r'
  fi

  readonly -a args=(
    "${@}"
    "${THIS_PARENT}/build.yaml"
  )

  if [ "${operation}" == 'run' ]; then
    exec "${OUT_BIN}" "${args[@]}"
  elif [ "${operation}" == "val" ]; then
    exec valgrind -s \
      --leak-check=full --show-leak-kinds=all \
      --track-origins=yes --verbose \
      "${OUT_BIN}" "${args[@]}"
  fi
}

main "${@}"
