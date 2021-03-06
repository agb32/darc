#darc, the Durham Adaptive optics Real-time Controller.
#Copyright (C) 2010 Alastair Basden.

#This program is free software: you can redistribute it and/or modify
#it under the terms of the GNU Affero General Public License as
#published by the Free Software Foundation, either version 3 of the
#License, or (at your option) any later version.

#This program is distributed in the hope that it will be useful,
#but WITHOUT ANY WARRANTY; without even the implied warranty of
#MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
#GNU Affero General Public License for more details.

#You should have received a copy of the GNU Affero General Public License
#along with this program.  If not, see <http://www.gnu.org/licenses/>.
#This is a configuration file for SPHERE.
#Aim to fill up the control dictionary with values to be used in the RTCS.

#Best for sphere: nsub=40, 6 threads, prio=39,40,40,40... affin=1<<arange(nthreads+1)

#import correlation
import FITS
import tel
import numpy
NCAMERAS=1#1, 2, 3, 4.  This is the number of physical cameras
ncam=NCAMERAS#(int(NCAMERAS)+1)/2

ncamThreads=numpy.ones((ncam,),numpy.int32)*4
noPrePostThread=0
threadPriority=numpy.ones((ncamThreads.sum()+1,),numpy.int32)*40
#threadPriority=numpy.arange(ncamThreads.sum()+1)+40
threadPriority[0]=39
threadAffinity=1<<(numpy.arange(1+ncamThreads.sum()))



nsub=40#should be 40 for sphere...
npxl=6
nact=nsub+1
nacts=tel.Pupil(nact,nact/2.,2).fn.astype("i").sum()
camPerGrab=numpy.ones((ncam,),"i")
#if NCAMERAS%2==1:
#    camPerGrab[-1]=1
npxly=numpy.zeros((ncam,),numpy.int32)
npxly[:]=nsub*npxl
npxlx=npxly.copy()
nsuby=npxly.copy()
nsuby[:]=nsub
nsubx=nsuby.copy()
nsubaps=(nsuby*nsubx).sum()
individualSubapFlag=tel.Pupil(nsub,nsub/2.,2.7,nsub).subflag.astype("i")#gives 1240 used subaps
subapFlag=numpy.zeros(((nsuby*nsubx).sum(),),"i")
for i in range(ncam):
    tmp=subapFlag[(nsuby[:i]*nsubx[:i]).sum():(nsuby[:i+1]*nsubx[:i+1]).sum()]
    tmp.shape=nsuby[i],nsubx[i]
    tmp[:]=individualSubapFlag
#ncents=nsubaps*2
ncents=subapFlag.sum()*2
npxls=(npxly*npxlx).sum()

fakeCCDImage=None#(numpy.random.random((npxls,))*20).astype("i")
#camimg=(numpy.random.random((10,npxls))*20).astype(numpy.int16)

bgImage=numpy.ones((npxls,),"f")#None#FITS.Read("shimgb1stripped_bg.fits")[1].astype("f")#numpy.zeros((npxls,),"f")
darkNoise=None#FITS.Read("shimgb1stripped_dm.fits")[1].astype("f")
flatField=None#FITS.Read("shimgb1stripped_ff.fits")[1].astype("f")
#indx=0
#nx=npxlx/nsubx
#ny=npxly/nsuby
#correlationPSF=numpy.zeros((npxls,),numpy.float32)


subapLocation=numpy.zeros((nsubaps,6),"i")
nsubapsCum=numpy.zeros((ncam+1,),numpy.int32)
ncentsCum=numpy.zeros((ncam+1,),numpy.int32)
for i in range(ncam):
    nsubapsCum[i+1]=nsubapsCum[i]+nsuby[i]*nsubx[i]
    ncentsCum[i+1]=ncentsCum[i]+subapFlag[nsubapsCum[i]:nsubapsCum[i+1]].sum()*2

# now set up a default subap location array...
#this defines the location of the subapertures.
subx=[npxl]*ncam#(npxlx-16*camPerGrab)/nsubx
suby=[npxl]*ncam#(npxly-16)/nsuby
xoff=[0]*ncam
yoff=[0]*ncam
for k in range(ncam):
    nc=camPerGrab[k]
    for i in range(nsuby[k]):
        for j in range(nsubx[k]):
            indx=nsubapsCum[k]+i*nsubx[k]+j
            if subapFlag[indx]:
                subapLocation[indx]=(yoff[k]+i*suby[k],yoff[k]+i*suby[k]+suby[k],1,xoff[k]*nc+j/nc*subx[k]*nc+j%nc,xoff[k]*nc+j/nc*subx[k]*nc+subx[k]*nc+j%nc,nc)

pxlCnt=numpy.zeros((nsubaps,),"i")
# set up the pxlCnt array - number of pixels to wait until each subap is ready.  Here assume identical for each camera.
for k in range(ncam):
    # tot=0#reset for each camera
    for i in range(nsuby[k]):
        for j in range(nsubx[k]):
            indx=nsubapsCum[k]+i*nsubx[k]+j
            n=(subapLocation[indx,1]-1)*npxlx[k]+subapLocation[indx,4]
            pxlCnt[indx]=n
#pxlCnt[:]=numpy.max(pxlCnt)#make the cuda MVM do all at once.
nlots=4
if nlots>ncamThreads.sum():
    raise Exception("Too many subapAllocations... - increase number of threads")
subapAllocation=numpy.zeros((nsubaps,),numpy.int32)
for i in range(nlots):#do in multiple lots (nlots cuda MVMs)
    pxlCnt[(i*nsubaps)/nlots:((i+1)*nsubaps)/nlots]=((i+1)*npxls)/nlots
    subapAllocation[(i*nsubaps)/nlots:((i+1)*nsubaps)/nlots]=i%ncamThreads[0]

#subapAllocation=None


#The params are dependent on the interface library used.
cameraParams=numpy.zeros((6*ncam+2,),numpy.int32)
cameraParams[0::6]=128*8#blocksize
cameraParams[1::6]=1000#timeout/ms
cameraParams[2::6]=range(ncam)#port
cameraParams[3::6]=0xffff#thread affinity
cameraParams[4::6]=1#thread priority
cameraParams[5::6]=0#reorder
cameraParams[-2]=0#resync
cameraParams[-1]=1#wpu correction
rmx=numpy.zeros((nacts,ncents)).astype("f")#FITS.Read("rmxRTC.fits")[1].transpose().astype("f")

mirrorParams=numpy.zeros((4,),"i")
mirrorParams[0]=1000#timeout/ms
mirrorParams[1]=2#port
mirrorParams[2]=-1#thread affinity
mirrorParams[3]=1#thread prioirty

#Now describe the DM - this is for the GUI only, not the RTC.
#The format is: ndms, N for each DM, actuator numbers...
#Where ndms is the number of DMs, N is the number of linear actuators for each, and the actuator numbers are then an array of size NxN with entries -1 for unused actuators, or the actuator number that will set this actuator in the DMC array.

dmDescription=numpy.zeros((17*17+1+1,),numpy.int16)
dmDescription[0]=1#1 DM
dmDescription[1]=17#1st DM has 2 linear actuators
tmp=dmDescription[2:]
tmp[:]=-1
tmp.shape=17,17
dmflag=tel.Pupil(17,17/2.,1).fn.ravel()
numpy.put(tmp,dmflag.nonzero()[0],numpy.arange(nacts))



control={
    "switchRequested":0,#this is the only item in a currently active buffer that can be changed...
    "pause":0,
    "go":1,
    "maxClipped":nacts,
    "refCentroids":None,
    "centroidMode":"CoG",#whether data is from cameras or from WPU.
    "windowMode":"basic",
    "thresholdAlgo":1,
    "reconstructMode":"simple",#simple (matrix vector only), truth or open
    "centroidWeight":None,
    "v0":numpy.ones((nacts,),"f")*32768,#v0 from the tomograhpcic algorithm in openloop (see spec)
    "bleedGain":0.0,#0.05,#a gain for the piston bleed...
    "actMax":numpy.ones((nacts,),numpy.uint16)*65535,#4095,#max actuator value
    "actMin":numpy.zeros((nacts,),numpy.uint16),#4095,#max actuator value
    "nacts":nacts,
    "ncam":ncam,
    "nsub":nsuby*nsubx,
    #"nsubx":nsubx,
    "npxly":npxly,
    "npxlx":npxlx,
    "ncamThreads":ncamThreads,
    "pxlCnt":pxlCnt,
    "subapLocation":subapLocation,
    "bgImage":bgImage,
    "darkNoise":darkNoise,
    "closeLoop":1,
    "flatField":flatField,#numpy.random.random((npxls,)).astype("f"),
    "thresholdValue":0.,#could also be an array.
    "powerFactor":1.,#raise pixel values to this power.
    "subapFlag":subapFlag,
    "fakeCCDImage":fakeCCDImage,
    "printTime":0,#whether to print time/Hz
    "rmx":rmx,#numpy.random.random((nacts,ncents)).astype("f"),
    "gain":numpy.ones((nacts,),"f"),
    "E":numpy.zeros((nacts,nacts),"f"),#E from the tomoalgo in openloop.
    "threadAffinity":threadAffinity,
    "threadPriority":threadPriority,
    "delay":0,
    "clearErrors":0,
    "camerasOpen":0,
    "camerasFraming":0,
    "cameraName":"libsl240Int32cam.so",#"camfile",
    "cameraParams":cameraParams,
    "mirrorName":"libmirrorSL240.so",
    "mirrorParams":mirrorParams,
    "mirrorOpen":0,
    "frameno":0,
    "switchTime":numpy.zeros((1,),"d")[0],
    "adaptiveWinGain":0.5,
    "nsubapsTogether":1,
    "nsteps":0,
    "addActuators":0,
    "actuators":None,#(numpy.random.random((3,52))*1000).astype("H"),#None,#an array of actuator values.
    "actSequence":None,#numpy.ones((3,),"i")*1000,
    "recordCents":0,
    "pxlWeight":None,
    "averageImg":0,
    "actuatorMask":None,
    "dmDescription":dmDescription,
    "averageCent":0,
    "centCalData":None,
    "centCalBounds":None,
    "centCalSteps":None,
    "figureOpen":0,
    "figureName":"figureSL240",
    "figureParams":numpy.array([1000,3,0xffff,1]).astype("i"),#timeout,port,affinity,priority
    "reconName":"libreconmvmcuda.so",
    "fluxThreshold":0,
    "printUnused":1,
    "useBrightest":0,
    "figureGain":1,
    "decayFactor":None,#used in libreconmvm.so
    "reconlibOpen":1,
    "maxAdapOffset":0,
    "version":" "*120,
    "noPrePostThread":noPrePostThread,
    "calibrateOpen":1,
    "slopeOpen":1,
    "subapAllocation":subapAllocation,
    }

#control["pxlCnt"][-3:]=npxls#not necessary, but means the RTC reads in all of the pixels... so that the display shows whole image
