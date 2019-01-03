# safe-chain-name

SCN = Safe-Chain-Name

SCN specification: c/c++ legal label

## how to set SCN

### configure

```bash
./configure --prefix=xxx --with-incompatible-bdb --disable-tests --disable-bench --with-gui=xxx CPPFLAGS=-DSAFE_CHAIN_NAME=yyy
```

`yyy`: main or dev or test; main if not specified.

### safe-chain-name.h

pre-define safe-chain-name: `SCN__main`, `SCN__dev`, `SCN__test`.

-DSAFE_CHAIN_NAME=`yyy` to set `SCN_CURRENT` as pre-define SCN.

if `yyy` is not any pre-define SCN, a error will be raised.

## how to use SCN

### source code file

any source code file that determines `SCN_CURRENT` by `#if` would `#include safe-chain-name.h` firstly. sample code like:

```c
/* src/clientversion.cpp */

#include "config/safe-chain.h"

#if SCN_CURRENT == SCN__main
#define CLIENT_VERSION_SUFFIX ""
#elif SCN_CURRENT == SCN__dev
#define CLIENT_VERSION_SUFFIX "-Dev"
#elif SCN_CURRENT == SCN__test
#define CLIENT_VERSION_SUFFIX "-Test"
#else
#error unsupported <safe chain name>
#endif
```

### find files using SCN

```bash
find ./ -type f -name '*.cpp' -o -name '*.h' | xargs grep --color SCN_CURRENT -l
```

## how to add new SCN

### 1. safe-chain-name.h

add pre-define safe-chain-name.

### 2. source code file

all files using SCN need to add new `#if-#elif-#else-#endif` to determine new SCN__yyy.

### 3. configure

```bash
./configure --prefix=xxx --with-incompatible-bdb --disable-tests --disable-bench --with-gui=xxx CPPFLAGS=-DSAFE_CHAIN_NAME=yyy
```

`yyy`: new SCN.