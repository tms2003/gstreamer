#!/bin/sh

tmpdir=`mktemp --tmpdir -d gst.XXXXXXXXXX`
workdir=$PWD
cd $tmpdir
res=0
elements="audiodecoder audioencoder audiofilter audiosink audiosrc baseparse basesink basesrc basetransform element videodecoder videoencoder videofilter videosink"

for element in $elements; do
  ${workdir}/gst-element-maker gst$element $element
  if test $? -ne 0; then
    res=1
    break
  fi
done

cd $workdir
rm -rf $tmpdir
exit $res;

