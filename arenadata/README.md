# Run regression tests in Docker container

1. Build Docker container with GPDB from source directory:
```
docker build -t "adb-6.x-dev:latest" -f arenadata/Dockerfile .
```

2. Download cmake-3.20 install script from https://github.com/Kitware/CMake/releases/download/v3.20.0/cmake-3.20.0-linux-x86_64.sh

3. Build diskquota in Docker container
Change <PATH_TO_DISKQUOTA> and <PATH_TO_CMAKE_INSTALL_SCRIPT> to appropriate paths at your local machine

```
docker run --rm -it -e DISKQUOTA_OS=rhel7 \
       -v /tmp/diskquota_artifacts_2:/home/gpadmin/diskquota_artifacts \
       -v <PATH_TO_DISKQUOTA>:/home/gpadmin/diskquota_src \
       -v <PATH_TO_CMAKE_INSTALL_SCRIPT>:/home/gpadmin/bin_cmake/cmake-3.20.0-linux-x86_64.sh \
       adb-6.x-dev:latest bash -c "/home/gpadmin/diskquota_src/concourse/scripts/entry.sh build"
```

4. Run tests
Change <PATH_TO_DISKQUOTA> and <PATH_TO_CMAKE_INSTALL_SCRIPT> to appropriate paths at your local machine

```
docker run --rm -it --sysctl 'kernel.sem=500 1024000 200 4096' \
       -v /tmp/diskquota_artifacts_2:/home/gpadmin/bin_diskquota \
       -v <PATH_TO_DISKQUOTA>:/home/gpadmin/diskquota_src \
       -v <PATH_TO_CMAKE_INSTALL_SCRIPT>:/home/gpadmin/bin_cmake/cmake-3.20.0-linux-x86_64.sh \
       adb-6.x-dev:latest bash -c "/home/gpadmin/diskquota_src/concourse/scripts/entry.sh test"
```
