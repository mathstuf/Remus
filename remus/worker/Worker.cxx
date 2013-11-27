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


#include <remus/worker/Worker.h>

#include <boost/thread.hpp>
#include <string>

#include <remus/common/Message.h>
#include <remus/common/Response.h>
#include <remus/common/zmqHelper.h>

namespace remus{
namespace worker{

//-----------------------------------------------------------------------------
class Worker::ServerCommunicator
{
public:
  ServerCommunicator(const std::string& serverInfo,
                     const std::string& mainThreadInfo):
    ContinueTalking(true),
    ServerEndpoint(serverInfo),
    MainEndpoint(mainThreadInfo)
    {
    }

  ~ServerCommunicator()
  {
  }

  void run(zmq::context_t *context)
  {
    zmq::socket_t Worker(*context,ZMQ_PAIR);
    zmq::socket_t Server(*context,ZMQ_DEALER);

    zmq::connectToAddress(Server,this->ServerEndpoint);
    zmq::connectToAddress(Worker,this->MainEndpoint);

    zmq::pollitem_t items[2]  = { { Worker,  0, ZMQ_POLLIN, 0 },
                                  { Server,  0, ZMQ_POLLIN, 0 } };
    while( this->ContinueTalking)
      {
      zmq::poll(&items[0],2,remus::HEARTBEAT_INTERVAL);
      bool sentToServer=false;
      if(items[0].revents & ZMQ_POLLIN)
        {
        sentToServer = true;
        remus::common::Message message(Worker);

        //just pass the message on to the server
        message.send(Server);

        //special case is that TERMINATE_JOB_AND_WORKER means we stop looping
        if(message.serviceType()==remus::TERMINATE_JOB_AND_WORKER)
          {
          this->ContinueTalking = false;
          }
        }
      if(items[1].revents & ZMQ_POLLIN && this->ContinueTalking)
        {
        //we make sure ContinueTalking is valid so we know the worker
        //is still listening and not in destructor

        //we can have two  response types, one which is for the Service MakeMesh
        //and the other for the Service TERMINATE_JOB_AND_WORKER which kills
        //the worker process
        remus::common::Response response(Server);
        if(response.serviceType() != remus::TERMINATE_JOB_AND_WORKER)
          {
          response.send(Worker);
          }
        else
          {
          //we should split up the TERMINATE_JOB_AND_WORKER into really two calls
          //the first would be stop_job which would only stop the current job
          //it might terminate the worker if needed, but no mandatory. The
          //second would be to terminate a worker and all jobs it contains

          //todo should we allow the worker a better way to cleanup?
          //currently the worker will need to inherit from
          //remus::common::SignalCatcher to find out it is closing
          exit(1);
          }
        }
      if(!sentToServer)
        {
        //std::cout << "send heartbeat to server" << std::endl;
        //send a heartbeat to the server
        remus::common::Message message(remus::common::MeshIOType(),remus::HEARTBEAT);
        message.send(Server);
        }
      }
  }

private:
  bool ContinueTalking;
  std::string ServerEndpoint;
  std::string MainEndpoint;
};

//-----------------------------------------------------------------------------
Worker::Worker(remus::common::MeshIOType mtype,
               remus::worker::ServerConnection const& conn):
  MeshIOType(mtype),
  Context(1),
  ServerComm(Context,ZMQ_PAIR),
  BComm(NULL),
  ServerCommThread(NULL)
{
  //FIRST THREAD HAS TO BIND THE INPROC SOCKET
  zmq::socketInfo<zmq::proto::inproc> internalCommInfo =
      zmq::bindToAddress<zmq::proto::inproc>(this->ServerComm,"worker");

  this->startCommunicationThread(conn.endpoint(),internalCommInfo.endpoint());
}


//-----------------------------------------------------------------------------
Worker::~Worker()
{
  //std::cout << "Ending the worker" << std::endl;
  this->stopCommunicationThread();
}

//-----------------------------------------------------------------------------
bool Worker::startCommunicationThread(const std::string &serverEndpoint,
                                      const std::string &commEndpoint)
{
  if(!this->ServerCommThread && !this->BComm)
    {
    //start about the server communication thread
    this->BComm = new Worker::ServerCommunicator(serverEndpoint,commEndpoint);
    this->ServerCommThread = new boost::thread(&Worker::ServerCommunicator::run,
                                             this->BComm,&this->Context);

    //register with the server that we can mesh a certain type
    remus::common::Message canMesh(this->MeshIOType,remus::CAN_MESH);
    canMesh.send(this->ServerComm);
    return true;
    }
  return false;
}

//-----------------------------------------------------------------------------
bool Worker::stopCommunicationThread()
{
  if(this->ServerCommThread && this->BComm)
    {
    //send message that we are shutting down communication, and we can stop
    //polling the server
    remus::common::Message shutdown(this->MeshIOType,
                                    remus::TERMINATE_JOB_AND_WORKER);
    shutdown.send(this->ServerComm);

    this->ServerCommThread->join();
    delete this->BComm;
    delete this->ServerCommThread;

    this->BComm = NULL;
    this->ServerCommThread = NULL;
    return true;
    }
  return false;
}

//-----------------------------------------------------------------------------
remus::worker::Job Worker::getJob()
{
  remus::common::Message askForMesh(this->MeshIOType,remus::MAKE_MESH);
  askForMesh.send(this->ServerComm);

  //we have our message back
  remus::common::Response response(this->ServerComm);
  //std::cout << "have our job from the server com in the real worker" <<std::endl;

  //we need a better serialization technique
  std::string msg = response.dataAs<std::string>();
  return remus::worker::to_Job(msg);
}

//-----------------------------------------------------------------------------
void Worker::updateStatus(const remus::worker::JobStatus& info)
{
  //send a message that contains, the status
  std::string msg = remus::worker::to_string(info);
  remus::common::Message message(this->MeshIOType,
                                    remus::MESH_STATUS,
                                    msg.data(),msg.size());
  message.send(this->ServerComm);
}

//-----------------------------------------------------------------------------
void Worker::returnMeshResults(const remus::worker::JobResult& result)
{
  //send a message that contains, the path to the resulting file
  std::string msg = remus::worker::to_string(result);
  remus::common::Message message(this->MeshIOType,
                                    remus::RETRIEVE_MESH,
                                    msg.data(),msg.size());
  message.send(this->ServerComm);
}


}
}
