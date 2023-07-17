#!/bin/bash

export SPACKENV=~/Build/ISDM/scorpio-example/scorpio-example-env
export YAML=$PWD/env.yaml

# add mpas-o-scorpio and scorpio-example spack repos
echo "adding custom spack repos for scorpio and for scorpio-example"
spack repo add mpas-o-scorpio > /dev/null 2>&1
spack repo add . > /dev/null 2>&1
echo "adding spack repo for lowfive"
spack repo add lowfive > /dev/null 2>&1

# create spack environment
echo "creating spack environment $SPACKENV"
#spack env deactivate > /dev/null 2>&1
#spack env remove -y $SPACKENV > /dev/null 2>&1
spack env create -d $SPACKENV # $YAML

# activate environment
echo "activating spack environment"
spack env activate $SPACKENV

spack add mpich@4
spack add henson+python+mpi-wrappers build_type=Debug

spack develop lowfive@master
spack add lowfive@master build_type=Debug

# add scorpio in develop mode
spack develop mpas-o-scorpio+hdf5@master
spack add mpas-o-scorpio+hdf5 build_type=Debug

## add scorpio-example in develop mode
#spack develop scorpio-example@master
#spack add scorpio-example build_type=Debug

# add netcdf in develop mode
spack develop netcdf-c@4.8.1+mpi
spack add netcdf-c@4.8.1+mpi cflags='-g' # build_type=Debug build_system=cmake

spack add parallel-netcdf
spack add netcdf-fortran@4.5.3

#spack add python
#spack add rr

# install everything in environment
echo "installing dependencies in environment"
spack install

# reset the environment (workaround for spack behavior)
spack env deactivate
spack env activate $SPACKENV

# set build flags
echo "setting flags for building scorpio-example"
export NETCDF_PATH=`spack location -i netcdf-c`
export PNETCDF_PATH=`spack location -i parallel-netcdf`
export SCORPIO_PATH=`spack location -i mpas-o-scorpio`
export LOWFIVE_PATH=`spack location -i lowfive`
#export SCORPIO_EXAMPLE_PATH=`spack location -i scorpio-example`
export HENSON_PATH=`spack location -i henson`

# set LD_LIBRARY_PATH
echo "setting flags for running scorpio-example"
export LD_LIBRARY_PATH=$NETCDF_PATH/lib:$LD_LIBRARY_PATH
export LD_LIBRARY_PATH=$PNETCDF_PATH/lib:$LD_LIBRARY_PATH
export LD_LIBRARY_PATH=$SCORPIO_PATH/lib:$LD_LIBRARY_PATH
export LD_LIBRARY_PATH=$LOWFIVE_PATH/lib:$LD_LIBRARY_PATH
export LD_LIBRARY_PATH=$HENSON_PATH/lib:$LD_LIBRARY_PATH

export HDF5_PLUGIN_PATH=$LOWFIVE_PATH/lib
export HDF5_VOL_CONNECTOR="lowfive under_vol=0;under_info={};"

