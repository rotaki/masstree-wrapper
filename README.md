 # Masstree-Wrapper

masstree-wrapper is a wrapper of masstree-beta (https://github.com/kohler/masstree-beta.git) written by Eddie Kohler.

Currently it supports the following methods

- get
- insert
- update
- remove

# Build & Execute

The following code will fetch the latest masstree-beta and executes the tests of wrapper. Some warnings might show up during the build but they are the problems caused by compiling masstree-beta with cmake.

```sh
mkdir build
cd build
cmake ..
make
cd bin
./main
```