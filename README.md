# anvil
> A Cargo-inspired build tool for C — configured via a clean YAML subset.

No Makefiles. No CMake. No indentation-based YAML, no tags.  
Just a readable config file, a streaming parser built from scratch, and clang.

```yaml
package: "myapp"
version: "0.1"
description: "A #{C} project"
```

---

## Features

- **Multi-target builds** — `exec`, with cross-compilation support (`x86_64`, `aarch64`)
- **Release & debug profiles** — flags defined once, applied everywhere
- **Memoized build arguments** — run `git rev-parse HEAD` once, cache it
- **Hook scripts** — bash hooks for dynamic values at build time
- **Dependency management** — `pkg-config` for system libs, `github` for single-file headers
- **YAML anchor support** — define a profile once, reuse it with `*alias`
- **Builds itself** — anvil's own `anvil.yaml` is the reference config

---

## anvil.yaml

```yaml
package: "myapp"
version: "0.1"
author: "you"
description: "A #{C} project"

# #{AWD} = Anvil Work Dir (project root)
workspace: {
  libs:  "#{AWD}/src/libs",
  build: "#{AWD}/build"
}

# build targets — index 0 is default for `anvil build` / `anvil run`
# use `anvil build --target <n>` for others
targets: [
  {
    name: 'myapp',
    type: 'exec',
    main: '#{AWD}/src/main.c',
    macros: {},
    for: [
      'x86_64-linux-gnu',
      'aarch64-linux-gnu'
    ]
  }
]

build: {
  compiler: 'clang',
  cstd: 'c23',
  jobs: 0,  # 0 -> auto

  # #{arg:name} -> from arguments below
  # #{hook:name} -> from bash hook scripts in hooks/
  macros: {
    GIT_HASH: '#{arg:git_hash}',
    GIT_INFO: '#{hook:git_info}'
  },

  arguments: {
    git_hash: {
      validation:   'none',      # none | status | content | all
      cache_policy: 'memoize',   # never | memoize | always
      command: ['git', 'rev-parse', 'HEAD']
    }
  },

  deps: [
    {
      name: 'gtk4',
      type: 'pkg-config'         # uses system pkg-config
    },
    {
      name: 'stb_image',
      type: 'github',
      repo: 'nothings/stb',
      path: 'stb_image.h'        # single file download
      # path: 'include/'         # trailing / clones the folder as -Iinclude/
    }
  ]
}

# release and debug profiles are required
# tip: define release as an anchor, alias it in debug to share flags
profiles: {
  release: [
    '-O3', '-Wall', '-Wextra', '-Werror', '-pedantic', '-DNDEBUG'
  ],
  debug: [
    '-ggdb', '-O1', '-Werror',
    '-fstack-protector-strong',
    '-D_FORTIFY_SOURCE=2',
    '-fsanitize=address,undefined',
    '-fno-omit-frame-pointer'
  ]
}
```

---

## Error messages

anvil gives precise, context-aware errors pointing at the exact problem in your config (removed, to be added again):

```
YamlError:: UNEXPECTED_TOKEN
 18 |    here: 62
 19 |    other: "Hi"
         ^^^^^
 20 |  }
Expected TOKEN_COMMA, found TOKEN_KEY
```

---

## YAML subset

anvil uses a custom streaming YAML parser. Supported:

- Flow maps `{ key: value }` and flow sequences `[ a, b, c ]`
- Quoted strings (single `'...'` and double `"..."`)
- Numbers, booleans
- Anchors `&name` and aliases `*name` (including `<<` merge)
- Comments `# ...`

Not supported (by design):

- Indentation-based blocks
- Tags (`!!type`)
- Multiline scalars (`|`, `>`)

---

## Internal libraries

anvil is built on a small set of C23 single-header libraries:

| Library | Description |
|---|---|
| `z3_string.h` | Growable heap strings, interpolation, escape/unescape, scoped cleanup |
| `z3_hashmap.h` | FNV-1a HashMap, bit-packed occupation tracking, linear probing, iterator |
| `z3_vector.h` | Generic growable vector |
| `z3_toys.h` | Shared utilities, `next_power_of2`, `die`, debug helpers |

All are single-header with `#define Z3_*_IMPL` for the implementation, and Valgrind-clean on the happy path.

### HashMap (`z3_hashmap.h`)
- Occupation tracking **87.5% smaller** — `bool[]` replaced with packed `uint64_t` bitflags
- Per-entry size reduced **32 → 24 bytes** (~25%)
- Iteration cache locality improved → **~9% faster**

### YAML parser (`yaml.c`)
- Streaming chunk-based reads via raw fd — no `fgets`, no full file load
- String pool replaces per-node allocations
- Heap: **14,181 → 7,220 bytes (-48.9%)**
- Allocations: **151 → 107 (-29.1%)**

---

## Building

Requires clang and C23. Bootstrap without anvil:

```bash
clang -std=c23 -O2 -o anvil src/main.c
```

Then use anvil to build itself:

```bash
./anvil build                  # default target, release
./anvil build --profile debug  # debug profile
./anvil build --target 1       # yaml test binary
./anvil run                    # build + run default target
```

---

## License

AGPL-3.0-or-later — © 2025-present Klapptnot
