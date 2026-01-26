// func-core/x310-d7/options.hpp
#pragma once


#include "../third-party/CLI/CLI11.hpp"
#include "common/common.hpp"

#include <chrono>
#include <ctime>
#include <unistd.h>
#include <limits.h>


#include <string>
#include <cstdint>
#include <ostream>
#include <filesystem>



namespace GRAIA {

// proga <- argv[0]
void _usage(const char* proga, std::ostream& out = std::cerr )
{
    out << "Usage:"
        << "\n  " << proga << "-f 428e6 --output-dir ./captured --file-size 0.75G --src enp3s0fp0:192.168.30.3:50000 --limit 10G"
        << "\n  file-size 0.75G=1024*1024*1024*3*/4   512M=512*1024*1024  47K=47*1024"
        << "\n  -f 428e6 freq in Hz cast to int"
    << std::endl;
}

struct Options {
    // CLI parameters. Creates ones on strurt up.

    // X310 mch stream vaules
    std::string dev;
    std::string ip;
    int port;

    double freq;
    
    size_t fsz;
    size_t fsz_limit;

    STD_PATH output_dir;
    tick_t t_start;
    // Parse command line arguments. Throw ecxeption on BAD params.
    void parse_cli(int argc, char** argv)
    {
        // if ( std::string(argv[1]) == "-h" || std::string(argv[1])=="--help")
        // {
        //     _usage(argv[0], std::cout);
        //     return;            
        // }
        try{

        }
        catch (const std::exception& e) {
            std::cerr << "fail cli parse: (" << e.what() << ")\n";
            _usage(argv[0], std::cerr);
            throw e;
        }
    }
    
    // Calculate/load derived parameters
    void calc_derivate()
    {

    };

};

} // namespace GRAIA


