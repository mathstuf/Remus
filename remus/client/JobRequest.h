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

#ifndef remus_client_JobRequest_h
#define remus_client_JobRequest_h

#include <algorithm>
#include <string>
#include <sstream>

#include <remus/common/MeshIOType.h>

//A job request has two purposes. First it is sent to the server to determine
//if the mesh type and data model that the job request has is supported.
//Secondly it is used to transport the actual job to the server

//Note: Currently no server supports the ability to restrict a mesh request
//to a single named mesher. This option is expected to be supported in the future.
//For now we replicate this feature by making <MeshIOType,inputMeshDataType> be
//unique for each mesher
namespace remus{
namespace client{
class JobRequest
{
public:

  //Construct a job request with only a mesher type given. This is used to
  //when asking a server if it supports a type of mesh
  JobRequest(const remus::meshtypes::MeshTypeBase& inputFileType,
             const remus::meshtypes::MeshTypeBase& outputMeshIOType):
    CombinedType(inputFileType,outputMeshIOType),
    JobInfo()
    {
    }

  //Construct a job request with a mesh type and the info required by the worker
  //to run the job. This is used when submitting a job from the client to the server.
  JobRequest(const remus::meshtypes::MeshTypeBase& inputFileType,
             const remus::meshtypes::MeshTypeBase& outputMeshIOType,
             const std::string& info):
    CombinedType(inputFileType,outputMeshIOType),
    JobInfo(info)
    {
    }

  //Construct a job request with the given types held inside the remus::common::MeshIOType object
  explicit JobRequest(remus::common::MeshIOType combinedType):
    CombinedType(combinedType),
    JobInfo()
    {
    }

  //Construct a job request with the given types held inside the remus::common::MeshIOType object
  JobRequest(remus::common::MeshIOType combinedType, const std::string& info):
    CombinedType(combinedType),
    JobInfo(info)
    {

    }

  //constructs a variable that represents the combination of the input
  //and output type as a single integer
  remus::common::MeshIOType type() const { return this->CombinedType; }

  boost::uint32_t outputType() const { return CombinedType.outputType(); }
  boost::uint32_t inputType() const { return CombinedType.inputType(); }
  const std::string& jobInfo() const { return JobInfo; }

private:
  remus::common::MeshIOType CombinedType;
  std::string JobInfo;
};

//------------------------------------------------------------------------------
inline std::string to_string(const remus::client::JobRequest& request)
{
  //convert a request to a string, used as a hack to serialize
  //encoding is simple, contents newline separated
  std::stringstream buffer;
  buffer << request.type() << std::endl;
  buffer << request.jobInfo().length() << std::endl;
  remus::internal::writeString(buffer, request.jobInfo());
  return buffer.str();
}


//------------------------------------------------------------------------------
inline remus::client::JobRequest to_JobRequest(const std::string& msg)
{
  //convert a job detail from a string, used as a hack to serialize
  std::stringstream buffer(msg);

  int dataLen;
  std::string data;
  remus::common::MeshIOType jobRequirements;

  buffer >> jobRequirements;

  buffer >> dataLen;
  data = remus::internal::extractString(buffer,dataLen);
  return remus::client::JobRequest(jobRequirements,data);
}


//------------------------------------------------------------------------------
inline remus::client::JobRequest to_JobRequest(const char* data, int size)
{
  //convert a job request from a string, used as a hack to serialize
  std::string temp(size,char());
  std::copy( data, data+size, temp.begin() );
  return to_JobRequest( temp );
}


}
}

#endif