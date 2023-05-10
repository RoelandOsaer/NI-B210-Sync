#include <zmq.hpp>
#include <uhd/usrp/multi_usrp.hpp>
#include <uhd/utils/safe_main.hpp>
#include <uhd/utils/thread.hpp>
#include <boost/format.hpp>
#include <boost/program_options.hpp>
#include <iostream>
#include <fstream>
#include <string>
#include <chrono>
#include <thread>
#include <cmath>
#include <filesystem>

#define FMT_HEADER_ONLY
#include <fmt/format.h>
#include <fmt/ranges.h>

namespace po = boost::program_options;

#define RATE    1e6
#define FREQ    400e6

zmq::context_t context(1);

using sample_dt = int16_t;

using sample_t = std::complex<sample_dt>;

std::vector<sample_t> read_ZC_seq(int min_samples)
{
        // adapted from https://wiki.gnuradio.org/index.php/File_Sink
        std::string filename = "../zc-sequence-sc16.dat";
        // check whether file exists
        if (!std::filesystem::exists(filename.data()))
        {
                fmt::print(stderr, "file '{:s}' not found\n", filename);
        }

        // calculate how many samples to read
        auto file_size = std::filesystem::file_size(std::filesystem::path(filename));
        auto samples_in_file = file_size / sizeof(sample_t);
        
        int num_repetitions = std::ceil(static_cast<sample_dt>(min_samples) / samples_in_file);

        std::vector<sample_t> samples;
        samples.resize(samples_in_file * num_repetitions);

        fmt::print(stderr, "Reading {:d} times...\n", num_repetitions);

        std::ifstream input_file(filename.data(), std::ios_base::binary);
        if (!input_file)
        {
                fmt::print(stderr, "error opening '{:s}'\n", filename);
        }

        fmt::print(stderr, "Reading {:d} samples...\n", samples_in_file);

        sample_t *buff_ptr = &samples.front();
        // read-out the same file "num_repetitions" times
        for (int i = 0; i < num_repetitions; i++)
        {
                input_file.read(reinterpret_cast<char *>(buff_ptr), samples_in_file * sizeof(sample_t));
                buff_ptr += samples_in_file;
        }
        std::cout << "samples_length: " << samples.size() << std::endl;

        return samples;

        /*
        LEGACY
        std::vector<std::complex<float>> seq(353*10);
        std::complex<float> seq_length(353,0);
        std::complex<float> u(7,0);
        std::complex<float> cf(53%2,0);
        std::complex<float> q(2*0,0);
        std::complex<float> min_j(0,-1);

        std::complex<float> pi = M_PI;


        std::complex<float> n = 0;
        for(uint16_t i=0;i<353*10;i++){
                n = i%353;
                std::complex<float> s = exp(min_j*pi*u*n*(n+cf+q) / seq_length); // i defined q = 2*q, so i dont need to cast 2 to a complex number
                seq.push_back(s);
        }
        */
}


    void ready_to_go(std::string id, std::string server_ip)
{
        // REQ RES pattern
        // let server now that you are ready and waiting for a SYNC command
        //  Socket to receive messages on
        //  Prepare our context and socket
        zmq::socket_t socket(context, zmq::socket_type::req);

        std::cout << "Connecting to server..." << std::endl;
        socket.connect("tcp://"+server_ip+":5555");

        zmq::message_t request(id.size());
        memcpy(request.data(), id.data(), id.size());
        std::cout << "Sending ID "
                  << id
                  << "..." << std::endl;
        socket.send(request, zmq::send_flags::none);

        //  Get the reply.
        zmq::message_t reply;
        auto res = socket.recv(reply, zmq::recv_flags::none);
        std::cout << "Received" << std::endl;
}

void wait_till_go_from_server(std::string server_ip)
{
        // TODO check if received message is SYNC
        //   Socket to receive messages on
        zmq::socket_t subscriber(context, ZMQ_SUB);
        subscriber.connect("tcp://"+server_ip+":5557");
        zmq_setsockopt(subscriber, ZMQ_SUBSCRIBE, "", 0);

        zmq::message_t msg;
        auto res = subscriber.recv(&msg);
        std::string msg_str = std::string(static_cast<char *>(msg.data()), msg.size());
        std::cout << "Received '" << msg_str << "'" << std::endl;
}

int UHD_SAFE_MAIN(int argc, char *argv[])
{

        std::string str_args;
        std::string port;
        std::string server_ip;
        bool ignore_sync = false;
        double rate;

        po::options_description desc("Allowed options");
        desc.add_options()("help", "produce help message")
        ("args", po::value<std::string>(&str_args)->default_value("type=b200,mode_n=integer"), "give device arguments here")
        ("iq_port", po::value<std::string>(&port)->default_value("8888"), "Port to stream IQ samples to")
        ("server-ip", po::value<std::string>(&server_ip), "SYNC server IP address")
        ("rate", po::value<double>(&rate)->default_value(1e6), "rate of incoming samples")
        ("ignore-server", po::bool_switch(&ignore_sync), "Discard waiting till SYNC server");

        po::variables_map vm;
        po::store(po::parse_command_line(argc, argv, desc), vm);
        po::notify(vm);

        if (vm.count("help"))
        {
                std::cout << desc << "\n";
                return 0;
        }

        uhd::device_addr_t args(str_args);
        uhd::usrp::multi_usrp::sptr usrp = uhd::usrp::multi_usrp::make(args);
        // std::cout << "Hello World"
        //           << std::endl;

        // // set up some catch-all masks
        uint32_t gpio_line = 0xF; // only the bottom 4 lines: 0xF = 00001111 = Pin 0, 1, 2, 3
        uint32_t all_one = 0xFF;
        uint32_t all_zero = 0x00;

        // // set gpio pins up for output
        std::cout << "Setting up GPIO" << std::endl;
        usrp->set_gpio_attr("FP0", "DDR", all_one, gpio_line, 0);
        usrp->set_gpio_attr("FP0", "CTRL", all_zero, gpio_line, 0);
        usrp->set_gpio_attr("FP0", "OUT", all_zero, gpio_line, 0); // reset LOW (async)

        // initialise
        std::cout << "Setting up PPS + 10MHz" << std::endl;
        usrp->set_clock_source("external");
        usrp->set_time_source("external");

       	std::map<std::string, std::string> m = usrp->get_usrp_tx_info();
       	std::string serial = m["mboard_serial"];
       	std::cout << "Serial number: " << serial << std::endl;

	uhd::stream_args_t stream_args("cs16"); // complex shorts (uint16_t)
        stream_args.channels = {0,1};
        uhd::tx_streamer::sptr tx_stream = usrp->get_tx_stream(stream_args);
	
       // size_t nsamps_per_buff = tx_stream->get_max_num_samps();
       // std::cout << "nsamps_per_buff: " << nsamps_per_buff<<std::endl;         //zelfde voor verschillende freq
       // std::vector<sample_t> seq = read_ZC_seq(nsamps_per_buff);

        if (!ignore_sync)
        {
                ready_to_go(serial,server_ip);        // non-blocking
                wait_till_go_from_server(server_ip); // blocking till SYNC message received
        }
        else
        {
                std::cout << "Ignoring waiting for server" << std::endl;
        }

        // This command will be processed fairly soon after the last PPS edge:
        usrp->set_time_next_pps(uhd::time_spec_t(0.0));
        std::cout << "[SYNC] Resetting time." << std::endl;
        std::this_thread::sleep_for(std::chrono::milliseconds(2000));
    
        usrp->set_command_time(uhd::time_spec_t(4.0));
 	usrp->set_gpio_attr("FP0", "OUT", all_one, gpio_line, 0);
	usrp->clear_command_time();

        // finished
        std::cout << std::endl << "Done!" << std::endl << std::endl;


        return EXIT_SUCCESS;
}
