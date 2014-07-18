//=============================================================================
//
//  Copyright (c) Kitware, Inc.
//  All rights reserved.
//  See LICENSE.txt for details.
//
//  This software is distributed WITHOUT ANY WARRANTY; without even
//  the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
//  PURPOSE.  See the above copyright notice for more information.
//
//=============================================================================

#ifndef remus_proto_zmqHelper_h
#define remus_proto_zmqHelper_h

#include <algorithm>
#include <cstddef>
#include <sstream>

#ifndef _MSC_VER
  #pragma GCC diagnostic push
  #pragma GCC diagnostic ignored "-Wunused-parameter"
#endif
#include <boost/lexical_cast.hpp>
#ifndef _MSC_VER
  #pragma GCC diagnostic pop
#endif

//We now provide our own zmq.hpp since it has been removed from zmq 3, and
//made its own project
#include <remus/proto/zmq.hpp>
#include <remus/proto/zmqSocketIdentity.h>
#include <remus/proto/zmqSocketInfo.h>

//inject some basic zero MQ helper functions into the namespace
namespace zmq
{
inline bool address_send(zmq::socket_t & socket, const zmq::SocketIdentity& address)
{
  zmq::message_t message(address.size());


  //disable warning about using std::copy with pointers
  //doing this inline to the method as an attempt to keep the rest of
  //the headers complexity reduced
# ifdef _MSC_VER
#   pragma warning(push)
#   pragma warning(disable: 4996)
# endif
  std::copy(address.data(),
            address.data()+address.size(),
            static_cast<unsigned char*>(message.data()));
//reset our warnings to the original level
# ifdef _MSC_VER
#   pragma warning(pop)
# endif

  return socket.send(message);
}

inline zmq::SocketIdentity address_recv(zmq::socket_t& socket)
{
  zmq::message_t message;
  socket.recv(&message);
  return zmq::SocketIdentity((char*)message.data(),message.size());
}

//specify a default linger so that if what we are connecting to
//doesn't exist and we are told to shutdown we don't hang for ever
inline void set_socket_linger(zmq::socket_t &socket)
{
  const int linger_duration = 250;
  socket.setsockopt(ZMQ_LINGER, &linger_duration, sizeof(int) );

  //ToDo we need to determine which sockets we want to set timeouts
  //on for sending and receiving messages.
  //I am worried about how this is going to interact with the variable
  //query / poll rates that happen on OSX 10.9+ when an app starts to be
  //slowed down
// #if ZMQ_VERSION_MAJOR >= 3
//   socket.setsockopt(ZMQ_RCVTIMEO, &linger_duration, sizeof(int) );
//   socket.setsockopt(ZMQ_SNDTIMEO, &linger_duration, sizeof(int) );
// #endif
}

//bind to initialInfo socket, and return that socket info
template<typename T>
inline zmq::socketInfo<T> bindToAddress(zmq::socket_t &socket,
                                        const zmq::socketInfo<T>& initialInfo)
{
  set_socket_linger(socket);
  socket.bind(initialInfo.endpoint().c_str());
  return initialInfo;
}

//return the true port we have bound to, since the initial one might be already in use
template< >
inline zmq::socketInfo<zmq::proto::tcp> bindToAddress(zmq::socket_t &socket,
                        const zmq::socketInfo<zmq::proto::tcp>& initialInfo)
{
  set_socket_linger(socket);

  //go through all ports, I hope the input port is inside the Ephemeral range
  int rc = -1;
  zmq::socketInfo<zmq::proto::tcp> socketInfo(initialInfo);
  for(int i=socketInfo.port();i < 65535 && rc != 0; ++i)
    {
    socketInfo.setPort(i);
    //using the C syntax to skip having to catch the exception;
    rc = zmq_bind(socket.operator void *(),socketInfo.endpoint().c_str());
    }

  if(rc!=0)
    {
    throw zmq::error_t();
    }

    return socketInfo;
}


inline void connectToAddress(zmq::socket_t &socket,const std::string &endpoint)
{
  set_socket_linger(socket);
  socket.connect(endpoint.c_str());
}

template<typename T>
inline void connectToAddress(zmq::socket_t &socket,const zmq::socketInfo<T> &sInfo)
{
  set_socket_linger(socket);
  socket.connect(sInfo.endpoint().c_str());
}

//A wrapper around zeroMQ send. When we call the standard send call
//from a Qt class we experience high number of system level interrupts which
//cause zero to throw an exception when we are sending a blocking message.
//When sending a blocking message we will try a couple of times before
//giving up
//In the future we need to change the client server
//communication in Remus to be async instead of req/reply based.
inline bool send_harder(zmq::socket_t& socket, zmq::message_t& message, int flags=0)
{
  bool sent = false;
  short tries = 0;
  while(!sent && tries < 5)
    {
    try
      {
      sent = socket.send(message,flags);
      if (!sent) { ++tries; }
      }
    catch(error_t){ ++tries; }
    }
  return sent;
}

//A wrapper around zeroMQ recv. When we call the standard recv call
//from a Qt class we experience high number of system level interrupts which
//cause zero to throw an exception when we are recv a blocking message.
//When recving a blocking message we will try a couple of times before
//giving up
//In the future we need to change the client server
//communication in Remus to be async instead of req/reply based.
inline bool recv_harder(zmq::socket_t& socket, zmq::message_t* message, int flags=0)
{
  bool recieved = false;
  short tries = 0;
  while(!recieved && tries < 5)
    {
    try
      {
      recieved = socket.recv(message,flags);
      if (!recieved) { ++tries; }
      }
    catch(error_t){ ++tries; }
    }
  return recieved;
}

//we presume that every message needs to be stripped
//as we make everything act like a req/rep and pad
//a null message on everything
inline bool removeReqHeader(zmq::socket_t& socket,
                            int flags=0)
{
  bool removedHeader = false;
  int socketType;
  std::size_t socketTypeSize = sizeof(socketType);
  socket.getsockopt(ZMQ_TYPE,&socketType,&socketTypeSize);
  if(socketType != ZMQ_REQ && socketType != ZMQ_REP)
    {
    zmq::message_t reqHeader;
    try
      {
      removedHeader = socket.recv(&reqHeader,flags);
      }
    catch(error_t) { return false; }
    }
  return removedHeader;
}

//if we are not a req or rep socket make us look like one
 inline bool attachReqHeader(zmq::socket_t& socket,
                             int flags=0)
{
  bool attachedHeader = false;
  int socketType;
  std::size_t socketTypeSize = sizeof(socketType);
  socket.getsockopt(ZMQ_TYPE,&socketType,&socketTypeSize);
  if(socketType != ZMQ_REQ && socketType != ZMQ_REP)
    {
    zmq::message_t reqHeader(0);
    try
      {
      attachedHeader = socket.send(reqHeader, flags|ZMQ_SNDMORE);
      }
    catch(error_t) { return false; }
    }
  return attachedHeader;
}

}

#endif // remus_proto_zmqHelper_h
