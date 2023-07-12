#include    <diy/master.hpp>
#include    <diy/decomposition.hpp>
#include    <diy/assigner.hpp>
#include    "opts.h"

#include    <lowfive/log.hpp>

#include    <dlfcn.h>

#include    "prod-con.hpp"

int main(int argc, char* argv[])
{
    diy::mpi::environment     env(argc, argv, MPI_THREAD_MULTIPLE);
    diy::mpi::communicator    world;

    int                       mem_blocks        = -1;             // all blocks in memory
    int                       threads           = 1;              // no multithreading
    int                       metadata          = 1;              // build in-memory metadata
    int                       passthru          = 0;              // write file to disk
    bool                      shared            = false;          // producer and consumer run on the same ranks
    float                     prod_frac         = 1.0 / 2.0;      // fraction of world ranks in producer
    std::string               producer_exec     = "./producer.so";    // name of producer executable
    std::string               consumer_exec     = "./consumer.so";    // name of consumer executable
    int                       ntrials           = 1;              // number of trials to run
    bool                      verbose, help;

    // get command line arguments
    using namespace opts;
    Options ops;
    ops
        >> Option('t', "thread",    threads,        "number of threads")
        >> Option(     "memblks",   mem_blocks,     "number of blocks to keep in memory")
        >> Option('m', "memory",    metadata,       "build and use in-memory metadata")
        >> Option('f', "file",      passthru,       "write file to disk")
        >> Option('p', "p_frac",    prod_frac,      "fraction of world ranks in producer")
        >> Option('s', "shared",    shared,         "share ranks between producer and consumer (-p ignored)")
        >> Option('r', "prod_exec", producer_exec,  "name of producer executable")
        >> Option('c', "con_exec",  consumer_exec,  "name of consumer executable")
        >> Option('v', "verbose",   verbose,        "print the block contents")
        >> Option('h', "help",      help,           "show help")
        >> Option(     "ntrials",   ntrials,        "number of trials to run")
        ;

    if (!ops.parse(argc,argv) || help)
    {
        if (world.rank() == 0)
        {
            std::cout << "Usage: " << argv[0] << " [OPTIONS]\n";
            std::cout << "Generates a grid and random particles in the domain and redistributes them into correct blocks.\n";
            std::cout << ops;
        }
        return 1;
    }

    // lowfive logging
    LowFive::create_logger("trace");

    int producer_ranks = world.size() * prod_frac;
    bool producer           = world.rank() < producer_ranks;
    if (world.size() == 1)
        shared = true;

    if (!shared && world.rank() == 0)
        fmt::print(stderr, "space partitioning: producer_ranks: {} consumer_ranks: {}\n", producer_ranks, world.size() - producer_ranks);
    if (shared && world.rank() == 0)
        fmt::print(stderr, "space sharing: producer_ranks = consumer_ranks = world: {}\n", world.size());

    // load tasks
    void* lib_producer = dlopen(producer_exec.c_str(), RTLD_LAZY);
    if (!lib_producer)
        fmt::print(stderr, "Couldn't open {}\n", producer_exec);

    void* lib_consumer = dlopen(consumer_exec.c_str(), RTLD_LAZY);
    if (!lib_consumer)
        fmt::print(stderr, "Couldn't open {}\n", consumer_exec);

    void* producer_f_ = dlsym(lib_producer, "producer_f");
    if (!producer_f_)
        fmt::print(stderr, "Couldn't find producer_f\n");
    void* consumer_f_ = dlsym(lib_consumer, "consumer_f");
    if (!consumer_f_)
        fmt::print(stderr, "Couldn't find consumer_f\n");

    // communicator management
    MPI_Comm intercomm_;
    std::vector<communicator> producer_intercomms, consumer_intercomms;
    communicator p_c_intercomm;
    communicator local;
    communicator producer_comm, consumer_comm;
    MPI_Comm_dup(world, &producer_comm);
    MPI_Comm_dup(world, &consumer_comm);

    if (shared)
    {
        MPI_Comm_dup(world, &producer_comm);
        MPI_Comm_dup(world, &consumer_comm);
        MPI_Comm_dup(world, &p_c_intercomm);
        producer_intercomms.push_back(p_c_intercomm);
        consumer_intercomms.push_back(p_c_intercomm);
    }
    else
    {
        MPI_Comm_split(world, producer ? 0 : 1, 0, &local);

        if (producer)
        {
            MPI_Intercomm_create(local, 0, world, /* remote_leader = */ producer_ranks, /* tag = */ 0, &intercomm_);
            producer_intercomms.push_back(communicator(intercomm_));
            producer_comm = local;
        }
        else
        {
            MPI_Intercomm_create(local, 0, world, /* remote_leader = */ 0, /* tag = */ 0, &intercomm_);
            consumer_intercomms.push_back(communicator(intercomm_));
            consumer_comm = local;
        }
    }

    // shared MetadataVol plugin
    hid_t plist;
    if (shared)
    {
        l5::MetadataVOL& shared_vol_plugin = l5::MetadataVOL::create_MetadataVOL();
        fmt::print(stderr, "prod-con: creating new shared mode MetadataVOL plugin\n");
        plist = H5Pcreate(H5P_FILE_ACCESS);

        if (passthru)
            H5Pset_fapl_mpio(plist, world, MPI_INFO_NULL);

        l5::H5VOLProperty vol_prop(shared_vol_plugin);
        if (!getenv("HDF5_VOL_CONNECTOR"))
            vol_prop.apply(plist);

        // set lowfive properties
        if (passthru)
            shared_vol_plugin.set_passthru("example1.nc", "*");
        if (metadata)
            shared_vol_plugin.set_memory("example1.nc", "*");
        shared_vol_plugin.set_keep(true);
    }

    // declare lambdas for the tasks

    auto producer_f = [&]()
    {
        ((void (*) (communicator&,
                    const std::vector<communicator>&,
                    bool,
                    int,
                    int))
                    (producer_f_))(
                        producer_comm,
                        producer_intercomms,
                        shared,
                        metadata,
                        passthru);
    };

    auto consumer_f = [&]()
    {
        ((void (*) (communicator&,
                    const std::vector<communicator>&,
                    bool,
                    int,
                    int))
                    (consumer_f_))(
                        consumer_comm,
                        consumer_intercomms,
                        shared,
                        metadata,
                        passthru);
    };

    std::vector<double> times(ntrials);     // elapsed time for each trial
    double sum_time = 0.0;                  // sum of all times
    for (auto i = 0; i < ntrials; i++)
    {
        // timing
        world.barrier();
        double t0 = MPI_Wtime();

        if (!shared)
        {
            if (producer)
                producer_f();
            else
                consumer_f();
        } else
        {
            // not multithreading, just serializing
            producer_f();
            consumer_f();
        }

        // timing
        world.barrier();
        times[i] = MPI_Wtime() - t0;
        sum_time += times[i];
        if (world.rank() == 0)
            fmt::print(stderr, "Elapsed time for trial {}\t\t{:.4f} s.\n", i, times[i]);
    }

    if (shared)
        H5Pclose(plist);

    // timing stats
    double mean_time    = sum_time / ntrials;
    double var_time     = 0.0;
    for (auto i = 0; i < ntrials; i++)
        var_time += ((times[i] - mean_time) * (times[i] - mean_time));
    var_time /= ntrials;

    if (world.rank() == 0)
    {
        fmt::print(stderr, "\nMean elapsed time for {} trials\t\t{:.4f} s.\n", ntrials, mean_time);
        fmt::print(stderr, "Variance\t\t\t\t{:.4f}\n", var_time);
        fmt::print(stderr, "Standard deviation\t\t\t{:.4f}\n", sqrt(var_time));
        fmt::print(stderr, "Minimum\t\t\t\t\t{:.4f} s.\n", *(std::min_element(times.begin(), times.end())));
        fmt::print(stderr, "Maximum\t\t\t\t\t{:.4f} s.\n", *(std::max_element(times.begin(), times.end())));
    }
}
