#include <diy/mpi/communicator.hpp>
#include "prod-con-mpi.hpp"

using communicator = diy::mpi::communicator;

extern "C" {
void producer_f (communicator& local, const std::vector<communicator>& intercomms,
                 std::mutex& exclusive, bool shared, std::string prefix,
                 int threads, int mem_blocks,
                 Bounds domain,
                 int prod_nblocks, int con1_nblocks, int con2_nblocks, int dim, size_t local_num_points);
}

void producer_f (communicator& local, const std::vector<communicator>& intercomms,
                 std::mutex& exclusive, bool shared, std::string prefix,
                 int threads, int mem_blocks,
                 Bounds domain,
                 int prod_nblocks, int con1_nblocks, int con2_nblocks, int dim, size_t local_num_points)
{
    // diy setup for the producer
    diy::FileStorage                prod_storage(prefix);
    diy::Master                     prod_master(local,
            threads,
            mem_blocks,
            &Block::create,
            &Block::destroy,
            &prod_storage,
            &Block::save,
            &Block::load);
    size_t global_num_points = local_num_points * prod_nblocks;
    AddBlock                        prod_create(prod_master, local_num_points, global_num_points, prod_nblocks);
    diy::ContiguousAssigner         prod_assigner(local.size(), prod_nblocks);
    diy::RegularDecomposer<Bounds>  prod_decomposer(dim, domain, prod_nblocks);
    prod_decomposer.decompose(local.rank(), prod_assigner, prod_create);
    diy::RegularDecomposer<Bounds>  con1_decomposer(dim, domain, con1_nblocks);

    // debug
    if (local.rank() == 0)
        fmt::print(stderr, "dim {} domain [{} : {}] prod_nblocks {}\n",
                dim, domain.min, domain.max, prod_nblocks);

    // send the grid data
    prod_master.foreach([&](Block* b, const diy::Master::ProxyWithLink& cp)
            { b->send_block_grid(cp, local, intercomms.front(), con1_decomposer); });

    // send the particle data
    if (con2_nblocks)
    {
        diy::RegularDecomposer<Bounds>  con2_decomposer(dim, domain, con2_nblocks);
        prod_master.foreach([&](Block* b, const diy::Master::ProxyWithLink& cp)
                { b->send_block_points(cp, global_num_points, prod_nblocks, local, intercomms.back(), con2_decomposer); });
    }
    else
        prod_master.foreach([&](Block* b, const diy::Master::ProxyWithLink& cp)
                { b->send_block_points(cp, global_num_points, prod_nblocks, local, intercomms.back(), con1_decomposer); });

    // signal the consumer that data are ready
    if (!shared)
    {
        for (auto& intercomm: intercomms)
            intercomm.barrier();
    }
    else
    {
        local.barrier();
        int a = 0;                          // it doesn't matter what we send, for synchronization only
        for (auto& intercomm : intercomms)
            intercomm.send(local.rank(), 0, a);
    }

    // wait for all nonblocking sends to complete
    prod_master.foreach([&](Block* b, const diy::Master::ProxyWithLink& cp)
            { b->wait_requests(cp); });
}

