#!/usr/bin/bash

include barg.sh

read -r THIS_SCRIPT < <(realpath -- "${0}")
read -r THIS_NAME < <(basename -- "${THIS_SCRIPT}")
read -r THIS_PARENT < <(dirname -- "${THIS_SCRIPT}")

readonly LIBS_DIR="${THIS_PARENT}/src/libs"
readonly TARGET_DIR="${THIS_PARENT}/target"
readonly ANVIL_YAML="${THIS_PARENT}/anvil.yaml"

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
    # mapfile -t files < <(find . -type f \( -name '*.c' -o -name '*.h' \))
    mapfile -t files < <(find ./src -type f -name '*.c')
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

function slow_try_get_cfiles {
  declare -n rec_arr="${1}"

  local main="${rec_arr[0]}"

  local c='' h=''
  read -ra all < <(clang "-I${LIBS_DIR}" -std=c23 -MM "${main}" | sed 's/\\$//' | tr -d '\n' | cut -d' ' -f3-)
  for h in "${all[@]}"; do
    [[ "${h}" != "${2}"* ]] && continue

    c=${h%.h}.c
    if test -f "${c}"; then
      [ "${c}" != "${main}" ] && rec_arr+=("${c}")
      continue
    fi

    h="${h##*/}"
    c="${2}/${h%.h}.c"
    test -f "${c}" && {
      [ "${c}" != "${main}" ] && rec_arr+=("${c}")
    }
  done
}

function slow_rebuild {
  local recipe=("${1}")
  shift 1
  slow_try_get_cfiles recipe "${libs_parent}"
  recipe+=("${@}")

  if [ -f "${OUT_BIN}" ] && ! ${BUILD_REBUILD}; then
    for f in "${recipe[@]}"; do
      if [ "${f}" -nt "${OUT_BIN}" ]; then
        BUILD_REBUILD=true
        break
      fi
    done
  else
    BUILD_REBUILD=true
  fi

  if "${BUILD_REBUILD}"; then
    printf ' Compiling...\r'
    clang "${C_FLAGS[@]}" \
      -o "${OUT_BIN}" "${recipe[@]}" || exit
    printf '\x1b[0K Done\r'
  fi
}

function main {
  [ ! -d "${TARGET_DIR}" ] && mkdir -p "${TARGET_DIR}"

  declare -a C_FLAGS=(
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
  )

  read -r targets < <(yq '.targets[] | .name' "${ANVIL_YAML}" | tr '\n' ' ')

  barg::parse "${@}" << EOF
  #[always]
  meta {
    argv_zero: 'build'
    subcommand_required: true
    help_enabled: true
    spare_args_var: 'BUILD_ARGS'
  }
  commands {
    build: "(re)Build target, just that"
    check: "Run clang check on all C files"
    expand: "Only run the preprocessor, print code"
    *run: "Run target binary"
    *val: "Run target binary with Valgrind"
  }

  r/rebuild :flag => BUILD_REBUILD 'Rebuild even if no new changes'
  t/target [${targets}] ${targets%\ *} => BUILD_TARGET 'Select build target'
EOF

  local -a args=("${BUILD_ARGS[@]}")
  mapfile -t c_flags < <(yq -r '.profiles.release[]' anvil.yaml)
  read -r libs_parent < <(yq -r '.workspace.libs' anvil.yaml)
  local libs_parent="${libs_parent/#\#\{AWD\}/${THIS_PARENT}}"
  local target=$(yq -r ".targets[] | select (.name == \"${BUILD_TARGET}\")" anvil.yaml)

  read -r target_main < <(jq -r .main <<< "${target}")
  mapfile -t target_macros < <(jq -r '.macros | to_entries[] | "-D\(.key)=\(.value | @sh)"' <<< "${target}")
  OUT_BIN="${TARGET_DIR}/${BUILD_TARGET}"

  local target_main="${target_main/#\#\{AWD\}/${THIS_PARENT}}"

  case "${BARG_SUBCOMMAND}" in
    check) clang_check "${args[@]}" ;;
    expand) exec clang -E "-I${LIBS_DIR}" -x c -std=c23 "${RECIPE[@]}" ;;
  esac

  slow_rebuild "${target_main}" "${target_macros[@]}"

  case "${BARG_SUBCOMMAND}" in
    run) exec "${OUT_BIN}" "${args[@]}" ;;
    val)
      exec valgrind -s \
        --leak-check=full --show-leak-kinds=all \
        --track-origins=yes --verbose \
        "${OUT_BIN}" "${args[@]}"
      ;;
  esac
}

main "${@}"
