#include "networkclient.h"

#include <iostream>
#include <boost/array.hpp>


NetworkClient::NetworkClient(const std::string& address, const std::string& port):
    m_query(address, port.c_str()),
    m_resolver(m_ioService)
{
}

NetworkClient::~NetworkClient()
{
}

bool NetworkClient::connect()
{
    if(!m_connected)
    {
        try{
            boost::asio::ip::tcp::resolver::iterator    end;
            boost::asio::ip::tcp::resolver::iterator    connectpoint    = m_resolver.resolve(m_query);
            boost::system::error_code                   error           = boost::asio::error::host_not_found;

            while (error && connectpoint != end)
            {
                m_socket.reset(new boost::asio::ip::tcp::socket(m_ioService));

                if(m_socket){
                    m_socket->connect(*connectpoint++, error);
                }
            }

            if (error)
            {
                //adddlog
                return false;
            }
        }
        catch(std::exception& e)
        {
            return false;
        }

        m_connected = true;
    }

    return true;
}

bool NetworkClient::disConnect()
{
    if(m_connected)
    {
        try{
            if(m_socket){
                m_socket->close();
            }
            m_socket.reset();
        }
        catch(std::exception& e)
        {
            return false;
        }

        m_connected = false;
    }

    return true;
}

bool NetworkClient::reconnect()
{
    disConnect();
    return connect();
}

bool NetworkClient::sendData(std::vector<uint8_t> buff)
{
    if(m_connected && m_socket)
    {
        auto size = buff.size();
        if( boost::asio::write(*m_socket.get(), boost::asio::buffer(std::move(buff))) == size ){
            return true;
        }
    }

    return false;
}

bool NetworkClient::sendData(uint8_t* buff, int size)
{
    if(m_connected && m_socket)
    {
        if( boost::asio::write(*m_socket.get(), boost::asio::buffer(buff, size)) == (std::size_t)size ){
            return true;
        }
    }

    return false;
}

bool NetworkClient::readData(std::vector<uint8_t>& buff)
{
    if(m_connected && m_socket)
    {
        auto size = buff.size();
        if( boost::asio::read(*m_socket.get(), boost::asio::buffer(buff)) == size ){
            return true;
        }
    }

    return false;
}

bool NetworkClient::readData(uint8_t* buff, int size)
{
    if(m_connected && m_socket)
    {
        if( boost::asio::read(*m_socket.get(), boost::asio::buffer(buff, size)) == (size_t)size ){
            return true;
        }
    }

    return false;
}

bool NetworkClient::readData(char* buff, int size)
{
    if(m_connected && m_socket)
    {
        if( boost::asio::read(*m_socket.get(), boost::asio::buffer(buff, size)) == (size_t)size ){
            return true;
        }
    }

    return false;
}
