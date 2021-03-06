/*
darc, the Durham Adaptive optics Real-time Controller.
Copyright (C) 2010 Alastair Basden.

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU Affero General Public License as
published by the Free Software Foundation, either version 3 of the
License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU Affero General Public License for more details.

You should have received a copy of the GNU Affero General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/
/**
   The code here is used to create a shared object library, which can then be swapped around depending on which mirrors/interfaces you have in use, ie you simple rename the mirror file you want to mirror.so (or better, change the soft link), and restart the coremain.

A library for figure sensor input, which simply places the actuator demands straight onto the mirror.  This also updates the RTC with the current frame number from the actuator demands.
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <pthread.h>
#include <signal.h>
#ifdef NSL
#include <nslapi.h>
#else
#ifdef NONSL
#else
#include <fxsl.h>
#include <fxslapi.h>
#endif
#endif
#include "rtcfigure.h"
#ifndef NODM
#include "powerdaq.h"
#include "powerdaq32.h"
#else
int _PdAO32Write(int mirhandle,int i,unsigned short act){
  return 0;
}
#endif
#include "darc.h"


typedef enum{
  ACTINIT,
  ACTMAPPING,
  ACTOFFSET,
  ACTSCALE,
  ACTSOURCE,
  FIGUREADDER,
  FIGUREDEBUG,
  FIGUREMULTIPLIER,
  NBUFFERVARIABLES
}figureNames;

#define makeParamNames() bufferMakeNames(NBUFFERVARIABLES,\
					 "actInit","actMapping","actOffset","actScale","actSource","figureAdder","figureDebug","figureMultiplier" \
					 )

#define errorChk(functionCall) {int error; if((error=functionCall)<0) { \
	                           fprintf(stderr, "Error %d at line %d in function call %s\n", error, __LINE__, #functionCall); \
	                           exit(EXIT_FAILURE);}}
#ifndef NSL
typedef unsigned int uint32;
#endif
typedef enum _state
{
   closed,
   unconfigured,
   configured,
   running
} tState;

#define HDRSIZE 8 //the size of the SL240 header expected...
typedef struct{
  int open;
  int sl240Opened;
  pthread_mutex_t m;
  pthread_mutex_t mInternal;
  pthread_cond_t cond;
  float **actsRequired;//shared with the figure sensor RTC core.
  pthread_t threadid;
  unsigned short *acts;//temporary space for reading actuators into
  int nacts;
#ifdef NSL
  nslDeviceInfo info;
  nslHandle handle;
  int gotsyncdv;
#else
#ifdef NONSL
  //nowt
#else
  /*HANDLE*/int handle;//the sl240 handle - is defined as type int in fxsl.h but a name clash with powerdaq.h means have to define it here...
  fxsl_configstruct cfg;
#endif
#endif
  uint32 timeout;
  uint32 fibrePort;
  unsigned int *threadAffinity;
  int threadAffinElSize;
  int threadPriority;
  unsigned int *frameno;
  char *arr;
  int arrsize;
  int mirhandle;
  tState state;//state of the acquisition session.
  int board;
  int adapterType;
  int err;
  int debug;
  int *actMapping;//map actuator number to DAC number.
  int initLen;//number of entries in actInit.
  unsigned short *actInit;//only used when library is opened. Can be used to set default values to unused actuators.
  int actMappingLen;
  int *actSource;
  float *actScale;
  float *actOffset;
  char *paramNames;
  int index[NBUFFERVARIABLES];
  void *values[NBUFFERVARIABLES];
  char dtype[NBUFFERVARIABLES];
  int nbytes[NBUFFERVARIABLES];
  float multiplier;
  float adder;
}figureStruct;

#ifdef NSL
#else
#ifdef NONSL
#else
char *
GetErrStr(fx_ulong status)
{
  char *p;
  switch (status) {
  case FXSL_SUCCESS: p = "Success";
    break;
  case FXSL_SYSTEM_ERROR: p = "System Error";
    break;
  case FXSL_BAD_PARAMETER: p = "Bad Parameter";
    break;
  case FXSL_LINK_ERROR: p = "Link Error";
    break;
  case FXSL_NO_DATA: p = "No Data";
    break;
  case FXSL_BUSY: p = "Busy";
    break;
  case FXSL_DRIVER_ERROR: p = "Driver Error";
    break;
  case FXSL_TIMEOUT: p = "Timeout";
    break;
/*   case FXSL_CALL_UNSUPPOPRTED: p = "Unsupported Call"; */
/*     break; */
  case FXSL_INSUFFICIENT_RESOURCES: p = "Insufficient Resources";
    break;
  case FXSL_CRC_ERROR: p = "CRC Error";
    break;
  case FXSL_FIFO_OVERFLOW: p = "Receive FIFO Overflow";
    break;
  default: p = "Unknown Error";
  }
  return (p);
}
#endif
#endif

void CleanUpSingleAO(figureStruct *pAoData){
#ifndef NODM
   if(pAoData->state == running){
      pAoData->state = configured;
   }
   if(pAoData->state == configured){
     errorChk(_PdAOutReset(pAoData->mirhandle));
     // need also to call this function if the board is a PD2-AO-xx
     if(pAoData->adapterType & atPD2AO){
       errorChk(_PdAO32Reset(pAoData->mirhandle));
     }
     pAoData->state = unconfigured;
   }
   if(pAoData->mirhandle > 0 && pAoData->state == unconfigured){
     errorChk(PdAcquireSubsystem(pAoData->mirhandle, AnalogOut, 0));
   }
   pAoData->state = closed;
#endif
}

int figureDofree(void **figureHandle){
  figureStruct *f;
  if(*figureHandle!=NULL){
    f=(figureStruct*)*figureHandle;
    if(f->sl240Opened)
#ifdef NSL
      nslClose(&f->handle);
#else
#ifdef NONSL
#else
      fxsl_close(f->handle);
#endif
#endif
    f->sl240Opened=0;
    if(f->arr!=NULL)free(f->arr);
    f->arr=NULL;
    CleanUpSingleAO(f);
    //if(f->mInternal!=NULL){
      pthread_mutex_destroy(&f->mInternal);
      //}
    free(*figureHandle);
  }

  *figureHandle=NULL;
  return 0;
}

int InitSingleAO(figureStruct *f){
#ifndef NODM
   Adapter_Info adaptInfo;
   // get adapter type
   errorChk(_PdGetAdapterInfo(f->board, &adaptInfo));
   f->adapterType = adaptInfo.atType;
   if(f->adapterType & atMF)
     printf("This is an MFx board\n");
   else
     printf("This is an AO32 board\n");
   f->mirhandle = PdAcquireSubsystem(f->board, AnalogOut, 1);
   if(f->mirhandle < 0){
      printf("SingleAO: PdAcquireSubsystem failed\n");
      f->state=closed;
      return 1;
   }
   f->state = unconfigured;
   errorChk(_PdAOutReset(f->mirhandle));
   // need also to call this function if the board is a PD2-AO-xx
   if(f->adapterType & atPD2AO){
      errorChk(_PdAO32Reset(f->mirhandle));
   }
#endif
   return 0;
}

#ifdef NSL
int sl240GetSOF(figureStruct *mirstr){//nslHandle *handle,int timeout,int attempts){
  int rt=0,done=0;
  int syncerrmsg=0;
  uint32 flagsIn;
  uint32 sofWord[1024];
  uint32 bytesXfered;
  uint32 flagsOut;
  uint32 status;
  int nbytes=0;
  if(mirstr->gotsyncdv){//have previously got the sof while reading truncated frame
    done=1;
    mirstr->gotsyncdv=0;
    nbytes=sizeof(uint32);//so message isn't printed out below.
  }
  flagsIn = NSL_DMA_USE_SYNCDV;
  while(done==0){
    //pthread_mutex_lock(&camstr->m);
    status = nslRecv(&mirstr->handle, (void *)sofWord, sizeof(uint32)*1024, flagsIn, mirstr->timeout, &bytesXfered, &flagsOut, NULL);
    //pthread_mutex_unlock(&camstr->m);
    if (status == NSL_SUCCESS) {
      if (flagsOut & NSL_DMA_USE_SYNCDV) {
	//printf("SYNC frame data = 0x%x bytes %d\n", sofWord,bytesXfered);
	done=1;
	rt=0;
	nbytes+=bytesXfered;
	
	//printf("New frame %d\n",cam);
      }else{//it may be here that the buffer is partially filled - in which case, we should continue reading 4 bytes for up to a full frame size, and see if we get the SYNC_DV.
	if(syncerrmsg==0){
	  printf("WARNING: SYNCDV not set - may be out of sync, sof[0]=%#x bytes %d timeout %d\n",sofWord[0],bytesXfered,mirstr->timeout);
	}
	syncerrmsg++;
	rt=1;
	nbytes+=bytesXfered;
      }
    }else if(status!=NSL_TIMEOUT){
      printf("figureerror: %s\n",nslGetErrStr(status));
      rt=1;
      done=1;
    }else{
      printf("Timeout waiting for new frame\n");
      rt=1;
      done=1;
    }
  }
  if((syncerrmsg>0 || nbytes!=sizeof(uint32)) && rt==0)//previously printed a sync warning... so now print an ok msg
    printf("Start of frame received okay after %d tries (%d bytes)\n",syncerrmsg,nbytes);
  return rt;
}
#else
#ifdef NONSL
int sl240GetSOF(figureStruct *mirstr){
  return 0;
}
#else
int
sl240GetSOF(HANDLE handle, fx_long timeout,int attempts)
{
  fx_ulong status;
  fxsl_configstruct cfg;
  /* FXSL documentation recommends recv buffer
   * be at least 4 bytes larger than largest expected frame
   * when using SYNC */
  unsigned int sofWord;//[0x4000];  
  unsigned int i;
  fx_ulong flagsOut, bytesXfered;

  status = fxsl_get_config(handle, &cfg);
  if (status != FXSL_SUCCESS) {
    printf("Failed to read SL240 config\n");
  } else {
    cfg.convert_dsync = 1;
    status = fxsl_set_config(handle, &cfg);
    if (status != FXSL_SUCCESS) {
      printf("Failed to set SL240 config\n");
    } else {
      for (i = 0; i <attempts; i++) {
	flagsOut = FXSL_USE_SYNC_DVALID;
	status = fxsl_recv(handle, (void *)&sofWord, 0x4, &flagsOut, timeout, 
			   &bytesXfered);
	if (status==FXSL_SUCCESS && (flagsOut & FXSL_USE_SYNC_DVALID))
	  break;
	else if(status==FXSL_TIMEOUT){
	  printf("Timeout waiting for figure sensor SoF\n");
	  break;
	}else{
	  status = FXSL_SYSTEM_ERROR;
	}
      }
    }
  }
  return !(status==FXSL_SUCCESS);
}
#endif
#endif

#ifdef NSL
int sl240GetBytes(figureStruct *f){
  int rt=0;
  uint32 flagsIn,bytesXfered,flagsOut,status;
  flagsIn = NSL_DMA_USE_SYNCDV;//0;
  //pthread_mutex_lock(&camstr->m);

  status = nslRecv(&f->handle, (void *)f->arr, f->arrsize, flagsIn, f->timeout,&bytesXfered, &flagsOut, NULL);
  //pthread_mutex_unlock(&camstr->m);
  if (status == NSL_TIMEOUT) {
    printf("Received timeout\n");
    rt=1;
  } else if (status == NSL_LINK_ERROR) {
    printf("Link error detected\n");
    rt=1;
  } else if (status == NSL_SUCCESS){
    if(flagsOut&NSL_DMA_USE_SYNCDV){
      printf("SYNCDV received while waiting for data - truncated frame (%d/%d bytes)\n",bytesXfered,f->arrsize);
      //So, have already got the sof for the next frame...
      f->gotsyncdv=1;
      rt=1;
    }else if(f->arrsize!=bytesXfered){
      printf("%d bytes requested, %d bytes received\n", f->arrsize, bytesXfered);
      rt=1;
    }else{
      //printf("got data okay\n");
    }
  }else{
    printf("%s\n", nslGetErrStr(status));
    rt=1;
  }
  return rt;
}
#else
#ifdef NONSL
int sl240GetBytes(figureStruct *f){
  return 0;
}
#else
int
sl240GetBytes(HANDLE handle, fx_ubyte *buffer, fx_ulong nBytes, fx_ulong timeout)
{
  fx_ulong status;
  fxsl_configstruct cfg;
  fx_ulong flagsOut, bytesXfered;


  status = fxsl_get_config(handle, &cfg);
  if (status != FXSL_SUCCESS) {
    printf("Failed to read SL240 config\n");
  } else {
    cfg.convert_dsync = 0;
    status = fxsl_set_config(handle, &cfg);
    if (status != FXSL_SUCCESS) {
      printf("Failed to set SL240 config\n");
    } else {
      flagsOut = 0;
      status = fxsl_recv(handle, buffer, nBytes, &flagsOut, timeout, 
			 &bytesXfered);
      if (status!=FXSL_SUCCESS || (bytesXfered != nBytes)) {
	printf("sl240GetBytes: %d bytes requested, %u bytes received, error %s\n",
	       nBytes, bytesXfered,GetErrStr(status));
	status = FXSL_SYSTEM_ERROR;
      }
    }
  }
  return !(status==FXSL_SUCCESS);
}
#endif
#endif
int figureGetActuators(figureStruct *f){
  //actuators should be placed into f->acts.
  //First get start of frame
  //uint32 flagsIn;
  //uint32 sofWord;
  //uint32 bytesXfered;
  //uint32 flagsOut;
  uint32 status;
  int err=0;//,done=0;
  //  int syncerrmsg=0;
  //  int i;
  //nslSeq seq;
  //Read 4 bytes until get a start of frame, for at most nacts... after which, consider it an error.
  //for(i=0; i<f->nacts; i++){
#ifdef NSL
  status=sl240GetSOF(f);
#else
#ifdef NONSL
  status=sl240GetSOF(f);
#else
  status=sl240GetSOF(f->handle,(fx_long)f->timeout,f->nacts+4);
#endif
#endif
  //  if(status==FXSL_SUCCESS){
  //  break;
  //}
  //}
  if(status!=0){
    printf("No Sync for figure sensor actuators\n");
    err=-1;
  }else{
#ifdef NSL
    status=sl240GetBytes(f);
#else
#ifdef NONSL
    status=sl240GetBytes(f);
#else
    status=sl240GetBytes(f->handle,(fx_ubyte*)f->arr,f->arrsize,(fx_ulong)f->timeout);
#endif
#endif
    if(status!=0){
      printf("Error getting bytes\n");
      err=1;
    }
      
  }
  return err;
}
  /*  flagsIn = NSL_DMA_USE_SYNCDV;
  while(done==0){
    status = nslRecv(&f->handle, (void *)&sofWord, sizeof(uint32), flagsIn, f->timeout, &bytesXfered, &flagsOut, NULL);
    if (status == NSL_SUCCESS) {
      if (flagsOut & NSL_DMA_USE_SYNCDV) {
	//printf("SYNC frame data = 0x%x bytes %d\n", sofWord,bytesXfered);
	done=1;
	err=0;
	//printf("New frame %d\n",cam);
      }else{//it may be here that the buffer is partially filled - in which case, we should continue reading 4 bytes for up to a full frame size, and see if we get the SYNC_DV.
	if((syncerrmsg%f->nacts)==0){
	  printf("WARNING: SYNCDV not set - figure library may be out of sync, sof=%#x bytes %d timeout %d\n",sofWord,bytesXfered,f->timeout);
	}
	syncerrmsg++;
	err=-1;
      }
    }else if(status!=NSL_TIMEOUT){
      printf("camerror: %s\n",nslGetErrStr(status));
      err=-1;
      done=1;
    }else{
      printf("Timeout waiting for new frame (figure library)\n");
      err=1;
      done=1;
    }
    }
  if(syncerrmsg>0 && err==0)//previously printed a sync warning... so now print an ok msg
    printf("Start of frame received okay after %d tries\n",syncerrmsg);
  
  if(err==0){
    //now read all the actuators.
    flagsIn=0;
    flagsOut=0;
    status = nslRecv(&f->handle, (void *)f->arr,f->arrsize, flagsIn, f->timeout,&bytesXfered, &flagsOut, NULL);
    if (status == NSL_TIMEOUT) {
      printf("Received timeout (figure library)\n");
      err=1;
    } else if (status == NSL_LINK_ERROR) {
      printf("Link error detected\n");
      err=-1;
    } else if (status == NSL_SUCCESS){
      if(f->arrsize!=bytesXfered){
	printf("%d bytes requested, got %d\n", f->arrsize,bytesXfered);
	err=-1;
      }else{
	//printf("got data okay\n");
      }
    }else{
      printf("%s\n", nslGetErrStr(status));
      err=-1;
    }
  }
  return err;
  }*/
/*
int figureSubtractPiston(figureStruct *f){
  int i;
  float s=0.;
  for(i=0; i<f->nacts; i++)
    s+=f->acts[i];
  s/=f->nacts;
  for(i=0; i<f->nacts; i++)
    f->acts[i]-=s;
  return 0;
  }*/

//#define MASKED_MODIFY(oldval, modval, mask) (((oldval) & ~(mask)) | ((modval) & (mask)))
/**
   This does the same as nslmon -u X --clrf
   Clears the receive fifo.
   Copied from the source code for nslmon.
*/
/*
int figureClearReceiveBuffer(figureStruct *f){
  uint32 state;
  printf("clearing receive buffer (clrf)\n");
  state = nslReadCR(&f->handle, 0x8);
  state = MASKED_MODIFY(state, 0x2000, 0x00002000);
  
  nslWriteCR(&f->handle, 0x8, state);
  
  //usysMsTimeDelay(10);
  usleep(10000);
  
  state = MASKED_MODIFY(state, 0, 0x00002000);
  nslWriteCR(&f->handle, 0x8, state);
  printf("clearing receive buffer (clrf) DONE\n");
  return 0;
}
*/
//#undef MASKED_MODIFY


int figureSetThreadAffinityAndPriority(unsigned int *threadAffinity,int threadPriority,int threadAffinElSize){
  int i;
  cpu_set_t mask;
  int ncpu;
  struct sched_param param;
  printf("Getting CPUs\n");
  ncpu= sysconf(_SC_NPROCESSORS_ONLN);
  printf("Got %d CPUs\n",ncpu);
  CPU_ZERO(&mask);
  printf("Setting %d CPUs\n",ncpu);
  for(i=0; i<ncpu && i<threadAffinElSize*32; i++){
    if(((threadAffinity[i/32])>>(i%32))&1){
      CPU_SET(i,&mask);
    }
  }
  if(sched_setaffinity(0,sizeof(cpu_set_t),&mask))
    printf("Error in sched_setaffinity: %s\n",strerror(errno));
  printf("Setting setparam\n");
  param.sched_priority=threadPriority;
  if(sched_setparam(0,&param)){
    printf("Error in sched_setparam: %s - probably need to run as root if this is important\n",strerror(errno));
  }
  if(sched_setscheduler(0,SCHED_RR,&param))
    printf("sched_setscheduler: %s - probably need to run as root if this is important\n",strerror(errno));
  if(pthread_setschedparam(pthread_self(),SCHED_RR,&param))
    printf("error in pthread_setschedparam - maybe run as root?\n");
  return 0;
}

/**
   A thread started by figureOpen and stopped by figureClose, which get new actuator setpoints when they are ready, and copies them into actsRequired.
*/
void *figureWorker(void *ff){
  figureStruct *f=ff;
  //int s;
  //float pist;
  int i;
  float *actsReq;
  figureSetThreadAffinityAndPriority(f->threadAffinity,f->threadPriority,f->threadAffinElSize);
  if(f->open && f->actInit!=NULL){
    pthread_mutex_lock(&f->mInternal);//lock it so that actMapping doesn't change.
    for(i=0; i<f->initLen; i++){
      _PdAO32Write(f->mirhandle,i,f->actInit[i]);
    }
    pthread_mutex_unlock(&f->mInternal);//lock it so that actMapping doesn't change.
  }
  while(f->open){
    //get the actuators from the actuator interface
    f->err=figureGetActuators(f);
    if(f->err==0){
      //write the actuators directly to the mirror.
      pthread_mutex_lock(&f->mInternal);//lock it so that actMapping etc doesn't change.
      if(f->actMapping==NULL){
	for(i=0; i<f->nacts; i++){
	  f->err|=_PdAO32Write(f->mirhandle,i,f->acts[i]);
	}
      }else{//actMapping is specified.
	if(f->actSource==NULL){
	  if(f->actScale==NULL){
	    if(f->actOffset==NULL){
	      for(i=0; i<f->actMappingLen; i++){
		f->err|=_PdAO32Write(f->mirhandle,f->actMapping[i],f->acts[i]);
	      }
	    }else{//actoffset defined.
	      for(i=0; i<f->actMappingLen; i++){
		f->err|=_PdAO32Write(f->mirhandle,f->actMapping[i],(unsigned short)(f->acts[i]+f->actOffset[i]));
	      }
	    }
	  }else{//actscale defined
	    if(f->actOffset==NULL){
	      for(i=0; i<f->actMappingLen; i++){
		f->err|=_PdAO32Write(f->mirhandle,f->actMapping[i],(unsigned short)(f->acts[i]*f->actScale[i]));
	      }
	    }else{//actScale and actoffset defined
	      for(i=0; i<f->actMappingLen; i++){
		f->err|=_PdAO32Write(f->mirhandle,f->actMapping[i],(unsigned short)(f->acts[i]*f->actScale[i]+f->actOffset[i]));
	      }
	    }
	  }
	}else{//actSource defined
	  if(f->actScale==NULL){
	    if(f->actOffset==NULL){
	      for(i=0; i<f->actMappingLen; i++){
		f->err|=_PdAO32Write(f->mirhandle,f->actMapping[i],f->acts[f->actSource[i]]);
	      }
	    }else{//actSource and actoffset defined.
	      for(i=0; i<f->actMappingLen; i++){
		f->err|=_PdAO32Write(f->mirhandle,f->actMapping[i],(unsigned short)(f->acts[f->actSource[i]]+f->actOffset[i]));
	      }
	    }
	  }else{//actSource and actscale defined
	    if(f->actOffset==NULL){
	      for(i=0; i<f->actMappingLen; i++){
		f->err|=_PdAO32Write(f->mirhandle,f->actMapping[i],(unsigned short)(f->acts[f->actSource[i]]*f->actScale[i]));
	      }
	    }else{//actSource and actScale and actoffset defined
	      for(i=0; i<f->actMappingLen; i++){
		f->err|=_PdAO32Write(f->mirhandle,f->actMapping[i],(unsigned short)(f->acts[f->actSource[i]]*f->actScale[i]+f->actOffset[i]));
	      }
	    }
	  }
	}
      }
      pthread_mutex_unlock(&f->mInternal);
      //update the RTC actuator frame number.
      pthread_mutex_lock(&f->m);
      *(f->frameno)=((unsigned int*)f->arr)[0];//copy frame number
      
      if(*(f->actsRequired)==NULL){
	if((*(f->actsRequired)=malloc(f->nacts*sizeof(float)))==NULL){
	  printf("Error actsRequired malloc\n");
	  f->err=1;
	}
      }
      if(*(f->actsRequired)!=NULL){
	actsReq=*(f->actsRequired);
	for(i=0; i<f->nacts; i++){
	  actsReq[i]=(f->acts[i]+f->adder)*f->multiplier;
	}
      }
      
      pthread_mutex_unlock(&f->m);

      if(f->err==0){
	//And update the RTC actuator frame number.
	//pthread_mutex_lock(&f->m);
	//*(f->frameno)=((unsigned int*)f->arr)[0];//copy frame number
	if(f->debug==1)
	  printf("Sending actuators for frame %u (first actuator %d)\n",((unsigned int*)f->arr)[0],(int)f->acts[0]);
	else if(f->debug==2){
	  printf("Sending actuators for frame %u\n",((unsigned int*)f->arr)[0]);
	  for(i=0;i<f->nacts; i++){
	    printf("0x%4x ",f->acts[i]);
	    if((i&0x7)==0)
	      printf("\n");
	  }
	  printf("\n\n");
	}else if(f->debug==3){
	  printf("Sending actuators for frame %u\n",((unsigned int*)f->arr)[0]);
	  for(i=0;i<f->nacts; i++){
	    printf("%6d ",(int)f->acts[i]);
	    if((i&0x7)==0)
	      printf("\n");
	  }
	  printf("\n\n");

	}else if(f->debug==4){
	  printf("Sending actuators for frame %u\n",((unsigned int*)f->arr)[0]);
	  if(f->actMapping==NULL){
	    for(i=0;i<f->nacts;i++){
	      printf("[%d] 0x%4x, ",i,f->acts[i]);
	      if((i&0x7)==0)
		printf("\n");
	    }
	  }else{
	    for(i=0;i<f->actMappingLen;i++){
	      if(f->actSource==NULL){
		if(f->actScale==NULL){
		  if(f->actOffset==NULL){
		    printf("[%d] 0x%4x, ",f->actMapping[i],f->acts[i]);
		  }else{
		    printf("[%d] 0x%4x, ",f->actMapping[i],(unsigned short)(f->acts[i]+f->actOffset[i]));
		  }
		}else{
		  if(f->actOffset==NULL){
		    printf("[%d] 0x%4x, ",f->actMapping[i],(unsigned short)(f->acts[i]*f->actScale[i]));
		  }else{
		    printf("[%d] 0x%4x, ",f->actMapping[i],(unsigned short)(f->acts[i]*f->actScale[i]+f->actOffset[i]));
		  }
		}
	      }else{
		if(f->actScale==NULL){
		  if(f->actOffset==NULL){
		    printf("[%d] 0x%4x, ",f->actMapping[i],f->acts[f->actSource[i]]);
		  }else{
		    printf("[%d] 0x%4x, ",f->actMapping[i],(unsigned short)(f->acts[f->actSource[i]]+f->actOffset[i]));
		  }
		}else{
		  if(f->actOffset==NULL){
		    printf("[%d] 0x%4x, ",f->actMapping[i],(unsigned short)(f->acts[f->actSource[i]]*f->actScale[i]));
		  }else{
		    printf("[%d 0x%4x, ",f->actMapping[i],(unsigned short)(f->acts[f->actSource[i]]*f->actScale[i]+f->actOffset[i]));
		  }
		}
	      }
	      if((i&0x7)==0)
		printf("\n");

	    }
	    printf("\n\n");
	  }
	}
	//Note - since we're not providing the RTC with the actuator demands, we don't need to wake it up.
	//However, we do need to allocate the actsRequired array so that the RTC picks up the frame number.
	//Actually - we may as well also paste the actuators into it, so that the dmc can be used for other processing.
      }else{
	printf("Error setting actuators\n");
      }

    }else{
      printf("Error getting actuators\n");
    }
  }
  //do some clearing up.
  pthread_mutex_lock(&f->m);
  if(*(f->actsRequired)!=NULL){
    free(*f->actsRequired);
    *f->actsRequired=NULL;
  }
  pthread_mutex_unlock(&f->m);
  return NULL;
}

#ifdef NSL
#define MASKED_MODIFY(oldval, modval, mask) (((oldval) & ~(mask)) | ((modval) & (mask)))
/**
   This does the same as nslmon -u X --clrf
   Clears the receive fifo.
   Copied from the source code for nslmon.
*/

int figureClearReceiveBuffer(figureStruct *f){
  uint32 state=0;
  printf("clearing receive buffer (clrf)\n");
  state = nslReadCR(&f->handle, 0x8);
  state = MASKED_MODIFY(state, 0x2000, 0x00002000);
  
  nslWriteCR(&f->handle, 0x8, state);
  //usysMsTimeDelay(10);
  usleep(10000);
  
  state = MASKED_MODIFY(state, 0, 0x00002000);
  nslWriteCR(&f->handle, 0x8, state);
  printf("clearing receive buffer (clrf) DONE\n");
  return 0;
}

#undef MASKED_MODIFY



#else
#ifdef NONSL
#else

/**
   Set up the SL240 card (single channel).  EJY.
*/
fx_ulong
sl240Setup(HANDLE handle,fxsl_configstruct cfg)
{
  fx_ulong status;
  fx_ulong cr;
  //fxsl_configstruct cfg;

  /* Reset the board - write bit 1 of register 4 */
  status = fxsl_read_CR(handle, 0x4, &cr);
  if (status != FXSL_SUCCESS) {
    printf( "Failed to read SL240 control reg.: %s\n",
	    GetErrStr(status));
    return(status);
  }
  status = fxsl_write_CR(handle, 0x4, cr | 0x2);
  if (status!= FXSL_SUCCESS) {
    printf("Failed to reset SL240 board: %s\n",
	    GetErrStr(status));
    return(status);
  }

  status = fxsl_get_config(handle, &cfg);
  if (status != FXSL_SUCCESS) {
    printf("Failed to read SL240 config\n");
  } else {

    cfg.useCRC = 1;
    cfg.use_flow_control = 0;
    cfg.swapBytes = 0;
    cfg.halt_on_link_errors = 1;
    cfg.loopConfig = 0;
    status = fxsl_set_config(handle, &cfg);
    if (status != FXSL_SUCCESS) {
      printf("Failed to set SL240 config\n");
    }
  }
  if (status != FXSL_SUCCESS) 
    printf("sl240Setup: %s\n", GetErrStr(status));
  return !(status==FXSL_SUCCESS);
}
#endif
#endif

/**
   Open a channel for reading actuator setpoints into this figure sensor.  Must be of type name.  Args are passed in an int array of size n, which can be cast if necessary.  Any state data is returned in figureHandle, which should be NULL if an error arises.
   Name is used if a library can support more than one camera, and to check that the currently compiled library is what you want it to be.
   The mutex should be obtained whenever new actuator setpoints arrive and are placed into actsRequired.  actsRequired should be allocated.
*/

int figureOpen(char *name,int n,int *args,paramBuf *pbuf,circBuf *rtcErrorBuf,char *prefix,arrayStruct *arr,void **figureHandle,int nthreads,unsigned int thisiter,unsigned int **frameno,int *framenoSize,int totCents,int nacts,pthread_mutex_t m,pthread_cond_t cond,float **actsRequired){
  int err=0;
  figureStruct *f=NULL;
#ifdef NSL
  uint32 status;
#else
#ifdef NONSL
#else
  uint32 status;
#endif
#endif
  char *pn;
  printf("Initialising figure %s\n",name);
  if((pn=makeParamNames())==NULL){
    printf("Error makeing paramList - please recode figureSL240SCPassThrough.c\n");
    *figureHandle=NULL;
    return 1;
  }
  if((*figureHandle=malloc(sizeof(figureStruct)))==NULL){
    printf("Error malloc figureHandle\n");
    err=1;
  }else{
    f=(figureStruct*)*figureHandle;
    memset(f,0,sizeof(figureStruct));
    f->paramNames=pn;
    if(n>5){
      f->timeout=args[0];
      f->fibrePort=args[1];
      f->threadAffinElSize=args[2];
      f->threadPriority=args[3];
      f->debug=args[4];
      f->threadAffinity=(unsigned int*)&args[5];
      if(n!=5+args[2]){
	printf("Wrong number of figure sensor library arguments - should be >5, was %d\n",n);
	err=1;
      }
    }else{
      printf("Wrong number of figure sensor library arguments - should be >5, was %d\n",n);
      err=1;
    }
  }
  if(err==0){
    f->m=m;
    f->cond=cond;
    f->actsRequired=actsRequired;
    f->nacts=nacts;
    if(*framenoSize==0){//need to allocate the space
      *frameno=malloc(sizeof(unsigned int));
      *framenoSize=1;
    }
    if(*frameno==NULL){
      printf("Unable to malloc figure frameno\n");
      *framenoSize=0;
    }
    f->frameno=*frameno;
    if(pthread_mutex_init(&f->mInternal,NULL)){
      printf("Error init figure internal mutex\n");
      err=1;
    }
    //DMA array has to be a whole number of int32 for the sl240, ie multiple of 4 bytes.
    f->arrsize=(HDRSIZE+nacts*sizeof(unsigned short)+3)&(~0x3);
    if((f->arr=malloc(f->arrsize))==NULL){
      printf("couldn't malloc arr\n");
      err=1;
    }else{
      memset(f->arr,0,f->arrsize);
      f->acts=(unsigned short*)&(f->arr[HDRSIZE]);
    }
#ifdef NSL
    status=nslOpen(f->fibrePort, &f->handle);
    if (status != NSL_SUCCESS) {
      printf("Failed to open SL240 port %d: %s\n\n",f->fibrePort,nslGetErrStr(status));
      err=1;
    }else{
      f->sl240Opened=1;
      status = nslGetDeviceInfo(&f->handle, &f->info);
      if (status != NSL_SUCCESS) {
	printf("Failed to get SL240 device info: %s",nslGetErrStr(status));
	err=1;
      }else{
	//set up card state.
	status = nslSetState(&f->handle, NSL_EN_EWRAP, 0);
	if (status != NSL_SUCCESS){
	  printf("%s\n",nslGetErrStr(status));err=1;}
	status = nslSetState(&f->handle,NSL_EN_RECEIVE, 1);
	if (status != NSL_SUCCESS){
	  printf("%s\n",nslGetErrStr(status));err=1;}
	status = nslSetState(&f->handle,NSL_EN_RETRANSMIT, 0);
	if (status != NSL_SUCCESS){
	  printf("%s\n",nslGetErrStr(status));err=1;}
	status = nslSetState(&f->handle,NSL_EN_CRC, 1);
	if (status != NSL_SUCCESS){
	  printf("%s\n",nslGetErrStr(status));err=1;}
	status = nslSetState(&f->handle,NSL_EN_FLOW_CTRL, 0);
	if (status != NSL_SUCCESS){
	  printf("%s\n",nslGetErrStr(status));err=1;}
	status = nslSetState(&f->handle,NSL_EN_LASER, 1);
	if (status != NSL_SUCCESS){
	  printf("%s\n",nslGetErrStr(status));err=1;}
	status = nslSetState(&f->handle,NSL_EN_BYTE_SWAP, 0);
	if (status != NSL_SUCCESS){
	  printf("%s\n",nslGetErrStr(status));err=1;}
	status = nslSetState(&f->handle,NSL_EN_WORD_SWAP, 0);
	if (status != NSL_SUCCESS){
	  printf("%s\n",nslGetErrStr(status));err=1;}
	status = nslSetState(&f->handle, NSL_STOP_ON_LNK_ERR, 1);
	if (status != NSL_SUCCESS){
	  printf("%s\n",nslGetErrStr(status));err=1;}
	status = nslSetState(&f->handle,NSL_EN_RECEIVE, 1);
	if (status != NSL_SUCCESS){
	  printf("%s\n",nslGetErrStr(status));err=1;}
	if(err==0)
	  figureClearReceiveBuffer(f);
      }
    }
    printf("done nsl\n");


#else
#ifdef NONSL
    f->sl240Opened=1;
#else
    status = fxsl_open((fx_ulong)f->fibrePort,(int*) &f->handle);
    if (status != FXSL_SUCCESS) {
      printf("Failed to open figuresensor SL240 port %d: %s\n\n",f->fibrePort,GetErrStr(status));
      err=1;
      /*status = nslOpen(f->fibrePort, &f->handle);
    if (status != NSL_SUCCESS) {
      printf("Failed to open figuresensor SL240 port %d: %s\n\n",f->fibrePort, nslGetErrStr(status));
      err=1;*/
    }else{
      f->sl240Opened=1;
      status = fxsl_get_config(f->handle, &f->cfg);
      if (status != FXSL_SUCCESS) {
	printf("Failed to read SL240 config\n");
	err=1;
      /*
      status = nslGetDeviceInfo(&f->handle, &f->info);
      if (status != NSL_SUCCESS) {
	printf("Failed to get SL240 device info: ");
	err=1;*/
      }else{
	status=sl240Setup(f->handle,f->cfg);
	if (status != FXSL_SUCCESS) {
	  printf("sl240Setup failed: %s\n", GetErrStr(status));
	  fxsl_close(f->handle);
	  err=1;
	}
	printf("TODO - do we need to clear the buffers in sl240 card?\n");
	/*printf("\n\nSL240 Device info:\n");
	printf("Unit no.\t %d\n", f->info.unitNum);
	printf("Board loc.\t %s\n", f->info.boardLocationStr);
	printf("Serial no.\t 0x%x.%x\n", f->info.serialNumH,f->info.serialNumL);
	printf("Firmware rev.\t 0x%x\n", f->info.revisionID);
	printf("Driver rev.\t %s\n", f->info.driverRevisionStr);
	printf("Fifo size\t %dM\n", f->info.popMemSize/0x100000);
	printf("Link speed\t %d MHz\n", f->info.linkSpeed);
	printf("No. links\t %d\n\n\n", f->info.numLinks);
	//set up card state.
	status = nslSetState(&f->handle, NSL_EN_EWRAP, 0);
	if (status != NSL_SUCCESS){
	  printf("%s\n",nslGetErrStr(status));err=1;}
	status = nslSetState(&f->handle,NSL_EN_RECEIVE, 1);
	if (status != NSL_SUCCESS){
	  printf("%s\n",nslGetErrStr(status));err=1;}
	status = nslSetState(&f->handle,NSL_EN_RETRANSMIT, 0);
	if (status != NSL_SUCCESS){
	  printf("%s\n",nslGetErrStr(status));err=1;}
	status = nslSetState(&f->handle,NSL_EN_CRC, 1);
	if (status != NSL_SUCCESS){
	  printf("%s\n",nslGetErrStr(status));err=1;}
	status = nslSetState(&f->handle,NSL_EN_FLOW_CTRL, 0);
	if (status != NSL_SUCCESS){
	  printf("%s\n",nslGetErrStr(status));err=1;}
	status = nslSetState(&f->handle,NSL_EN_LASER, 1);
	if (status != NSL_SUCCESS){
	  printf("%s\n",nslGetErrStr(status));err=1;}
	status = nslSetState(&f->handle,NSL_EN_BYTE_SWAP, 0);
	if (status != NSL_SUCCESS){
	  printf("%s\n",nslGetErrStr(status));err=1;}
	status = nslSetState(&f->handle,NSL_EN_WORD_SWAP, 0);
	if (status != NSL_SUCCESS){
	  printf("%s\n",nslGetErrStr(status));err=1;}
	status = nslSetState(&f->handle, NSL_STOP_ON_LNK_ERR, 1);
	if (status != NSL_SUCCESS){
	  printf("%s\n",nslGetErrStr(status));err=1;}
	status = nslSetState(&f->handle,NSL_EN_RECEIVE, 1);
	if (status != NSL_SUCCESS){
	  printf("%s\n",nslGetErrStr(status));err=1;}
	  figureClearReceiveBuffer(f);*/
      }
    }
#endif
#endif
    f->open=1;
  }
  printf("Initialised SL240, err=%d\n",err);
  if(err==0){
#ifndef NODM
    printf("Initialising DAC card\n");
    //initialise acquisition session
    if(InitSingleAO(f)){//failed...
      printf("Failed to initSingleAO\n");
      err=1;
    }else{
      // set configuration - _PdAOutReset is called inside _PdAOutSetCfg
      int aoCfg = 0;
      errorChk(_PdAOutSetCfg(f->mirhandle, aoCfg, 0));
      f->state = configured;
      //Start SW trigger
      errorChk(_PdAOutSwStartTrig(f->mirhandle));
      f->state = running;
    }
#else
    f->state=running;
#endif
  }
  printf("done nsl\n");
  if(err==0)
    err=figureNewParam(*figureHandle,pbuf,thisiter,arr);
  if(err==0 && pthread_create(&f->threadid,NULL,figureWorker,f)){
    printf("pthread_create figureWorker failed\n");
    err=1;
  }
  if(err){
    figureDofree(figureHandle);
  }
  return err;
}

/**
   Close a camera of type name.  Args are passed in the float array of size n, and state data is in camHandle, which should be freed and set to NULL before returning.
*/
int figureClose(void **figureHandle){
  figureStruct *f;
  printf("Closing figure\n");
  if(*figureHandle!=NULL){
    f=(figureStruct*)*figureHandle;
    f->open=0;
    if(f->paramNames!=NULL)
      free(f->paramNames);
    //Wait for the thread to finish.
    printf("waiting for join\n");
    pthread_join(f->threadid,NULL);
    printf("joined\n");
  }
  figureDofree(figureHandle);
  *figureHandle=NULL;
  printf("figure closed\n");
  return 0;
}
/**
New parameters ready - use if you need to...
*/
int figureNewParam(void *figureHandle,paramBuf *pbuf,unsigned int frameno,arrayStruct *arr){
  figureStruct *f=(figureStruct *)figureHandle;
  unsigned short *actInit;
  int initLen;
  int *actMapping;
  int *actSource;
  float *actScale,*actOffset;
  float multiplier;
  float adder;
  int actMappingLen=0,actSourceLen=0,actScaleLen=0,actOffsetLen=0;
  int *index=f->index;
  void **values=f->values;
  char *dtype=f->dtype;
  int *nbytes=f->nbytes;
  int err=0;
  if(figureHandle!=NULL){
    f=(figureStruct*)figureHandle;
    actMapping=NULL;
    actSource=NULL;
    actScale=NULL;
    actOffset=NULL;
    actInit=NULL;
    multiplier=1.;
    adder=0;
    initLen=0;
    bufferGetIndex(pbuf,NBUFFERVARIABLES,f->paramNames,index,values,dtype,nbytes);
    if(index[ACTMAPPING]>=0){
      if(nbytes[ACTMAPPING]==0){
	//nowt
      }else if(nbytes[ACTMAPPING]%sizeof(int)==0 && dtype[ACTMAPPING]=='i'){
	actMapping=(int*)values[ACTMAPPING];
	actMappingLen=nbytes[ACTMAPPING]/sizeof(int);
      }else{
	printf("Warning - bad actuator mapping (wrong size or type\n");
	err=1;
      }
    }else
      printf("actMapping for figure sensor library not found - continuing with linear mapping\n");
    
    if(index[ACTINIT]>=0){
      if(nbytes[ACTINIT]>0){
	if(dtype[ACTINIT]=='H' && nbytes[ACTINIT]%sizeof(unsigned short)==0){
	  actInit=(unsigned short*)values[ACTINIT];
	  initLen=nbytes[ACTINIT]/sizeof(unsigned short);
	}else{
	  printf("Warning - bad actInit values (wrong size or type)\n");
	  err=1;
	}
      }
    }else
      printf("actInit for figure sensor library not found - continuing\n");
    
    if(index[ACTSOURCE]>=0){
      if(nbytes[ACTSOURCE]==0){
      }else if(nbytes[ACTSOURCE]%sizeof(int)==0 && dtype[ACTSOURCE]=='i'){
	actSource=(int*)values[ACTSOURCE];
	actSourceLen=nbytes[ACTSOURCE]/sizeof(int);
      }else{
	printf("Warning - bad figureActSource (wrong size or type)\n");
	err=1;
      }
    }else
      printf("figureActSource for figure sensor library not found - continuing\n");
    
    if(index[ACTSCALE]>=0){
      if(nbytes[ACTSCALE]==0){
      }else if(nbytes[ACTSCALE]%sizeof(float)==0 && dtype[ACTSCALE]=='f'){
	actScale=(float*)values[ACTSCALE];
	actScaleLen=nbytes[ACTSCALE]/sizeof(float);
      }else{
	printf("Warning - bad figureActScale (wrong size or type)\n");
	err=1;
      }
    }else
      printf("figureActScale for figure sensor library not found - continuing\n");
    
    if(index[ACTOFFSET]>=0){
      if(nbytes[ACTOFFSET]==0){
      }else if(nbytes[ACTOFFSET]%sizeof(float)==0 && dtype[ACTOFFSET]=='f'){
	actOffset=(float*)values[ACTOFFSET];
	actOffsetLen=nbytes[ACTOFFSET]/sizeof(float);
      }else{
	printf("Warning - bad figureActOffset (wrong size or type)\n");
	err=1;
      }
    }else
      printf("figureActOffset for figure sensor library not found - continuing\n");
    
    if(index[FIGUREDEBUG]>=0){
      if(nbytes[FIGUREDEBUG]==sizeof(int) && dtype[FIGUREDEBUG]=='i'){
	f->debug=*((int*)values[FIGUREDEBUG]);
      }else{
	printf("Warning - figureDebug bad\n");
      }
    }else
      printf("figureDebug for figure sensor library not found - continuing\n");
    
    if(index[FIGUREMULTIPLIER]>=0){
      if(nbytes[FIGUREMULTIPLIER]==sizeof(float) && dtype[FIGUREMULTIPLIER]=='f'){
	multiplier=*((float*)values[FIGUREMULTIPLIER]);
      }else{
	printf("Warning - figureMultiplier bad\n");
      }
    }else
      printf("figureMultiplier not found - continuing\n");
    if(index[FIGUREADDER]>=0){
      if(nbytes[FIGUREADDER]==sizeof(float) && dtype[FIGUREADDER]=='f'){
	adder=*((float*)values[FIGUREADDER]);
      }else{
	printf("Warning - figureAdder bad\n");
      }
    }else{
      printf("figureAdder not found - continuing\n");
    }
    if(actSourceLen!=actMappingLen && actSourceLen!=0){
      printf("figureActSource wrong size\n");
      actSource=NULL;
      err=1;
    }
    if(actScaleLen!=actMappingLen && actScaleLen!=0){
      printf("figureActScale wrong size\n");
      actScale=NULL;
      err=1;
    }
    if(actOffsetLen!=actMappingLen && actOffsetLen!=0){
      printf("figureActOffset wrong size\n");
      actOffset=NULL;
      err=1;
    }
    if(actSource==NULL && actMappingLen!=f->nacts){
      printf("warning: actSource not defined, and actMapping not equal to nacts - ignoring actMapping\n");
      actMappingLen=0;
      actMapping=NULL;
    }
    
    pthread_mutex_lock(&f->mInternal);
    f->actMapping=actMapping;
    f->actMappingLen=actMappingLen;
    f->actInit=actInit;
    f->initLen=initLen;
    f->actSource=actSource;
    f->actScale=actScale;
    f->actOffset=actOffset;
    f->adder=adder;
    f->multiplier=multiplier;
    pthread_mutex_unlock(&f->mInternal);
  }
  
  return err;
}

