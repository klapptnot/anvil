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

function clang_check {
  local clangd_cfg="${XDG_CONFIG_HOME:-${HOME}/.config}/clangd/config.yaml"
  local flags=()
  local files=()

  # Extract CompileFlags -> Add from the YAML
  if [[ -f "${clangd_cfg}" ]]; then
    local in_add=0
    mapfile -t flags < <(yq -rM '.CompileFlags.Add[]' "${clangd_cfg}")
  else
    echo "💢 Config not found at ${clangd_cfg}" >&2
    return 1
  fi

  # Determine files to check
  if [[ ${#} -eq 0 ]]; then
    # No args? Find all .c and .h files recursively
    mapfile -t files < <(find . -type f \( -name '*.c' -o -name '*.h' \))
  else
    files=("${@}")
  fi

  if [[ ${#files[@]} -eq 0 ]]; then
    echo "😭 No files to check!" >&2
    return 1
  fi

  echo "🔥 Checking ${#files[@]} files with ${#flags[@]} flags from clangd config..."

  clang "-I${LIBS_DIR}" "${flags[@]}" "${files[@]}"
}

function main {
  [ ! -d "${TARGET_DIR}" ] && mkdir -p "${TARGET_DIR}"

  local operation="${1:?Argument required, one of <exp|run|val|check>}"
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

  if [ "${operation}" == 'check' ]; then
    clang_check "${@}"
    exit
  fi

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
    "${THIS_PARENT}/anvil.yaml"
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
