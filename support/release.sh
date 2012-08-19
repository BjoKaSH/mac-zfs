#!/bin/sh
#Generates a new release for the projects

DIR=`dirname $0`
BUILD=build

VERSION=`${DIR}/version.sh`
if [ -z "${VERSION}" ]
then
	echo "Unable to parse version"
	exit 1
fi

CONFIG=Release
if [ "--debug" == "$1" ]
then
	CONFIG=Debug
fi

USER=`whoami`
GROUP=`id -gn`

(
mkdir -p ${DIR}/../${BUILD}
rm -rf ${DIR}/../${BUILD}/ZFS105
rm -rf ${DIR}/../${BUILD}/ZFS106
cp -R ${DIR}/MacZFS.pmdoc ${DIR}/../${BUILD}
cp -R ${DIR}/post-install.sh ${DIR}/../${BUILD}
sed -i~  -e "s/0\\.0\\.0/${VERSION}/g" ${DIR}/../${BUILD}/MacZFS.pmdoc/*

cd ${DIR}/..
xcodebuild -sdk macosx10.5 -configuration ${CONFIG} -parallelizeTargets install INSTALL_OWNER=${USER} INSTALL_GROUP=${GROUP} SYMROOT=${BUILD}/${CONFIG}105 DSTROOT=${BUILD}/ZFS105 || exit 2
chmod -Rv u+w,go-w,a+r ${BUILD}/ZFS105
echo "explicitly setting group and ownership, enter sudo password if prompted"
sudo chown -Rv root:wheel ${BUILD}/ZFS105
# No point in building on 10.5, it'll just do the same thing again
if [ "`sysctl -b kern.osrelease`" != "9.8.0" ] 
then
	xcodebuild -sdk macosx10.6 -configuration ${CONFIG} -parallelizeTargets install INSTALL_OWNER=${USER} INSTALL_GROUP=${GROUP} SYMROOT=${BUILD}/${CONFIG}106 DSTROOT=${BUILD}/ZFS106 || exit 3
	chmod -R u+w,go-w,a+r ${BUILD}/ZFS106
	sudo chown -Rv root:wheel ${BUILD}/ZFS106
fi

cd ${BUILD}
# run packagemaker and save exit code, do NOT abort! We must fix permissions first.
packagemaker --doc MacZFS.pmdoc --version ${VERSION} --title "Mac ZFS ${VERSION}" --out MacZFS-${VERSION}.pkg --target 10.5
err=$?
sudo chown -R ${USER}:${GROUP} ZFS10?
if [ ${err} -ne 0 ] ; then
	exit 4
fi 
)
