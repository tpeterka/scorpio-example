#!/bin/bash

# activate the environment
export SPACKENV=scorpio-example-env
spack env deactivate > /dev/null 2>&1
spack env activate $SPACKENV
echo "activated spack environment $SPACKENV"

echo "setting flags for building scorpio-example"
export NETCDF_PATH=`spack location -i netcdf-c`
export PNETCDF_PATH=`spack location -i parallel-netcdf`
export SCORPIO_PATH=`spack location -i mpas-o-scorpio`
export LOWFIVE_PATH=`spack location -i lowfive`

echo "setting flags for running scorpio-example"
export LD_LIBRARY_PATH=$NETCDF_PATH/lib:$LD_LIBRARY_PATH
export LD_LIBRARY_PATH=$PNETCDF_PATH/lib:$LD_LIBRARY_PATH
export LD_LIBRARY_PATH=$SCORPIO_PATH/lib:$LD_LIBRARY_PATH
export LD_LIBRARY_PATH=$LOWFIVE_PATH/lib:$LD_LIBRARY_PATH

export HDF5_PLUGIN_PATH=$LOWFIVE_PATH/lib
export HDF5_VOL_CONNECTOR="lowfive under_vol=0;under_info={};"


