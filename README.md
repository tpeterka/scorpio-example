# Instructions for Building and Running Scorpio Example

Installation is done through Spack. If you don't have Spack installed or if Spack is new to you, go [here](https://spack.readthedocs.io/en/latest/) first.

## Setting up Spack environment

### First time: create and load the Spack environment

```
git clone https://github.com/tpeterka/scorpio-example
cd /path/to/scorpio-example
source ./create-env.sh     # requires being in the same directory to work properly
```

### Subsequent times: load the Spack environment

```
source /path/to/scorpio-example/load-env.sh
```

-----

## Running the example

```
cd $SCORPIO_EXAMPLE_PATH/bin
mpiexec -n 1 ./prod-con -m 0 -f 1
```



