FROM hub.adsw.io/library/gpdb6_regress:latest

RUN source /home/gpadmin/gpdb_src/concourse/scripts/common.bash \
  && configure \
  && rm -rf /home/gpadmin/bin_gpdb/bin_gpdb.tar.gz \
  && mkdir /tmp/build \
  && mv -t /tmp/build/ /home/gpadmin/gpdb_src /home/gpadmin/bin_gpdb

WORKDIR /tmp/build
