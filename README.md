 # Masstree-Wrapper

masstree-wrapper is a wrapper of masstree-beta (https://github.com/kohler/masstree-beta.git) written by Eddie Kohler.

Currently it supports the following methods

- get
- insert
- update
- remove
- scan
- rscan
# Build & Execute

The following code will fetch the latest masstree-beta and executes some tests for the wrapper. Some warnings might show up during the build due to the compilation of masstree-beta using cmake.

```sh
mkdir build
cd build
cmake ..
make
cd bin
./main
```

# Usage

See `src/main.cpp`.