# How to run diskquota regression tests

## For versions prior 2.0.0

1. Apply the next patch for builds from arenadata fork:

        diff --git a/concourse/scripts/build_diskquota.sh b/concourse/scripts/build_diskquota.sh
        index 1c18f57..6a4ffda 100755
        --- a/concourse/scripts/build_diskquota.sh
        +++ b/concourse/scripts/build_diskquota.sh
        @@ -26,6 +26,7 @@ function pkg() {
                lib/postgresql/diskquota.so \
                share/postgresql/extension/diskquota.control \
                share/postgresql/extension/diskquota--1.0.sql \
        +        share/postgresql/extension/diskquota--1.0.3.sql \
                install_gpdb_component
            ;;
            rhel7)
        @@ -33,6 +34,7 @@ function pkg() {
                lib/postgresql/diskquota.so \
                share/postgresql/extension/diskquota.control \
                share/postgresql/extension/diskquota--1.0.sql \
        +        share/postgresql/extension/diskquota--1.0.3.sql \
                install_gpdb_component
            ;;
            ubuntu18.04)
        @@ -40,6 +42,7 @@ function pkg() {
                lib/postgresql/diskquota.so \
                share/postgresql/extension/diskquota.control \
                share/postgresql/extension/diskquota--1.0.sql \
        +        share/postgresql/extension/diskquota--1.0.3.sql \
                install_gpdb_component
            ;;
            *) echo "Unknown OS: $OSVER"; exit 1 ;;
        diff --git a/concourse/scripts/test_diskquota.sh b/concourse/scripts/test_diskquota.sh
        index 31cc052..bbb5aaf 100755
        --- a/concourse/scripts/test_diskquota.sh
        +++ b/concourse/scripts/test_diskquota.sh
        @@ -54,7 +54,7 @@ function _main() {
                time make_cluster
                time install_diskquota
                if [ "${DISKQUOTA_OS}" == "rhel7" ]; then
        -               CUT_NUMBER=5
        +               CUT_NUMBER=4
                fi

                time test

1. Build diskquota artifacts:

        docker run --rm -it -e DISKQUOTA_OS=rhel7 --sysctl 'kernel.sem=500 1024000 200 4096' \
            -v /tmp/diskquota_artifacts:/home/gpadmin/diskquota_artifacts \
            -v $PWD:/home/gpadmin/diskquota_src hub.adsw.io/library/gpdb6_regress:latest \
            bash -c 'diskquota_src/concourse/scripts/build_diskquota.sh || bash'

1. Run regression tests:

        docker run --rm -it -e DISKQUOTA_OS=rhel7 --sysctl 'kernel.sem=500 1024000 200 4096' \
            -v /tmp/diskquota_artifacts:/home/gpadmin/bin_diskquota \
            -v $PWD:/home/gpadmin/diskquota_src hub.adsw.io/library/gpdb6_regress:latest \
            bash -c 'diskquota_src/concourse/scripts/test_diskquota.sh || bash'

## For version since 2.0.0

1. Build artifacts for previous version (see above).
1. Remove arenadata suffix from tarball name
1. Download cmake [3.18.0](https://cmake.org/files/v3.18/cmake-3.18.0-Linux-x86_64.sh)
1. Build docker container from [arenadata/Dockerfile](./Dockerfile):

        docker build -t diskquota_regress:latest -f arenadata/Dockerfile .

1. Build artifacts:

        docker run --rm -it -e DISKQUOTA_OS=rhel7 --sysctl 'kernel.sem=500 1024000 200 4096' --privileged \
            -v /tmp/diskquota_artifacts:/tmp/build/last_released_diskquota_bin \
            -v /tmp/diskquota_artifacts_2:/tmp/build/diskquota_artifacts \
            -v $PWD:/tmp/build/diskquota_src \
            -v ~/Downloads/cmake-3.18.0-linux-x86_64.sh:/tmp/build/bin_cmake/cmake-3.18.0-linux-x86_64.sh \
            diskquota_regress:latest bash -c "diskquota_src/concourse/scripts/entry.sh build || bash"

1. Run regression tests:

        docker run --rm -it --sysctl 'kernel.sem=500 1024000 200 4096' \
            -v /tmp/diskquota_artifacts:/tmp/build/last_released_diskquota_bin \
            -v /tmp/diskquota_artifacts_2:/tmp/build/bin_diskquota \
            -v $PWD:/tmp/build/diskquota_src \
            -v ~/Downloads/cmake-3.18.0-linux-x86_64.sh:/tmp/build/bin_cmake/cmake-3.18.0-linux-x86_64.sh \
            diskquota_regress:latest bash -c "diskquota_src/concourse/scripts/entry.sh test || bash"
