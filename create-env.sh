#!/bin/bash

export SPACKENV=scorpio-example-env
export YAML=$PWD/env.yaml

# add mpas-o-scorpio and scorpio-example spack repos
echo "adding custom spack repos for scorpio and for scorpio-example"
spack repo add mpas-o-scorpio > /dev/null 2>&1
spack repo add . > /dev/null 2>&1

# create spack environment
echo "creating spack environment $SPACKENV"
spack env deactivate > /dev/null 2>&1
spack env remove -y $SPACKENV > /dev/null 2>&1
spack env create $SPACKENV $YAML

# activate environment
echo "activating spack environment"
spack env activate $SPACKENV

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
export SCORPIO_EXAMPLE_PATH=`spack location -i scorpio-example`
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

