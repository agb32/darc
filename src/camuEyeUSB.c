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
   The code here is used to create a shared object library, which can then be swapped around depending on which cameras you have in use, ie you simple rename the camera file you want to camera.so (or better, change the soft link), and restart the coremain.

The library is written for a specific camera configuration - ie in multiple camera situations, the library is written to handle multiple cameras, not a single camera many times.
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "rtccamera.h"
#include <time.h>
#include <unistd.h>
#include <string.h>
#include <pthread.h>
#define __LINUX__ 1
#include "ueye.h" //was uEye.h
#ifdef UEYE_VERSION_CODE
   #if UEYE_VERSION_CODE>UEYE_VERSION(4,0,1)
      #include "ueye_deprecated.h"
      #warning "camuEye.c:: including deprecated function via header"
   #endif
#endif
#include "darc.h"
typedef enum{
  UEYEACTUALEXP,
  UEYEBLACKLEVEL,
  UEYEBOOSTGAIN,
  UEYEEXPTIME,
  UEYEFRAMERATE,
  UEYEGAIN,
  UEYEGRABMODE,
  UEYENFRAMES,
  UEYEPIXELCLOCK,
  //Add more before this line.
  CAMNBUFFERVARIABLES//equal to number of entries in the enum
}CAMBUFFERVARIABLEINDX;

#define camMakeNames() bufferMakeNames(CAMNBUFFERVARIABLES,"uEyeActualExp","uEyeBlackLevel","uEyeBoostGain","uEyeExpTime","uEyeFrameRate","uEyeGain","uEyeGrabMode","uEyeNFrames","uEyePixelClock")


#define nBuffers 8
typedef struct{
  int frameno;
  float *imgdata;
  int npxls;
  unsigned int *userFrameNo;
  int ncam;
  int captureStarted;
  int camOpen;
  HIDS hCam;
  char *imageMem[nBuffers];
  INT pid[nBuffers];
  char *paramNames;
  float frameRate;
  float expTime;
  circBuf *rtcErrorBuf;
  int index[CAMNBUFFERVARIABLES];
  void *values[CAMNBUFFERVARIABLES];
  char dtype[CAMNBUFFERVARIABLES];
  int nbytes[CAMNBUFFERVARIABLES];
  unsigned char *lastImgMem;
  int nFrames;
  int grabMode;
  int gain;
  int pxlClock;
  int black;
  int binning;
}CamStruct;



void camdoFree(CamStruct *camstr){
  int hCam=camstr->hCam;
  int i;
  if(camstr!=NULL){
    if(camstr->paramNames!=NULL)
      free(camstr->paramNames);
    if(camstr->captureStarted){
      is_StopLiveVideo(hCam,IS_WAIT);
      is_ClearSequence(hCam);
    }
    for(i=0;i<nBuffers;i++){
      if(camstr->imageMem[i]!=NULL)
	is_FreeImageMem(hCam,camstr->imageMem[i],camstr->pid[i]);
    }
    if(camstr->camOpen){
      is_DisableEvent(hCam,IS_SET_EVENT_FRAME);
      is_ExitEvent(hCam,IS_SET_EVENT_FRAME);
      is_ExitCamera(hCam);
    }
    free(camstr);
  }
}


int camNewParam(void *camHandle,paramBuf *pbuf,unsigned int frameno,arrayStruct *arr){
  //the only param needed is camReorder if reorder!=0.
  int i;
  CamStruct *camstr=(CamStruct*)camHandle;
  int nfound,err=0;
  INT nRet;
  double actualFrameRate;
  double actualExpTime;
  int prevGrabMode;
  float *actualExp=NULL;
  nfound=bufferGetIndex(pbuf,CAMNBUFFERVARIABLES,camstr->paramNames,camstr->index,camstr->values,camstr->dtype,camstr->nbytes);
  i=UEYEPIXELCLOCK;
  if(camstr->index[i]>=0){//has been found...
    if(camstr->dtype[i]=='i' && camstr->nbytes[i]==sizeof(int)){
      camstr->pxlClock=*((int*)camstr->values[i]);//in MHz: recommends that frame rate and exposure time are also set after this.
      if((nRet=is_SetPixelClock(camstr->hCam,camstr->pxlClock))!=IS_SUCCESS)
	printf("is_SetPixelClock failed\n");
    }else{
      printf("uEyePixelClock error\n");
      writeErrorVA(camstr->rtcErrorBuf,-1,frameno,"uEyePixelClock error");
      err=1;
    }
  }else{
    printf("uEyePixelClock not found - ignoring\n");
  }
  i=UEYEACTUALEXP;
  if(camstr->index[i]>=0){
    if(camstr->nbytes[i]==4 && camstr->dtype[i]=='f'){
      actualExp=(float*)camstr->values[i];
    }else{
      printf("Not returning actual exposure time - wrong datatype\n");
      actualExp=NULL;
    }
  }else{
    actualExp=NULL;
    printf("uEyeActualExp not found - ignoring\n");
  }

  i=UEYEFRAMERATE;
  if(camstr->index[i]>=0){//has been found...
    if(camstr->dtype[i]=='f' && camstr->nbytes[i]==4){
      camstr->frameRate=*((float*)camstr->values[i]);
      if((nRet=is_SetFrameRate(camstr->hCam,(double)camstr->frameRate,&actualFrameRate))!=IS_SUCCESS)
	printf("is_SetFrameRate failed\n");
      else
	printf("Framerate set to %g\n",actualFrameRate);
    }else{
      printf("uEyeFrameRate error\n");
      writeErrorVA(camstr->rtcErrorBuf,-1,frameno,"uEyeFrameRate error");
      err=1;
    }
  }else{
    printf("uEyeFrameRate not found - ignoring\n");
  }
  i=UEYEEXPTIME;//If ==0, then exp time is 1/frame rate.
  if(camstr->index[i]>=0){//has been found...
    if(camstr->dtype[i]=='f' && camstr->nbytes[i]==4){
      camstr->expTime=*((float*)camstr->values[i]);
      if((nRet=is_SetExposureTime(camstr->hCam,(double)camstr->expTime,&actualExpTime))!=IS_SUCCESS)
	printf("is_SetExposureTime failed\n");
      else{
	printf("Exposure time set to %gms (requested %gms)\n",actualExpTime,camstr->expTime);
	if(actualExp!=NULL){
	  *actualExp=(float)actualExpTime;
	}
      }
    }else{
      printf("uEyeExpTime error\n");
      writeErrorVA(camstr->rtcErrorBuf,-1,frameno,"uEyeExpTime error");
      err=1;
    }
  }else{
    printf("uEyeExpTime not found - ignoring\n");
  }
  i=UEYENFRAMES;
  if(camstr->index[i]>=0){//has been found...
    if(camstr->dtype[i]=='i' && camstr->nbytes[i]==sizeof(int)){
      camstr->nFrames=*((int*)camstr->values[i]);
    }else{
      printf("uEyeNFrames error\n");
      writeErrorVA(camstr->rtcErrorBuf,-1,frameno,"uEyeNFrames error");
      err=1;
      camstr->nFrames=1;
    }
  }else{
    printf("uEyeNFrames not found - ignoring\n");
    camstr->nFrames=1;
  }
  i=UEYEBOOSTGAIN;
  if(camstr->index[i]>=0){//has been found
    if(camstr->dtype[i]=='i' && camstr->nbytes[i]==sizeof(int)){
      if(*((int*)camstr->values[i])){
	if(is_SetGainBoost(camstr->hCam,IS_SET_GAINBOOST_ON)!=IS_SUCCESS)
	  printf("SetGainBoost(on) failed - maybe not available\n");
      }else{
	if(is_SetGainBoost(camstr->hCam,IS_SET_GAINBOOST_OFF)!=IS_SUCCESS)
	  printf("SetGainBoost(off) failed - maybe not available\n");
      }
    }else{
      printf("uEyeBoostGain error\n");
      writeErrorVA(camstr->rtcErrorBuf,-1,frameno,"uEyeBoostGain error");
      err=1;
    }
  }else{
    printf("uEyeBoostGain not found - ignoring\n");
  }
  i=UEYEGAIN;
  if(camstr->index[i]>=0){//has been found
    if(camstr->dtype[i]=='i' && camstr->nbytes[i]==sizeof(int)){
      camstr->gain=*((int*)camstr->values[i]);
      if(camstr->gain>100){
	printf("Setting gain to max 100\n");
	camstr->gain=100;
      }else if(camstr->gain<0){
	printf("Setting gain to min 0\n");
	camstr->gain=0;
      }
      is_SetHardwareGain(camstr->hCam,camstr->gain,IS_IGNORE_PARAMETER,IS_IGNORE_PARAMETER,IS_IGNORE_PARAMETER);
    }else{
      printf("uEyeGain error\n");
      writeErrorVA(camstr->rtcErrorBuf,-1,frameno,"uEyeGain error");
      err=1;
    }
  }else{
    printf("uEyeGain not found - ignoring\n");
  }
  i=UEYEBLACKLEVEL;
  if(camstr->index[i]>=0){//has been found
    if(camstr->dtype[i]=='i' && camstr->nbytes[i]==sizeof(int)){
      camstr->black=(*((int*)camstr->values[i]));
      if(camstr->black<0){
	if(is_SetBlCompensation(camstr->hCam,IS_BL_COMPENSATION_DISABLE,abs(camstr->black),0)!=IS_SUCCESS)
	  printf("SetBlCompensation failed\n");
      }else{
	if(is_SetBlCompensation(camstr->hCam,IS_BL_COMPENSATION_ENABLE,camstr->black,0)!=IS_SUCCESS)
	  printf("SetBlCompensation failed\n");
      }
    }else{
      printf("uEyeBlackLevel error\n");
      writeErrorVA(camstr->rtcErrorBuf,-1,frameno,"uEyeBlackLevel error");
      err=1;
    }
  }else{
    printf("uEyeBlackLevel not found - ignoring\n");
  }

  prevGrabMode=camstr->grabMode;
  i=UEYEGRABMODE;
  if(camstr->index[i]>=0){//has been found...
    if(camstr->dtype[i]=='i' && camstr->nbytes[i]==sizeof(int)){
      camstr->grabMode=*((int*)camstr->values[i]);
    }else{
      printf("uEyeGrabMode error\n");
      writeErrorVA(camstr->rtcErrorBuf,-1,frameno,"uEyeGrabMode error");
      err=1;
      camstr->grabMode=0;
    }
  }else{
    printf("uEyeGrabMode not found - ignoring\n");
    camstr->grabMode=0;
  }
  if(camstr->grabMode!=prevGrabMode){
    //change the operation mode
    if(camstr->grabMode){
      if(camstr->captureStarted){
	camstr->captureStarted=0;
	is_StopLiveVideo(camstr->hCam,IS_WAIT);
      }
    }else{
      //turn on framing.
      is_CaptureVideo(camstr->hCam,IS_WAIT);//set to live mode...
      camstr->captureStarted=1;

    }
  }
  return err;
}


/**
   Open a camera of type name.  Args are passed in a int32 array of size n, which can be cast if necessary.  Any state data is returned in camHandle, which should be NULL if an error arises.
   pxlbuf is the array that should hold the data. The library is free to use the user provided version, or use its own version as necessary (ie a pointer to physical memory or whatever).  It is of size npxls*sizeof(short).
   ncam is number of cameras, which is the length of arrays pxlx and pxly, which contain the dimensions for each camera.
   Name is used if a library can support more than one camera.

   This opens a uEye USB camera.
   args here contains filename
*/

int camOpen(char *name,int n,int *args,paramBuf *pbuf,circBuf *rtcErrorBuf,char *prefix,arrayStruct *arr,void **camHandle,int nthreads,unsigned int thisiter,unsigned int **frameno,int *framenoSize,int npxls,int ncam,int *pxlx,int* pxly){
  CamStruct *camstr;
  int i;
  unsigned short *tmps;
  HIDS hCam=0;
  SENSORINFO camInfo;
  INT xpos,ypos,width,height,bitsPerPxl=8,nRet;
  double expMax,actualFrameRate;
  int binning=1;
  //INT cerr;
  //char *errtxt;
  //unsigned short *pxlbuf=arr->pxlbufs;
  printf("Initialising camera %s\n",name);
  if((*camHandle=malloc(sizeof(CamStruct)))==NULL){
    printf("Couldn't malloc camera handle\n");
    return 1;
  }
  memset(*camHandle,0,sizeof(CamStruct));
  camstr=(CamStruct*)*camHandle;
  camstr->paramNames=camMakeNames();
  camstr->rtcErrorBuf=rtcErrorBuf;
  if(n>=5){
    xpos=args[0];
    ypos=args[1];
    width=args[2];
    height=args[3];
    camstr->frameRate=(double)(args[4]);
    if(n>5)
      binning=args[5];
  }else{
    printf("Error - args should be >=5 in camuEyeUSB: xpos,ypos,width,height,frameRate,(optional)binning\n");
    camdoFree(camstr);
    *camHandle=NULL;
    return 1;
  }


  if(arr->pxlbuftype!='f' || arr->pxlbufsSize!=sizeof(float)*npxls){
    //need to resize the pxlbufs...
    arr->pxlbufsSize=sizeof(float)*npxls;
    arr->pxlbuftype='f';
    arr->pxlbufelsize=sizeof(float);
    tmps=realloc(arr->pxlbufs,arr->pxlbufsSize);
    if(tmps==NULL){
      if(arr->pxlbufs!=NULL)
	free(arr->pxlbufs);
      printf("pxlbuf malloc error in camfile.\n");
      arr->pxlbufsSize=0;
      free(*camHandle);
      *camHandle=NULL;
      return 1;
    }
    arr->pxlbufs=tmps;
    memset(arr->pxlbufs,0,arr->pxlbufsSize);
  }
  if(ncam!=1){
    printf("Sorry - only 1 camera currently allowed\n");
    free(*camHandle);
    *camHandle=NULL;
    return 1;
  }
  camstr->imgdata=arr->pxlbufs;
  if(*framenoSize<ncam){
    if(*frameno!=NULL)free(*frameno);
    *frameno=malloc(sizeof(int)*ncam);
    if(*frameno==NULL){
      *framenoSize=0;
      printf("Unable to malloc camframeno\n");
    }else{
      *framenoSize=ncam;
    }
  }
  camstr->userFrameNo=*frameno;
  camstr->npxls=npxls;//*pxlx * *pxly;
  camstr->ncam=ncam;
  camstr->frameno=-1;
  for(i=0; i<ncam; i++){
    camstr->userFrameNo[i]=-1;
  }

  if((nRet=is_InitCamera(&hCam,NULL))!=IS_SUCCESS){
    printf("Failed to open camera: %d (IS_NO_SUCCESS==%d)\n",nRet,IS_NO_SUCCESS);
    //is_GetError(hCam,&cerr,&errtxt);
    //printf("Error %d was %s\n",cerr,errtxt);
    if(nRet==IS_STARTER_FW_UPLOAD_NEEDED){
      INT nTime;
      is_GetDuration(hCam,IS_SE_STARTER_FW_UPLOAD,&nTime);
      printf("Uploading firmware - will take %ds\n",nTime);
      printf("Actually - not doing this because it doesn't compile\n");
      //nRet=is_InitCamera((&hCam)|IS_ALLOW_STARTER_FW_UPLOAD,NULL);
    }else{
      camdoFree(camstr);
      *camHandle=NULL;
      return 1;
    }
  }
  camstr->hCam=hCam;
  camstr->camOpen=1;
  if((nRet=is_GetSensorInfo(hCam,&camInfo))!=IS_SUCCESS){
    printf("failed to get camera info\n");
  }
  printf("Opened camera %32s, size %dx%d\n",camInfo.strSensorName,camInfo.nMaxWidth,camInfo.nMaxHeight);
  if((nRet=is_SetColorMode(hCam,IS_CM_MONO8))!=IS_SUCCESS){
    printf("setColorMode failed\n");
  }
  if((nRet=is_HotPixel(hCam,IS_HOTPIXEL_DISABLE_CORRECTION,NULL,0))!=IS_SUCCESS)
    printf("is_HotPixel(disable) failed\n");





  if(binning!=1){
    int mode=IS_BINNING_DISABLE;
    switch(binning){
    case 2:
      mode=IS_BINNING_2X_VERTICAL;
      break;
    case 3:
      mode=IS_BINNING_3X_VERTICAL;
      break;
    case 4:
      mode=IS_BINNING_4X_VERTICAL;
      break;
    default:
      mode=IS_BINNING_DISABLE;
      break;
    }
    if(is_SetBinning(camstr->hCam,mode)!=IS_SUCCESS){
      printf("is_SetBinning failed\n");
    }
  }


  if((nRet=is_SetAOI(hCam,IS_SET_IMAGE_AOI,&xpos,&ypos,&width,&height))!=IS_SUCCESS)
    printf("is_SetAOI failed\n");
  //if((nRet=is_SetExposureTime(hCam,IS_SET_ENABLE_AUTO_SHUTTER,&expMax))!=IS_SUCCESS)
  //  printf("is_SetExposureTime failed\n");
  if((nRet=is_SetFrameRate(hCam,(double)camstr->frameRate,&actualFrameRate))!=IS_SUCCESS)
    printf("is_SetFrameRate failed\n");
  if((nRet=is_SetExposureTime(hCam,0.,&expMax))!=IS_SUCCESS)
    printf("is_SetExposureTime failed\n");
  printf("Exposure time set to %gms with frame rate %g Hz\n",expMax,actualFrameRate);
  if (camInfo.bGlobShutter == TRUE)
    is_SetGlobalShutter(hCam, IS_SET_GLOBAL_SHUTTER_ON);
  // Alloc some memory for image buffer
  for(i=0;i<nBuffers;i++){
    if((nRet=is_AllocImageMem(hCam, (INT)width, (INT)height, bitsPerPxl,&(camstr->imageMem[i]), &(camstr->pid[i])))!=IS_SUCCESS){
      printf("Failed to allocate camera memory - closing camera\n");
      camdoFree(camstr);
      *camHandle=NULL;
      return 1;
    }
    if((nRet=is_AddToSequence(hCam,camstr->imageMem[i],camstr->pid[i]))!=IS_SUCCESS){
      printf("Failed is_AddToSequence\n");
      camdoFree(camstr);
      *camHandle=NULL;
      return 1;
    }
  }
  if((nRet=is_SetExternalTrigger(hCam, IS_SET_TRIGGER_OFF))!=IS_SUCCESS)//IS_SET_TRIGGER_SOFTWARE as previously used - but is slower
    printf("is_SetExternalTrigger failed\n");
  nRet=is_SetDisplayMode(hCam, IS_GET_DISPLAY_MODE);
  if(!(nRet & IS_SET_DM_DIB)){
    printf("is_SetDisplayMode failed\n");
    camdoFree(camstr);
    *camHandle=NULL;
    return 1;
  }
  //Now install handling... do we want an event or a message handler?  An event, because messages are windoze only.
  
  if((nRet=is_InitEvent(hCam,NULL,IS_SET_EVENT_FRAME))!=IS_SUCCESS)
    printf("is_InitEvent failed\n");
  if((nRet=is_EnableEvent(hCam,IS_SET_EVENT_FRAME))!=IS_SUCCESS)
    printf("is_EnableEvent failed\n");
  is_CaptureVideo(camstr->hCam,IS_WAIT);//set to live mode...
  camstr->captureStarted=1;

  if(camNewParam(*camHandle,pbuf,thisiter,arr)!=0){
    printf("Error in camOpen->newParam...\n");
    camdoFree(camstr);
    *camHandle=NULL;
    return 1;
  }



  //if((nRet=is_WaitEvent(hCam,IS_SET_EVENT_FRAME,1000))!=IS_SUCCESS)
  //  printf("is_WaitEvent failed\n");
  //is_WaitForNextImage(hCam,1000,&imgptr,&imgid);

  //is_FreezeVideo(hCam,IS_DONT_WAIT);
  return 0;
}


/**
   Close a camera of type name.  Args are passed in the int32 array of size n, and state data is in camHandle, which should be freed and set to NULL before returning.
*/
int camClose(void **camHandle){
  CamStruct *camstr;
  printf("Closing camera\n");
  if(*camHandle==NULL)
    return 1;
  camstr=(CamStruct*)*camHandle;
  camdoFree(camstr);
  *camHandle=NULL;
  printf("Camera closed\n");
  return 0;
}


/**
   Called when we're starting processing the next frame.
*/
int camNewFrameSync(void *camHandle,unsigned int thisiter,double starttime){
  //printf("camNewFrame\n");
  CamStruct *camstr;
  int i,j,n;
  unsigned char *imgMem=NULL;
  //INT pitch;
  camstr=(CamStruct*)camHandle;
  if(camHandle==NULL){// || camstr->streaming==0){
    //printf("called camNewFrame with camHandle==NULL\n");
    return 1;
  }
  memset(camstr->imgdata,0,sizeof(float)*camstr->npxls);
  if(camstr->nFrames<1)
    camstr->nFrames=1;
  for(n=0;n<camstr->nFrames;n++){//sum this many frames...
    if(camstr->grabMode){
      is_FreezeVideo(camstr->hCam,IS_WAIT);
      is_GetImageMem(camstr->hCam,(void**)&imgMem);
      i=10;
      while(i>0 && imgMem==camstr->lastImgMem){//wait for new data
	is_FreezeVideo(camstr->hCam,IS_WAIT);
	is_GetImageMem(camstr->hCam,(void**)&imgMem);
	i--;
      }
      if(imgMem==camstr->lastImgMem)
	printf("Duplicate image retrieved at %p\n",imgMem);
      camstr->lastImgMem=imgMem;
    }else{
      is_GetImageMem(camstr->hCam,(void**)&imgMem);
      i=10;
      while(i>0 && imgMem==camstr->lastImgMem){//wait for new data
	is_WaitEvent(camstr->hCam,IS_SET_EVENT_FRAME,1000);
	is_GetImageMem(camstr->hCam,(void**)&imgMem);
	i--;
      }
      //is_GetImageMemPitch(camstr->hCam,&pitch);
      if(imgMem==camstr->lastImgMem)
	printf("Duplicate image retrieved at %p\n",imgMem);
      camstr->lastImgMem=imgMem;
    }
    for(j=0;j<camstr->npxls;j++){//byte to float conversion.
      camstr->imgdata[j]+=(float)imgMem[j];
    }
    //memcpy(camstr->imgdata,imgMem,sizeof(char)*camstr->npxls);
  }
  if(camstr->nFrames>1){
    for(i=0;i<camstr->npxls;i++)
      camstr->imgdata[i]/=camstr->nFrames;
  }
  camstr->frameno++;
  for(i=0; i<camstr->ncam; i++){
    camstr->userFrameNo[i]++;//=camstr->frameno;
  }
  return 0;
}
