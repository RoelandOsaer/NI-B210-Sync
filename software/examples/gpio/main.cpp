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

#define FMT_HEADER_ONLY
#include <fmt/format.h>
#include <fmt/ranges.h>

namespace po = boost::program_options;

zmq::context_t context(1); 

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
        ("args", po::value<std::string>(&str_args)->default_value("type=b210"), "give device arguments here")
        ("iq_port", po::value<std::string>(&port)->default_value("8888"), "Port to stream IQ samples to")
        ("server-ip", po::value<std::string>(&server_ip), "SYNC server IP address")
        ("rate", po::value<double>(&rate)->default_value(10e6), "rate of incoming samples")
        ("ignore-server", po::bool_switch(&ignore_sync), "Discard waiting till SYNC server");
	

	std::cout << "str_args: " << str_args << std::endl;
	std::cout << "iq_port: " << server_ip << std::endl;
	std::cout << "rate: " << rate << std::endl;

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

        std::map<std::string, std::string> m = usrp->get_usrp_rx_info();
        std::string serial = m["mboard_serial"];
        std::cout << "Serial number: " << serial << std::endl;

	uhd::stream_args_t stream_args("sc16"); // complex shorts (uint16_t)
        stream_args.channels = {0,1};
        uhd::rx_streamer::sptr rx_stream = usrp->get_rx_stream(stream_args);
	
	std::string file = "../usrp_samples_" + serial + "_0.dat";
        std::ofstream outfile_0;
        outfile_0.open(file.c_str(), std::ofstream::binary | std::ios::trunc);

        file = "../usrp_samples_" + serial + "_1.dat";
        std::ofstream outfile_1;
        outfile_1.open(file.c_str(), std::ofstream::binary | std::ios::trunc);
	
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
    
        usrp->set_command_time(uhd::time_spec_t(3.0));
 	usrp->set_gpio_attr("FP0", "OUT", all_one, gpio_line, 0);
	usrp->clear_command_time();
	std::this_thread::sleep_for(std::chrono::milliseconds(7500));
        
        // finished
        std::cout << std::endl << "Done!" << std::endl << std::endl;
        return EXIT_SUCCESS;
}
