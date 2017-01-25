# Docker container for building binaries

* based on instructions in [BUILDING.md](../BUILDING.md)
* it works

build and start container on host
```bash
$ docker build -t startkladde-build .
$ cd ..
$ docker run -ti -v $(pwd):/startkladde -w /startkladde startkladde-build:latest /bin/bash
```

build inside container
```bash
root@494eb777295d:/startkladde# mkdir -p build; cd build
root@494eb777295d:/startkladde/build# cmake ..; make -j4 clean all
root@494eb777295d:/startkladde/build# ls startkladde -l
-rwxr-xr-x 1 root root 2562872 Jan 25 19:50 startkladde
```
