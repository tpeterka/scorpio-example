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

### First time: create an example1.nc file

Because of a quirk in the way that SCORPIO I/O works, there needs to be an `example1.nc` file on disk, otherwise SCORPIO
will complain. This means that you need to run the example once in passthru mode, and replace the file anytime it gets
deleted, e.g., if the binary directory is re-installed.
```
cd $SCORPIO_EXAMPLE_PATH/bin
mpiexec -n 1 ./prod-con -m 0 -f 1
```

### Shared mode: one MPI rank, producer and consumer take turns on same rank

passthru mode
```
cd $SCORPIO_EXAMPLE_PATH/bin
mpiexec -n 1 ./prod-con -m 0 -f 1
```
memory mode
```
cd $SCORPIO_EXAMPLE_PATH/bin
mpiexec -n 1 ./prod-con
```
### Distributed mode: two MPI ranks, 1 rank producer + 1 rank consumer

passthru mode
```
cd $SCORPIO_EXAMPLE_PATH/bin
mpiexec -n 2 ./prod-con -m 0 -f 1
```
memory mode
```
cd $SCORPIO_EXAMPLE_PATH/bin
mpiexec -n 2 ./prod-con
```



