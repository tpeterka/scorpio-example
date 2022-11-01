#include <diy/mpi/communicator.hpp>
#include <thread>
#include "prod-con.hpp"

#include <pio.h>

#define NDIM 1
#define DIM_LEN 1024
#define DIM_NAME "x"
#define VAR_NAME "foo"
#define START_DATA_VAL 100

extern "C"
{
void consumer_f (
        communicator& local,
        const std::vector<communicator>& intercomms,
        bool shared,
        int metadata,
        int passthru,
        const std::unique_ptr<l5::MetadataVOL>& shared_vol_plugin);      // for single process, MetadataVOL test
}

void consumer_f (
        communicator& local,
        const std::vector<communicator>& intercomms,
        bool shared,
        int metadata,
        int passthru,
        const std::unique_ptr<l5::MetadataVOL>& shared_vol_plugin)      // for single process, MetadataVOL test
{
    diy::mpi::communicator local_(local);

    // PIO defs
    int my_rank = local_.rank();
    int ntasks  = local_.size();
    int ioproc_stride = 1;
    int ioproc_start = 0;
    int iosysid;
    int ncid;
    int format = PIO_IOTYPE_NETCDF4P;
    int *buffer = NULL;
    PIO_Offset elements_per_pe;
    PIO_Offset *compdof = NULL;
    int dim_len[1] = {DIM_LEN};
    int ioid;
    int varid;

    // debug
    fmt::print(stderr, "consumer: local comm rank {} size {}\n", my_rank, ntasks);

    // wait for data to be ready
    if (passthru && !metadata && !shared)
    {
        for (auto& intercomm: intercomms)
            diy_comm(intercomm).barrier();
    }

    // VOL plugin and properties
    std::unique_ptr<l5::DistMetadataVOL> vol_plugin{};
    hid_t plist;

    if (shared)                     // single process, MetadataVOL test
        fmt::print(stderr, "consumer: using shared mode MetadataVOL plugin created by prod-con\n");
    else                            // normal multiprocess, DistMetadataVOL plugin
    {
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
    }

//     // debugging
//     if (shared)
//     {
//         fmt::print(stderr, "Consumer metadata hierarchy:\n");
//         shared_vol_plugin->print_files();
//     }

    // init PIO
    PIOc_Init_Intracomm(local, ntasks, ioproc_stride, ioproc_start, PIO_REARR_SUBSET, &iosysid);

    // decomposition
    elements_per_pe = DIM_LEN / ntasks;
    compdof = (MPI_Offset*)(malloc(elements_per_pe * sizeof(PIO_Offset)));

    for (int i = 0; i < elements_per_pe; i++)
        compdof[i] = (my_rank * elements_per_pe + i + 1) + 10;

    PIOc_InitDecomp(iosysid, PIO_INT, NDIM, dim_len, (PIO_Offset)elements_per_pe, compdof, &ioid, NULL, NULL, NULL);
    free(compdof);

    // open file for reading
    PIOc_openfile(iosysid, &ncid, &format, "example1.nc", PIO_NOWRITE);

    // read the data
    buffer = (int*)(malloc(elements_per_pe * sizeof(int)));
    PIOc_inq_varid(ncid, VAR_NAME, &varid);
    PIOc_read_darray(ncid, varid, ioid, (PIO_Offset)elements_per_pe, buffer);
    free(buffer);

    // clean up
    PIOc_closefile(ncid);
    PIOc_freedecomp(iosysid, ioid);
    PIOc_finalize(iosysid);
    if (!shared)
        H5Pclose(plist);
}

