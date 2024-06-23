#!/bin/bash

export SPACKENV=scorpio-example-env
export YAML=$PWD/env.yaml

# create spack environment
echo "creating spack environment $SPACKENV"
spack env deactivate > /dev/null 2>&1
spack env remove -y $SPACKENV > /dev/null 2>&1
spack env create $SPACKENV $YAML

# activate environment
echo "activating spack environment"
spack env activate $SPACKENV

spack develop lowfive@master build_type=Debug
spack add lowfive

spack develop netcdf-c@main+mpi build_system=cmake build_type=Debug
spack add netcdf-c@main+mpi

spack develop mpas-o-scorpio@master+hdf5 build_type=Debug
spack add mpas-o-scorpio+hdf5

spack develop scorpio-example@master
spack add scorpio-example

# install everything in environment
echo "installing dependencies in environment"
spack install

spack env deactivate

