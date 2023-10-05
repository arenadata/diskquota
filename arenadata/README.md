# Run regression tests in Docker container

You can build your Docker image from GPDB source or use prebuilt images from hub.adsw.io.
How to build Docker image: (["readme.md"](https://github.com/arenadata/gpdb/blob/f7ff7c8ecae4ce7ab3b73fd46171cdaa457b3591/arenadata/readme.md)).

1. Download the cmake-3.20 install script from ([source](https://github.com/Kitware/CMake/releases/download/v3.20.0/cmake-3.20.0-linux-x86_64.sh)).

2. Prepare diskquota-<LAST_DISKQUOTA_TAG>.tar.gz which must contains all <MAJOR>.<MINOR> versions of diskquota. For each versions you need to use last tag (see `git tag`). Name of this archive must be `diskquota-<LAST_DISKQUOTA_TAG>.tar.gz`, where <LAST_DISKQUOTA_TAG> is tag for the latest version of diskquota in this archive. All `*.so` files must be at `lib/postgresql` path:

```
evgeniy@evgeniy-pc:~/gpdb/diskquota_bin$ tar -tvf diskquota-2.2.1_arenadata3.tar.gz
drwxrwxr-x evgeniy/evgeniy   0 2023-10-05 14:40 lib/
drwxrwxr-x evgeniy/evgeniy   0 2023-10-05 14:41 lib/postgresql/
-rwxr-xr-x evgeniy/evgeniy 281632 2023-10-03 13:55 lib/postgresql/diskquota.so
-rwxr-xr-x evgeniy/evgeniy 550080 2023-10-03 13:53 lib/postgresql/diskquota-2.0.so
-rwxr-xr-x evgeniy/evgeniy 619824 2023-10-03 13:51 lib/postgresql/diskquota-2.1.so
-rwxr-xr-x evgeniy/evgeniy 755664 2023-10-04 10:11 lib/postgresql/diskquota-2.2.so
```

This archive is needed for run upgrade tests for diskquota. This tests may be disabled (see ([commit](https://github.com/arenadata/diskquota/commit/50ed2e4e1883ec8ec4e7086b750cb28cdc5a2dc0)).

3. Build diskquota in the Docker container.
Change <PATH_TO_DISKQUOTA_SRC> and <PATH_TO_CMAKE_INSTALL_SCRIPT> to the appropriate paths on your local machine. <LAST_DISKQUOTA_TAG> is the same as at step 2. If upgrade test is disabled, line with <PATH_TO_ARCHIVE_WITH_OLD_VERSIONS> is not needed.

```
docker run --rm -it -e DISKQUOTA_OS=rhel7 \
       -v /tmp/diskquota_artifacts:/home/gpadmin/diskquota_artifacts \
       -v <PATH_TO_DISKQUOTA_SRC>:/home/gpadmin/diskquota_src \
       -v <PATH_TO_CMAKE_INSTALL_SCRIPT>:/home/gpadmin/bin_cmake/cmake-3.20.0-linux-x86_64.sh \
       -v <PATH_TO_ARCHIVE_WITH_OLD_VERSIONS>:/home/gpadmin/last_released_diskquota_bin/diskquota-<LAST_DISKQUOTA_TAG>.tar.gz \
       hub.adsw.io/library/gpdb6_regress:latest diskquota_src/concourse/scripts/entry.sh build
```

4. Run tests.
Change <PATH_TO_DISKQUOTA_SRC> and <PATH_TO_CMAKE_INSTALL_SCRIPT> to the appropriate paths on your local machine.

```
docker run --rm -it --sysctl 'kernel.sem=500 1024000 200 4096' \
       -v /tmp/diskquota_artifacts:/home/gpadmin/bin_diskquota \
       -v <PATH_TO_DISKQUOTA_SRC>:/home/gpadmin/diskquota_src \
       -v <PATH_TO_CMAKE_INSTALL_SCRIPT>:/home/gpadmin/bin_cmake/cmake-3.20.0-linux-x86_64.sh \
       hub.adsw.io/library/gpdb6_regress:latest diskquota_src/concourse/scripts/entry.sh test
```
