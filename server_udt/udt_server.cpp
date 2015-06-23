#include <arpa/inet.h>
#include <udt.h>
#include <iostream>
#include <cstring>
#include <sys/socket.h>

#include "udt_server.h"

const int g_IP_Version = AF_INET;
//const int g_Socket_Type = SOCK_STREAM;
const int g_Socket_Type = SOCK_DGRAM;


UDTServer::UDTServer(void) :
    listen_sock_(UDT::INVALID_SOCK),
    udtbuf_recved_len_(0),
    udt_running_(0),
    udt_eid_(-1)
{
}

// return -1 means error. otherwise return 0
int UDTServer::CreateListenSocket(int listen_port)
{
    //listen_sock_ = UDT::socket(AF_INET, SOCK_STREAM, 0);
    listen_sock_ = UDT::socket(AF_INET, SOCK_DGRAM, 0);
    if (UDT::INVALID_SOCK == listen_sock_)
    {
        std::cout << "UDT socket: " << UDT::getlasterror().getErrorCode() << ' ' << UDT::getlasterror().getErrorMessage();
        return -1;
    }

    // setopt
    {
        bool block = true;  // 设置成阻塞模式, 然后容易保证写的完整性
        //bool block = false;
        UDT::setsockopt(listen_sock_, 0, UDT_SNDSYN, &block, sizeof(bool));
        UDT::setsockopt(listen_sock_, 0, UDT_RCVSYN, &block, sizeof(bool));

        //UDT::setsockopt(listen_sock_, 0, UDT_MSS, new int(1500), sizeof(int));

        //int snd_buf = 16000;// 8192;
        //int rcv_buf = 16000;//8192;
        //UDT::setsockopt(listen_sock_, 0, UDT_SNDBUF, &snd_buf, sizeof(int)); // use default:10MB
        //UDT::setsockopt(listen_sock_, 0, UDT_RCVBUF, &rcv_buf, sizeof(int)); // use default:10MB


        //int fc = 4096;
        //UDT::setsockopt(listen_sock_, 0, UDT_FC, &fc, sizeof(int));

        bool reuse = true;
        UDT::setsockopt(listen_sock_, 0, UDT_REUSEADDR, &reuse, sizeof(bool));

        bool rendezvous = false;
        UDT::setsockopt(listen_sock_, 0, UDT_RENDEZVOUS, &rendezvous, sizeof(bool));
    }

    // bind
    sockaddr_in my_addr;
    my_addr.sin_family = AF_INET;
    my_addr.sin_port = htons(listen_port);
    my_addr.sin_addr.s_addr = INADDR_ANY;
    std::memset(&(my_addr.sin_zero), '\0', 8);
    if (UDT::ERROR == UDT::bind(listen_sock_, (sockaddr*)&my_addr, sizeof(my_addr)))
    {
        std::cout << "UDT bind: " << UDT::getlasterror().getErrorCode() << ' ' << UDT::getlasterror().getErrorMessage() << std::endl;
        return -1;
    }

    // listen
    //
    if (UDT::ERROR == UDT::listen(listen_sock_, 20))
    {
        std::cout << "UDT listen: " << UDT::getlasterror().getErrorCode() << ' ' << UDT::getlasterror().getErrorMessage() << std::endl;
        return -1;
    }

    // success
	return 0;
}

int UDTServer::SendMsg(const UDTSOCKET& sock, const std::string& msg)
{
    std::cout << "UDT client SendMsg: " << msg << std::endl;

    int send_ret = UDT::sendmsg(sock, msg.c_str(), msg.size());
    if (UDT::ERROR == send_ret)
    {
        std::cout << "UDT sendmsg:" << UDT::getlasterror().getErrorCode() << ' ' << UDT::getlasterror().getErrorMessage() << std::endl;
        return 0;
    }
    if (static_cast<size_t>(send_ret) != msg.size())
    {
        std::cout << "UDT sendmsg: not all msg send!  add lowwatier for send socket is a good thing" << std::endl;
        return 0;
    }
    return 1;
}

int UDTServer::RecvMsg(const UDTSOCKET& sock)
{
    udtbuf_recved_len_ = 0;

    int recv_ret = 0;
    if (UDT::ERROR == (recv_ret = UDT::recvmsg(sock, udtbuf_, sizeof(udtbuf_)))) {

        CUDTException& lasterror = UDT::getlasterror();
        std::cout << "UDT recv: " << lasterror.getErrorCode() << " " <<  lasterror.getErrorMessage() << std::endl;

        int error_code = lasterror.getErrorCode();
        if (error_code == CUDTException::EINVPARAM) {
            udt_running_ = 0;
            UDT::epoll_remove_usock(udt_eid_, sock);
        }
        else if (error_code == CUDTException::ECONNLOST) {
            UDT::epoll_remove_usock(udt_eid_, sock);
        }
        else {
            std::cout << "RecvMsg: an error do not handle!" << std::endl;
        }
        return 0;
    }

    if (recv_ret > 0) 
    {
        //DEBUG_MSG("UDT Thread Enter.\n");
        udtbuf_recved_len_ = recv_ret;
        std::cout << "UDT recv msg: " << std::string(udtbuf_, udtbuf_recved_len_) << std::endl;
        //DEBUG_MSG(" - UDT Thread Exit.\n");
        return 1;
    }

    std::cout << "UDT recv msg with zero len" << std::endl;
    return 1;
}


void UDTServer::Run(int listen_port)
{
	udt_running_ = 0;

    // create, bind and listen
    //
	CreateListenSocket(listen_port);


    // epoll
	udt_eid_ = UDT::epoll_create();

    {
        int add_usock_ret = UDT::epoll_add_usock(udt_eid_, listen_sock_);
        if (add_usock_ret < 0)
            std::cout << "UDT::epoll_add_usock error: " << add_usock_ret << std::endl;
    }
	
	std::cout << "Run UDT server loop ...\n";
	udt_running_ = 1;


    std::set<UDTSOCKET> readfds;


	while (udt_running_) {
		int state = UDT::epoll_wait(udt_eid_, &readfds, NULL, 1, NULL, NULL);
        //std::cout << "UDT::epoll_wait return " << state << std::endl;
		if (state > 0) {
			for (std::set<UDTSOCKET>::iterator i = readfds.begin(); i != readfds.end(); ++i) {
                UDTSOCKET cur_sock = *i;

                if (cur_sock == listen_sock_)
                {
                    std::cout << "cur_sock == listen_sock_" << std::endl;
                    sockaddr addr;
                    int addr_len;
                    UDTSOCKET new_sock = UDT::accept(listen_sock_, &addr, &addr_len);
                    if (new_sock == UDT::INVALID_SOCK)
                    {
                        std::cout << "UDT accept:" << UDT::getlasterror().getErrorCode() << ' ' << UDT::getlasterror().getErrorMessage() << std::endl;
                        continue;
                    }

                    // add to readfds
                    {
                        int add_usock_ret = UDT::epoll_add_usock(udt_eid_, new_sock);
                        if (add_usock_ret < 0)
                            std::cout << "UDT::epoll_add_usock new_sock add error: " << add_usock_ret << std::endl;
                    }

                    continue;
                }

                // client sock
                {
                    RecvMsg(cur_sock);
                    if (udtbuf_recved_len_ > 0)
                    {
                        SendMsg(cur_sock, std::string(udtbuf_, udtbuf_recved_len_));
                    }
                }
			}
		}
        else if (state == 0) {
            std::cout << "UDT epoll_wait return 0" << std::endl;
        }
		else {
            if (UDT::getlasterror().getErrorCode() != CUDTException::ETIMEOUT) {
                std::cout << "UDT epoll_wait: " << UDT::getlasterror().getErrorCode() <<
                    ' ' << UDT::getlasterror().getErrorMessage() << std::endl;
            }
			if ((CUDTException::EINVPARAM == UDT::getlasterror().getErrorCode()) ||
				(CUDTException::ECONNLOST == UDT::getlasterror().getErrorCode())) {
				udt_running_ = 0;
				//UDT::epoll_remove_usock(eid, cur_sock);
			}
		}
	}

	std::cout << "release UDT epoll ..." << std::endl;
	int release_state = UDT::epoll_release(udt_eid_);
    if (release_state != 0)
        std::cout << "UDT epoll_release: " << release_state << std::endl;
	

	std::cout << "Close server ...";
	int close_state = UDT::close(listen_sock_);
    if (close_state != 0)
        std::cout << "UDT close:" << UDT::getlasterror().getErrorCode() << ' ' << UDT::getlasterror().getErrorMessage() << std::endl;

	std::cout << "ok\n";

	pthread_detach(pthread_self());
}