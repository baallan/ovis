#! /bin/bash
# input file is output of ibnetdiscover -p
x=$1
if test -z "$x"; then
	x=stria-p-netdiscover
fi
SRC="cat $x"
SRC=ibnetdiscover
echo '# switches'
echo '# lid guid port-count portlist'
$SRC |grep -v sharp | grep ^SW | grep -v '???' | sort -n -k 2 -k 3 | sed -e 's/^SW *//' -e 's/ .x .*//' | while read -r lid port guid; do
  echo $lid $guid 1 $port
done 
echo '# hcas'
echo '# lid guid port-count portlist'
$SRC |grep -v sharp | grep ^CA | grep -v '???' | sort -n -k 2 -k 3 | sed -e 's/^CA *//' -e 's/ .x .*//' | while read -r lid port guid; do
  echo $lid $guid 1 $port
done 
