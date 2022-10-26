#include <diy/mpi/communicator.hpp>
#include "prod-con.hpp"

#include <pio.h>

#define NDIM 1
#define DIM_LEN 1024
#define DIM_NAME "x"
#define VAR_NAME "foo"
#define START_DATA_VAL 100

herr_t fail_on_hdf5_error(hid_t stack_id, void*)
{
    H5Eprint(stack_id, stderr);
    fprintf(stderr, "An HDF5 error was detected. Terminating.\n");
    exit(1);
}

extern "C" {
void producer_f (
        communicator& local,
        const std::vector<communicator>& intercomms,
        std::mutex& exclusive,
        bool shared,
        int metadata,
        int passthru);
}

void producer_f (
        communicator& local,
        const std::vector<communicator>& intercomms,
        std::mutex& exclusive,
        bool shared,
        int metadata,
        int passthru)
{
    diy::mpi::communicator local_(local);

    // PIO defs
    int my_rank = local_.rank();
    int ntasks  = local_.size();
    int format = PIO_IOTYPE_NETCDF4P;
    int ioproc_stride = 1;
    int ioproc_start = 0;
    int dimid;
    PIO_Offset elements_per_pe;
    int dim_len[1] = {DIM_LEN};
    int iosysid;
    int ncid;
    int varid;
    int ioid;
    int *buffer = NULL;
    PIO_Offset *compdof = NULL;

    // debug
    fmt::print(stderr, "producer: local comm rank {} size {}\n", my_rank, ntasks);

    // VOL plugin and properties
    std::unique_ptr<l5::DistMetadataVOL> vol_plugin {};
    hid_t plist;

    vol_plugin = std::unique_ptr<l5::DistMetadataVOL>(new l5::DistMetadataVOL(local, intercomms));
    plist = H5Pcreate(H5P_FILE_ACCESS);

    if (passthru)
        H5Pset_fapl_mpio(plist, local, MPI_INFO_NULL);

    l5::H5VOLProperty vol_prop(*vol_plugin);
    if (!getenv("HDF5_VOL_CONNECTOR"))
        vol_prop.apply(plist);

    // set lowfive properties
    LowFive::LocationPattern all { "example1.nc", "*"};
    if (passthru)
        vol_plugin->passthru.push_back(all);
    if (metadata)
        vol_plugin->memory.push_back(all);

    // set Scorpio log level
//     PIOc_set_log_level(5);

    // set HDF5 error handler
    H5Eset_auto(H5E_DEFAULT, fail_on_hdf5_error, NULL);

    // init PIO
    PIOc_Init_Intracomm(local, ntasks, ioproc_stride, ioproc_start, PIO_REARR_SUBSET, &iosysid);

    // decomposition
    elements_per_pe = DIM_LEN / ntasks;
    compdof = (MPI_Offset*)(malloc(elements_per_pe * sizeof(PIO_Offset)));

    for (int i = 0; i < elements_per_pe; i++)
        compdof[i] = (my_rank * elements_per_pe + i + 1) + 10;

    PIOc_InitDecomp(iosysid, PIO_INT, NDIM, dim_len, (PIO_Offset)elements_per_pe, compdof, &ioid, NULL, NULL, NULL);
    free(compdof);

    // create file
    PIOc_createfile(iosysid, &ncid, &format, "example1.nc", PIO_CLOBBER);

    // define variables
    PIOc_def_dim(ncid, DIM_NAME, (PIO_Offset)dim_len[0], &dimid);
    PIOc_def_var(ncid, VAR_NAME, PIO_INT, NDIM, &dimid, &varid);
    PIOc_enddef(ncid);

    // write the data
    buffer = (int*)(malloc(elements_per_pe * sizeof(int)));
    for (int i = 0; i < elements_per_pe; i++)
        buffer[i] = START_DATA_VAL + my_rank;
    PIOc_write_darray(ncid, varid, ioid, (PIO_Offset)elements_per_pe, buffer, NULL);
    free(buffer);

    // debugging
//     vol_plugin.print_files();

    // debug
    fmt::print(stderr, "producer 1:\n");

    // clean up
    PIOc_closefile(ncid);
    PIOc_freedecomp(iosysid, ioid);
    PIOc_finalize(iosysid);

    // debug
    fmt::print(stderr, "producer 2:\n");

    // signal the consumer that data are ready
    if (!shared)
    {
        for (auto& intercomm: intercomms)
            diy_comm(intercomm).barrier();
    }
    else
    {
        local_.barrier();
        int a = 0;                          // it doesn't matter what we send, for synchronization only
        for (auto& intercomm : intercomms)
            diy_comm(intercomm).send(local_.rank(), 0, a);
    }
}
