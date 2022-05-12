#!/bin/sh -e

## Proposal:
# give generated files .lst extension
# don't give non-generated files .lst extension

LC_ALL=C

sort -u b2ld.list d2ld.list g2ld.list r2ld.list f2ld.list | grep -vE '^(tk|tel|mp|planeta\.rambler\.ru)$' >j-owners-spm.lst
cp -p j-owners-spm.lst areas.lst
grep -wvFf no-serp-2ld.list j-owners-spm.lst >2ld.list || true

sort -u b2ld.list f2ld.list >hostings.lst

awk '{print $1}' xrdr.excl >xrdr-excl.lst

(
 echo com.ru
 echo net.ru
 echo org.ru
 echo pp.ru
 cat relcom-free.txt
 cat reg-free.txt
 echo tk
) | sort -u >freezones.lst

sort -u freezones.lst g2ld.list r2ld.list | grep -v '^tk$' >nsareas.lst

echo OK

#### Note: don't forget to update it ####
#
# yr +WALRUS +wspm +wspm2 +walrus +primus +tsa +ya +mfas +mfas001 +sas00 +sas01 +ossa2 +PRIMUS +ZM 'cd /Berkanavt/urlrules; svn up'
# scp -p j-owners-spm.lst areas.lst akita:/Berkanavt/urlrules/
# (cd /opt/zmimage/place/berkanavt/urlrules/; svn up)
#
####
