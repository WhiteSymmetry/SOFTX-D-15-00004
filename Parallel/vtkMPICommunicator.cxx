/*=========================================================================

  Program:   Visualization Toolkit
  Module:    vtkMPICommunicator.cxx
  Language:  C++
  Date:      $Date$
  Version:   $Revision$

  Copyright (c) 1993-2002 Ken Martin, Will Schroeder, Bill Lorensen 
  All rights reserved.
  See Copyright.txt or http://www.kitware.com/Copyright.htm for details.

     This software is distributed WITHOUT ANY WARRANTY; without even 
     the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR 
     PURPOSE.  See the above copyright notice for more information.

=========================================================================*/

#include "vtkMPICommunicator.h"

#include "vtkMPIController.h"
#include "vtkMPIGroup.h"
#include "vtkMPIGroup.h"
#include "vtkObjectFactory.h"
#include "vtkToolkits.h"

vtkCxxRevisionMacro(vtkMPICommunicator, "1.17");
vtkStandardNewMacro(vtkMPICommunicator);

vtkCxxSetObjectMacro(vtkMPICommunicator,Group,vtkMPIGroup);

vtkMPICommunicator* vtkMPICommunicator::WorldCommunicator = 0;

// Return the world communicator (i.e. MPI_COMM_WORLD).
// Create one if necessary (singleton).
vtkMPICommunicator* vtkMPICommunicator::GetWorldCommunicator()
{
  int err, size;

  if (vtkMPICommunicator::WorldCommunicator == 0)
    {
    vtkMPICommunicator* comm = vtkMPICommunicator::New();
    vtkMPIGroup* group = vtkMPIGroup::New();
    comm->Handle = new MPI_Comm;
    *(comm->Handle) = MPI_COMM_WORLD;
    comm->SetGroup(group);
    group->Delete();
    group = NULL;
    if ( (err = MPI_Comm_size(MPI_COMM_WORLD, &size)) != MPI_SUCCESS  )
      {
      char *msg = vtkMPIController::ErrorString(err);
      vtkGenericWarningMacro("MPI error occured: " << msg);
      delete[] msg;
      delete comm->Handle;
      comm->Handle = 0;
      comm->Delete();
      return 0;
      }
    comm->Group->Initialize(size);
    for(int i=0; i<size; i++)
      {
      comm->Group->AddProcessId(i);
      }
    comm->Initialized = 1;
    comm->KeepHandleOn();
    vtkMPICommunicator::WorldCommunicator = comm;
    }
  return vtkMPICommunicator::WorldCommunicator;
}

void vtkMPICommunicator::PrintSelf(ostream& os, vtkIndent indent)
{
  this->Superclass::PrintSelf(os,indent);
  os << indent << "Group: ";
  if (this->Group)
    {
    os << endl;
    this->Group->PrintSelf(os, indent.GetNextIndent());
    }
  else
    {
    os << "(nil)";
    os << endl;
    }
  os << indent << "MPI Communicator handler: " ;
  if (this->Handle)
    {
    os << *(this->Handle);
    }
  else
    {
    os << "(nil)";
    }
  os << endl;
  os << indent << "Initialized: " << (this->Initialized ? "(yes)" : "(no)")
     << endl;
  os << indent << "Keep handle: " << (this->KeepHandle ? "(yes)" : "(no)")
     << endl;
  if ( this != vtkMPICommunicator::WorldCommunicator )
    {
    os << indent << "World communicator: ";
    if (vtkMPICommunicator::WorldCommunicator)
      {
      os << endl;
      vtkMPICommunicator::WorldCommunicator->PrintSelf(os, indent.GetNextIndent());
      }
    else
      {
      os << "(nil)";
      }
    os << endl;
    }
  return;
}

vtkMPICommunicator::vtkMPICommunicator()
{
  this->Handle = 0;
  this->Group = 0;
  this->Initialized = 0;
  this->KeepHandle = 0;
}

vtkMPICommunicator::~vtkMPICommunicator()
{
  // Free the handle if required and asked for.
  if (this->Handle && !this->KeepHandle )
    {
    if (*(this->Handle) != MPI_COMM_NULL)
      {
      MPI_Comm_free(this->Handle);
      }
    }
  delete this->Handle;
  this->SetGroup(0);
}

int vtkMPICommunicator::Initialize(vtkMPICommunicator* mpiComm, 
                                   vtkMPIGroup* group)
{
  if (this->Initialized)
    {
    return 0;
    }

  // If mpiComm has been initialized, it is guaranteed (unless
  // the MPI calls return an error somewhere) to have valid 
  // Communicator and Group
  if (!mpiComm->Initialized)
    {
    vtkWarningMacro("The communicator passed has not been initialized!");
    return 0;
    }

  this->KeepHandleOff();

  int nProcIds = group->GetNumberOfProcessIds();
  // The new group has to be a sub-class
  if ( ( nProcIds <= 0) ||
       ( mpiComm->Group == 0 ) ||
       ( nProcIds > mpiComm->Group->GetNumberOfProcessIds() ) )
    {
    vtkWarningMacro("The group or the communicator has " 
                    << "invalid number of ids.");
    return 0;
    }

  
  // Select the new processes
  int* ranks = new int[nProcIds];
  for(int i=0; i<nProcIds; i++)
    {
    ranks[i] = group->GetProcessId(i);
    }

  MPI_Group superGroup;
  MPI_Group subGroup;

  // Get the group from the argument
  int err;
  if ( (err = MPI_Comm_group(*(mpiComm->Handle), &superGroup))
       != MPI_SUCCESS )
    {
    delete[] ranks;
    MPI_Group_free(&superGroup);

    char *msg = vtkMPIController::ErrorString(err);
    vtkErrorMacro("MPI error occured: " << msg);
    delete[] msg;

    return 0;
    }
  
  // Create a new group by including the process ids in group
  if ( (err = MPI_Group_incl(superGroup, nProcIds, ranks, &subGroup))
       != MPI_SUCCESS )
    {
    delete[] ranks;
    MPI_Group_free(&superGroup);
    MPI_Group_free(&subGroup);

    char *msg = vtkMPIController::ErrorString(err);
    vtkErrorMacro("MPI error occured: " << msg);
    delete[] msg;

    return 0;
    }

  delete[] ranks;
  MPI_Group_free(&superGroup);

  this->Handle = new MPI_Comm;
  // Create the communicator from the group
  if ( (err  = MPI_Comm_create(*(mpiComm->Handle), subGroup, 
                               this->Handle) )
       != MPI_SUCCESS )
    {
    MPI_Group_free(&subGroup);
    delete this->Handle;
    this->Handle = 0;

    char *msg = vtkMPIController::ErrorString(err);
    vtkErrorMacro("MPI error occured: " << msg);
    delete[] msg;

    return 0;
    }
       
  MPI_Group_free(&subGroup);

  this->Initialized = 1;

  // Store the group so that this communicator can be used
  // to create new ones
  this->SetGroup(group);

  this->Modified();

  return 1;

}

// Start the copying process  
void vtkMPICommunicator::InitializeCopy(vtkMPICommunicator* source)
{
  if(!source)
    {
    return;
    }

  this->SetGroup(0);
  vtkMPIGroup* group = vtkMPIGroup::New();
  this->SetGroup(group);
  group->Delete();
  group = 0;
  this->Group->CopyFrom(source->Group);

  if (this->Handle && !this->KeepHandle)
    {
      MPI_Comm_free(this->Handle);
    }
  delete this->Handle;
  this->Handle = 0;

  this->Initialized = source->Initialized;
  this->Modified();
}

// Copy the MPI handle
void vtkMPICommunicator::CopyFrom(vtkMPICommunicator* source)
{
  this->InitializeCopy(source);

  if (source->Handle)
    {
    this->KeepHandleOn();
    this->Handle = new MPI_Comm;
    *(this->Handle) = *(source->Handle);
    }
}

// Duplicate the MPI handle
void vtkMPICommunicator::Duplicate(vtkMPICommunicator* source)
{
  this->InitializeCopy(source);

  this->KeepHandleOff();

  if (source->Handle)
    {
    this->Handle = new MPI_Comm;
    int err;
    if ( (err = MPI_Comm_dup(*(source->Handle), this->Handle))
          != MPI_SUCCESS ) 
      {
      char *msg = vtkMPIController::ErrorString(err);
      vtkErrorMacro("MPI error occured: " << msg);
      delete[] msg;
      }                      
    }
}

// overloaded functions for vtkIdType
#ifdef VTK_HAS_ID_TYPE
#ifdef VTK_USE_64BIT_IDS
static MPI_Datatype getMPIType(vtkIdType *data)
{
  return MPI_LONG_LONG;
}
#endif
#else
static MPI_Datatype getMPIType(int *data)
{
  return MPI_INT;
}
#endif

char* vtkMPICommunicator::Allocate(size_t size)
{
#ifdef MPIPROALLOC
  char* ptr;
  MPI_Alloc_mem(size, NULL, &ptr);
  return ptr;
#else
  return new char[size];
#endif
}

void vtkMPICommunicator::Free(char* ptr)
{
#ifdef MPIPROALLOC
  MPI_Free_mem(ptr);
#else
  delete[] ptr;
#endif
}

static int SendData(char* data, int length, int sizeoftype,
                    int remoteProcessId, int tag, 
                    MPI_Datatype datatype,
                    MPI_Comm *Handle,
                    int useCopy)
{

 if (useCopy)
    {
    int retVal;
    
    char* tmpData = vtkMPICommunicator::Allocate(length*sizeoftype);
    memcpy(tmpData, data, length*sizeoftype);
    retVal = MPI_Send(tmpData, length, datatype, 
                      remoteProcessId, tag, 
                      *(Handle));
    vtkMPICommunicator::Free(tmpData);
    return retVal;
    }
  else
    {
    return MPI_Send(data, length, datatype, 
                    remoteProcessId, tag, 
                    *(Handle));
    }
}

static int ReceiveData(char* data, int length, int sizeoftype,
                       int remoteProcessId, int tag, 
                       MPI_Datatype datatype, MPI_Comm *Handle,
                       int useCopy)
{
  MPI_Status status; 
  
  if (remoteProcessId == vtkMultiProcessController::ANY_SOURCE)
    {
    remoteProcessId = MPI_ANY_SOURCE;
    }
  
  if (useCopy)
    {
    int retVal;
    char* tmpData = vtkMPICommunicator::Allocate(length*sizeoftype);
    retVal = MPI_Recv(tmpData, length, datatype, 
                      remoteProcessId, tag, 
                      *(Handle), &status);
    memcpy(data, tmpData, length*sizeoftype);
    vtkMPICommunicator::Free(tmpData);
    return retVal;
    }
  else
    {
    return MPI_Recv(data, length, datatype, 
                    remoteProcessId, tag, 
                    *(Handle), &status);
    }

}

static int NoBlockSendData(char* data, int length,
                           int remoteProcessId, 
                           int tag, MPI_Datatype datatype, 
                           vtkMPICommunicator::Request& req, MPI_Comm *Handle)
{
    return MPI_Isend(data, length, datatype, 
                    remoteProcessId, tag, 
                    *(Handle), &req.Req);
}

static int NoBlockReceiveData(char* data, int length, 
                              int remoteProcessId, 
                              int tag, MPI_Datatype datatype, 
                              vtkMPICommunicator::Request& req, 
                              MPI_Comm *Handle)
{

  if (remoteProcessId == vtkMultiProcessController::ANY_SOURCE)
    {
    remoteProcessId = MPI_ANY_SOURCE;
    }
  
  return MPI_Irecv(data, length, datatype, 
                   remoteProcessId, tag, 
                   *(Handle), &req.Req);

}

int vtkMPICommunicator::CheckForMPIError(int err)
{

  if ( err == MPI_SUCCESS )
    {
    return 1;
    }
  else
    {
    char *msg = vtkMPIController::ErrorString(err);
    vtkGenericWarningMacro("MPI error occured: " << msg);
    delete[] msg;
    return 0;
    }

}

//----------------------------------------------------------------------------
int vtkMPICommunicator::Send(int* data, int length, int remoteProcessId, 
                            int tag)
{

  return CheckForMPIError(SendData(reinterpret_cast<char*>(data), length, 
                                   sizeof(int),
                                   remoteProcessId, tag, 
                                   MPI_INT, this->Handle, 
                                   vtkCommunicator::UseCopy));

}
//----------------------------------------------------------------------------
int vtkMPICommunicator::Send(unsigned long* data, int length, 
                             int remoteProcessId, int tag)
{

  return CheckForMPIError(SendData(reinterpret_cast<char*>(data), length, 
                                   sizeof(unsigned long),
                                   remoteProcessId, tag, 
                                   MPI_UNSIGNED_LONG, this->Handle,
                                   vtkCommunicator::UseCopy));

}
//----------------------------------------------------------------------------
int vtkMPICommunicator::Send(char* data, int length, 
                             int remoteProcessId, int tag)
{

  return CheckForMPIError(SendData(reinterpret_cast<char*>(data), length, 
                                   sizeof(char),
                                   remoteProcessId, tag, 
                                   MPI_CHAR, this->Handle, 
                                   vtkCommunicator::UseCopy));

}
//----------------------------------------------------------------------------
int vtkMPICommunicator::Send(unsigned char* data, int length, 
                             int remoteProcessId, int tag)
{

  return CheckForMPIError(SendData(reinterpret_cast<char*>(data), length, 
                                   sizeof(unsigned char),
                                   remoteProcessId, tag, 
                                   MPI_UNSIGNED_CHAR, this->Handle,
                                   vtkCommunicator::UseCopy));

}
//----------------------------------------------------------------------------
int vtkMPICommunicator::Send(float* data, int length, 
                             int remoteProcessId, int tag)
{

  return CheckForMPIError(SendData(reinterpret_cast<char*>(data), length, 
                                   sizeof(float),
                                   remoteProcessId, tag, 
                                   MPI_FLOAT, this->Handle, 
                                   vtkCommunicator::UseCopy));
 
}

//----------------------------------------------------------------------------
int vtkMPICommunicator::Send(double* data, int length, 
                             int remoteProcessId, int tag)
{

  return CheckForMPIError(SendData(reinterpret_cast<char*>(data), length, 
                                   sizeof(double),
                                   remoteProcessId, tag, 
                                   MPI_DOUBLE, this->Handle, 
                                   vtkCommunicator::UseCopy));

}

//----------------------------------------------------------------------------
#ifdef VTK_USE_64BIT_IDS
int vtkMPICommunicator::Send(vtkIdType* data, int length, 
                             int remoteProcessId, int tag)
{

  return CheckForMPIError(SendData(reinterpret_cast<char*>(data), length, 
                                   sizeof(vtkIdType),
                                   remoteProcessId, tag, 
                                   getMPIType(data), this->Handle,
                                   vtkCommunicator::UseCopy));

}
#endif

//----------------------------------------------------------------------------
int vtkMPICommunicator::NoBlockSend(int* data, int length, 
                                    int remoteProcessId, int tag,
                                    Request& req)
{

  return CheckForMPIError(NoBlockSendData(reinterpret_cast<char*>(data), 
                                          length,
                                          remoteProcessId, 
                                          tag, MPI_INT, req, this->Handle));

}
//----------------------------------------------------------------------------
int vtkMPICommunicator::NoBlockSend(unsigned long* data, int length, 
                                    int remoteProcessId, int tag,
                                    Request& req)
{

  return CheckForMPIError(NoBlockSendData(reinterpret_cast<char*>(data), 
                                          length, 
                                          remoteProcessId, 
                                          tag, MPI_UNSIGNED_LONG, req, 
                                          this->Handle));


}
//----------------------------------------------------------------------------
int vtkMPICommunicator::NoBlockSend(char* data, int length, 
                                    int remoteProcessId, int tag, Request& req)
{

  return CheckForMPIError(NoBlockSendData(reinterpret_cast<char*>(data), 
                                          length,  remoteProcessId,
                                          tag, MPI_CHAR, req, this->Handle));

}
//----------------------------------------------------------------------------
int vtkMPICommunicator::NoBlockSend(float* data, int length, 
                                    int remoteProcessId, int tag, Request& req)
{

  return CheckForMPIError(NoBlockSendData(reinterpret_cast<char*>(data), 
                                          length, remoteProcessId, 
                                          tag, MPI_FLOAT, req, this->Handle));
}



//----------------------------------------------------------------------------
int vtkMPICommunicator::Receive(int* data, int length, 
                                int remoteProcessId, int tag)
{

  return CheckForMPIError(ReceiveData(reinterpret_cast<char*>(data), length, 
                                      sizeof(int), remoteProcessId, tag, 
                                      MPI_INT, this->Handle, 
                                      vtkCommunicator::UseCopy));

}
//----------------------------------------------------------------------------
int vtkMPICommunicator::Receive(unsigned long* data, int length, 
                                int remoteProcessId, int tag)
{

  return CheckForMPIError(ReceiveData(reinterpret_cast<char*>(data), length,
                                      sizeof(unsigned long),
                                      remoteProcessId, tag, 
                                      MPI_UNSIGNED_LONG, this->Handle,
                                      vtkCommunicator::UseCopy));

}
//----------------------------------------------------------------------------
int vtkMPICommunicator::Receive(char* data, int length, 
                                int remoteProcessId, int tag)
{

  return CheckForMPIError(ReceiveData(reinterpret_cast<char*>(data), length, 
                                      sizeof(char), remoteProcessId, tag, 
                                      MPI_CHAR, this->Handle, 
                                      vtkCommunicator::UseCopy));

}
//----------------------------------------------------------------------------
int vtkMPICommunicator::Receive(unsigned char* data, int length, 
                                int remoteProcessId, int tag)
{

  return CheckForMPIError(ReceiveData(reinterpret_cast<char*>(data), length,
                                      sizeof(unsigned char), remoteProcessId, 
                                      tag, MPI_UNSIGNED_CHAR, this->Handle,
                                      vtkCommunicator::UseCopy));

}
//----------------------------------------------------------------------------
int vtkMPICommunicator::Receive(float* data, int length, 
                                int remoteProcessId, int tag)
{

  return CheckForMPIError(ReceiveData(reinterpret_cast<char*>(data), length, 
                                      sizeof(float), remoteProcessId, tag, 
                                      MPI_FLOAT, this->Handle, 
                                      vtkCommunicator::UseCopy));

}

//----------------------------------------------------------------------------
int vtkMPICommunicator::Receive(double* data, int length, 
                                int remoteProcessId, int tag)
{

  return CheckForMPIError(ReceiveData(reinterpret_cast<char*>(data), length,
                                      sizeof(double), remoteProcessId, tag, 
                                      MPI_DOUBLE, this->Handle, 
                                      vtkCommunicator::UseCopy));

}

//----------------------------------------------------------------------------
#ifdef VTK_USE_64BIT_IDS
int vtkMPICommunicator::Receive(vtkIdType* data, int length, 
                                int remoteProcessId, int tag)
{

  return CheckForMPIError(ReceiveData(reinterpret_cast<char*>(data), length, 
                                      sizeof(vtkIdType), remoteProcessId, tag, 
                                      getMPIType(data), this->Handle,
                                      vtkCommunicator::UseCopy));
}
#endif

//----------------------------------------------------------------------------
int vtkMPICommunicator::NoBlockReceive(int* data, int length, 
                                       int remoteProcessId, int tag,
                                       Request& req)
{

  return CheckForMPIError(NoBlockReceiveData(reinterpret_cast<char*>(data), 
                                             length, remoteProcessId, 
                                             tag, MPI_INT, req, this->Handle));

}
//----------------------------------------------------------------------------
int vtkMPICommunicator::NoBlockReceive(unsigned long* data, int length, 
                                       int remoteProcessId, int tag,
                                       Request& req)
{

  return CheckForMPIError(NoBlockReceiveData(reinterpret_cast<char*>(data), 
                                             length, remoteProcessId, 
                                             tag, MPI_UNSIGNED_LONG, req, 
                                             this->Handle));

}
//----------------------------------------------------------------------------
int vtkMPICommunicator::NoBlockReceive(char* data, int length, 
                                       int remoteProcessId, int tag,
                                       Request& req)
{

  return CheckForMPIError(NoBlockReceiveData(reinterpret_cast<char*>(data), 
                                             length, remoteProcessId, 
                                             tag, MPI_CHAR, req, 
                                             this->Handle));

}
//----------------------------------------------------------------------------
int vtkMPICommunicator::NoBlockReceive(float* data, int length, 
                                       int remoteProcessId, int tag,
                                       Request& req)
{

  return CheckForMPIError(NoBlockReceiveData(reinterpret_cast<char*>(data), 
                                             length, remoteProcessId, 
                                             tag, MPI_FLOAT, req, 
                                             this->Handle));

}

int vtkMPICommunicator::Request::Test()
{
  MPI_Status status;
  int retVal;

  int err = MPI_Test(&this->Req, &retVal, &status);
  
  if ( err == MPI_SUCCESS )
    {
    return retVal;
    }
  else
    {
    char *msg = vtkMPIController::ErrorString(err);
    vtkGenericWarningMacro("MPI error occured: " << msg);
    delete[] msg;
    return 0;
    }
}

void vtkMPICommunicator::Request::Wait()
{
  MPI_Status status;

  int err = MPI_Wait(&this->Req, &status);
  
  if ( err != MPI_SUCCESS )
    {
    char *msg = vtkMPIController::ErrorString(err);
    vtkGenericWarningMacro("MPI error occured: " << msg);
    delete[] msg;
    }
}

void vtkMPICommunicator::Request::Cancel()
{
  int err = MPI_Cancel(&this->Req);
  
  if ( err != MPI_SUCCESS )
    {
    char *msg = vtkMPIController::ErrorString(err);
    vtkGenericWarningMacro("MPI error occured: " << msg);
    delete[] msg;
    }
}
