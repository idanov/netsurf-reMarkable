# This shell fragment is intended for use in `bash` or `zsh`.  While it
# may work in other shells it is not meant to, and any misbehaviour is not
# considered a bug in that case.
#
# NetSurf Library, tool and browser development support script
#
# Copyright 2013-2017 Vincent Sanders <vince@netsurf-browser.org>
# Released under the MIT Licence
#
# This script allows NetSurf and its libraries to be built without
#   requiring installation into a system.
#
# Usage: source env.sh
#
# Controlling variables
#   HOST sets the target architecture for library builds
#   BUILD sets the building machines architecture
#   TARGET_WORKSPACE is the workspace directory to keep the sandboxes
#
# The use of HOST and BUILD here is directly comprable to the GCC
#   usage as described at:
#     http://gcc.gnu.org/onlinedocs/gccint/Configure-Terms.html
#

###############################################################################
# Setup environment
###############################################################################

# find which command used to find everything else on path
if [ -x /usr/bin/which ]; then
    WHICH_CMD=/usr/bin/which
else
    WHICH_CMD=/bin/which
fi

# environment parameters

# The system doing the building
if [ "x${BUILD}" = "x" ]; then
    BUILD_CC=$(${WHICH_CMD} cc)
    if [ $? -eq 0 ];then
        BUILD=$(cc -dumpmachine)
    else
       echo "Unable to locate a compiler. Perhaps run make image"
       return 1
    fi
fi

# Get the host build if unset
if [ "x${HOST}" = "x" ]; then
    if [ "x${TARGET_ABI}" = "x" ]; then
        HOST=${BUILD}
    else
        HOST=${TARGET_ABI}
    fi
else
    HOST_CC_LIST="/opt/netsurf/${HOST}/cross/bin/${HOST}-cc /opt/netsurf/${HOST}/cross/bin/${HOST}-gcc ${HOST}-cc ${HOST}-gcc"
    for HOST_CC_V in $(echo ${HOST_CC_LIST});do
        HOST_CC=$(${WHICH_CMD} ${HOST_CC_V})
        if [ "x${HOST_CC}" != "x" ];then
            break
        fi
    done
    if [ "x${HOST_CC}" = "x" ];then
        echo "Unable to execute host compiler for HOST=${HOST}. is it set correctly?"
        return 1
    fi

    HOST_CC_MACHINE=$(${HOST_CC} -dumpmachine 2>/dev/null)

    if [ "${HOST_CC_MACHINE}" != "${HOST}" ];then
        echo "Compiler dumpmachine differs from HOST setting"
        return 2
    fi

    NS_ENV_CC="${HOST_CC}"
    export NS_ENV_CC

    unset HOST_CC_LIST HOST_CC_V HOST_CC HOST_CC_MACHINE
fi

# set up a default target workspace
if [ "x${TARGET_WORKSPACE}" = "x" ]; then
    TARGET_WORKSPACE=${HOME}/dev-netsurf/workspace
fi

# set up default parallelism
if [ "x${USE_CPUS}" = "x" ]; then
    NCPUS=$(getconf _NPROCESSORS_ONLN 2>/dev/null || getconf NPROCESSORS_ONLN 2>/dev/null)
    NCPUS="${NCPUS:-1}"
    NCPUS=$((NCPUS * 2))
    USE_CPUS="-j${NCPUS}"
fi

# report to user
echo "BUILD=${BUILD}"
echo "HOST=${HOST}"
echo "TARGET_WORKSPACE=${TARGET_WORKSPACE}"
echo "USE_CPUS=${USE_CPUS}"

export PREFIX=${TARGET_WORKSPACE}/inst-${HOST}
export BUILD_PREFIX=${TARGET_WORKSPACE}/inst-${BUILD}
export PKG_CONFIG_PATH=${PREFIX}/lib/pkgconfig:${PKG_CONFIG_PATH}::
export LD_LIBRARY_PATH=${LD_LIBRARY_PATH}:${PREFIX}/lib
export PATH=${PATH}:${BUILD_PREFIX}/bin

# make tool
if [ -z ${MAKE+x} ]; then
    MAKE=make
fi

# NetSurf GIT repositories
NS_GIT="git://git.netsurf-browser.org"

# Buildsystem: everything depends on this
NS_BUILDSYSTEM="buildsystem"

# internal libraries all frontends require (order is important)
NS_INTERNAL_LIBS="libwapcaplet libparserutils libhubbub libdom libcss libnsgif libnsbmp libutf8proc libnsutils libnspsl libnslog"

# The browser itself
NS_BROWSER="netsurf"

# tools required to build the browser
NS_TOOLS="nsgenbind"
# additional internal libraries
NS_FRONTEND_LIBS="libsvgtiny libnsfb"

export MAKE

################ Development helpers ################

# git pull in all repos parameters are passed to git pull
ns-pull()
{
    for REPO in $(echo ${NS_BUILDSYSTEM} ${NS_INTERNAL_LIBS} ${NS_FRONTEND_LIBS} ${NS_TOOLS} ${NS_BROWSER}) ; do
        echo -n "     GIT: Pulling ${REPO}: "
        if [ -f "${TARGET_WORKSPACE}/${REPO}/.git/config" ]; then
            (cd ${TARGET_WORKSPACE}/${REPO} && git pull $*; )
        else
            echo "Repository not present"
        fi
    done
}

# clone all repositories
ns-clone()
{
    mkdir -p ${TARGET_WORKSPACE}
    for REPO in $(echo ${NS_BUILDSYSTEM} ${NS_INTERNAL_LIBS} ${NS_FRONTEND_LIBS} ${NS_RISCOS_LIBS} ${NS_TOOLS} ${NS_BROWSER}) ; do
        echo "GIT: Cloning ${REPO}: "
        if [ -f ${TARGET_WORKSPACE}/${REPO}/.git/config ]; then
            echo "Repository already present"
        else
            REPO_LOCATION="${NS_GIT}/${REPO}"

            REPO_REF="${REPO^^}_REPOSITORY"
            REPO_OVERRIDE="${!REPO_REF}"
            if [[ ! -z ${REPO_OVERRIDE} ]]; then
                echo "Using fork of repository at ${REPO_OVERRIDE}"
                REPO_LOCATION="${REPO_OVERRIDE}"
            fi

            VERSION_REF="${REPO^^}_VERSION"
            VERSION="${!VERSION_REF}"

            (cd ${TARGET_WORKSPACE} && git clone ${REPO_LOCATION}.git ${REPO}; )
            if [[ ! -z ${VERSION} ]]; then
                echo "Checking out ${VERSION} of ${REPO}"
                (cd ${TARGET_WORKSPACE}/${REPO} && git -c advice.detachedHead=false checkout ${VERSION}; )
            fi

        fi
        echo
    done
}

# issues a make command to all libraries
ns-make-libs()
{
    for REPO in $(echo ${NS_BUILDSYSTEM} ${NS_INTERNAL_LIBS} ${NS_FRONTEND_LIBS}); do
        echo "    MAKE: make -C ${REPO} $USE_CPUS $*"
        ${MAKE} -C ${TARGET_WORKSPACE}/${REPO} HOST=${HOST} $USE_CPUS $*
        if [ $? -ne 0 ]; then
            return $?
        fi
    done
}

# issues make command for all tools
ns-make-tools()
{
    for REPO in $(echo ${NS_BUILDSYSTEM} ${NS_TOOLS}); do
        echo "    MAKE: make -C ${REPO} $USE_CPUS $*"
        ${MAKE} -C ${TARGET_WORKSPACE}/${REPO} PREFIX=${BUILD_PREFIX} HOST=${BUILD} $USE_CPUS $*
        if [ $? -ne 0 ]; then
            return $?
        fi
    done
}

# issues a make command for framebuffer libraries
ns-make-libnsfb()
{
    echo "    MAKE: make -C libnsfb $USE_CPUS $*"
    ${MAKE} -C ${TARGET_WORKSPACE}/libnsfb HOST=${HOST} $USE_CPUS $*
}

# pulls all repos and makes and installs the libraries and tools
ns-pull-install()
{
    ns-pull $*

    ns-make-tools install
    ns-make-libs install
}

# Passes appropriate flags to make
ns-make()
{
    ${MAKE} $USE_CPUS "$@"
}
