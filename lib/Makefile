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
all:

include ../Makefile.config
-include ../Makefile.config.local

install:
	cp python/buffer.py $(PY)
	cp python/calibrate.py $(PY)
	cp python/Check.py $(PY)
	cp python/ConnObj.py $(PY)
	cp python/controlCorba.py $(PY)
	cp python/controlVirtual.py $(PY)
	cp python/controlCorbaTest.py $(PY)
	cp python/controlPyro.py $(PY)
	cp python/controlPyro4.py $(PY)
	cp python/control.py $(PY)
	cp python/correlation.py $(PY)
	cp python/dataSwitchClient.py $(PY)
	cp python/FITS.py $(PY)
	cp python/logread.py $(PY)
	cp python/overlayMaker.py $(PY)
	cp python/plot.py $(PY)
	cp python/jobsched.py $(PY)
	cp python/PSuser.py $(PY)
	cp python/recvStream.py $(PY)
	cp python/rtcgui.py $(PY)
	cp python/Saver.py $(PY)
	cp python/sendStream.py $(PY)
	cp python/serialise.py $(PY)
	cp python/SockConn.py $(PY)
	cp python/socketCam.py $(PY)
	cp python/socketDM.py $(PY)
	cp python/spot.py $(PY)
	cp python/startStreams.py $(PY)
	cp python/stdoutlog.py $(PY)
	cp python/tel.py $(PY)
	cp python/zernike.py $(PY)
	cp python/plotxml.py $(PY)
	cp python/quickDisplay.py $(PY)
	cp python/nodestream.py $(PY)
	cp python/readParamLog.py $(PY)
	cp python/darcStreamTool.py $(PY)
	python2 -c "import compileall; compileall.main()" $(PY)
	chmod a+x $(PY)/control.py
	chmod a+x $(PY)/control.pyc
	chmod a+x $(PY)/FITS.py
	chmod a+x $(PY)/FITS.pyc
	chmod a+x $(PY)/startStreams.py
	chmod a+x $(PY)/startStreams.pyc
	chmod a+x $(PY)/sendStream.py
	chmod a+x $(PY)/sendStream.pyc
	chmod a+x $(PY)/recvStream.py
	chmod a+x $(PY)/recvStream.pyc
	chmod a+x $(PY)/rtcgui.py
	chmod a+x $(PY)/rtcgui.pyc
	chmod a+x $(PY)/quickDisplay.py
	chmod a+x $(PY)/quickDisplay.pyc
	chmod a+x $(PY)/logread.py
	chmod a+x $(PY)/logread.pyc
	chmod a+x $(PY)/plot.py
	chmod a+x $(PY)/plot.pyc
	chmod a+x $(PY)/socketCam.py
	chmod a+x $(PY)/socketCam.pyc
	chmod a+x $(PY)/socketDM.py
	chmod a+x $(PY)/socketDM.pyc
	chmod a+x $(PY)/nodestream.py
	chmod a+x $(PY)/nodestream.pyc
	chmod a+x $(PY)/darcStreamTool.py
	chmod a+x $(PY)/darcStreamTool.pyc
	ln -fs $(PY)/control.py $(BIN)/
	ln -fs $(PY)/control.pyc $(BIN)/
	ln -fs $(PY)/FITS.py $(BIN)/
	ln -fs $(PY)/FITS.pyc $(BIN)/
	ln -fs $(PY)/startStreams.py $(BIN)/
	ln -fs $(PY)/startStreams.pyc $(BIN)/
	ln -fs $(PY)/sendStream.py $(BIN)/
	ln -fs $(PY)/sendStream.pyc $(BIN)/
	ln -fs $(PY)/recvStream.py $(BIN)/
	ln -fs $(PY)/recvStream.pyc $(BIN)/
	ln -fs $(PY)/rtcgui.py $(BIN)/
	ln -fs $(PY)/rtcgui.pyc $(BIN)/
	ln -fs $(PY)/quickDisplay.py $(BIN)/
	ln -fs $(PY)/quickDisplay.pyc $(BIN)/
	ln -fs $(PY)/logread.py $(BIN)/
	ln -fs $(PY)/logread.pyc $(BIN)/
	ln -fs $(PY)/plot.py $(BIN)/
	ln -fs $(PY)/plot.pyc $(BIN)/
	ln -fs $(PY)/socketCam.py $(BIN)/
	ln -fs $(PY)/socketCam.pyc $(BIN)/
	ln -fs $(PY)/socketDM.py $(BIN)/
	ln -fs $(PY)/socketDM.pyc $(BIN)/
	ln -fs $(PY)/nodestream.py $(BIN)/
	ln -fs $(PY)/nodestream.pyc $(BIN)/
	ln -fs $(PY)/control.py $(BIN)/darccontrol
	ln -fs $(PY)/rtcgui.py $(BIN)/darcgui
	ln -fs $(PY)/plot.py $(BIN)/darcplot
	ln -fs $(PY)/controlCorba.py $(PY)/darc.py
	ln -fs $(PY)/darcStreamTool.py $(BIN)/darcStreamTool
	cp Makefile $(LIB)

installdev:
	chmod a+x $(PWD)/python/control.py
	chmod a+x $(PWD)/python/FITS.py
	chmod a+x $(PWD)/python/startStreams.py
	chmod a+x $(PWD)/python/sendStream.py
	chmod a+x $(PWD)/python/recvStream.py
	chmod a+x $(PWD)/python/rtcgui.py
	chmod a+x $(PWD)/python/quickDisplay.py
	chmod a+x $(PWD)/python/logread.py
	chmod a+x $(PWD)/python/plot.py
	chmod a+x $(PWD)/python/socketCam.py
	chmod a+x $(PWD)/python/socketDM.py
	chmod a+x $(PWD)/python/nodestream.py
	chmod a+x $(PWD)/python/darcStreamTool.py
	ln -fs $(PWD)/python/control.py $(PWD)/../bin/
	ln -fs $(PWD)/python/control.pyc $(PWD)/../bin/
	ln -fs $(PWD)/python/FITS.py $(PWD)/../bin/
	ln -fs $(PWD)/python/FITS.pyc $(PWD)/../bin/
	ln -fs $(PWD)/python/startStreams.py $(PWD)/../bin/
	ln -fs $(PWD)/python/startStreams.pyc $(PWD)/../bin/
	ln -fs $(PWD)/python/sendStream.py $(PWD)/../bin/
	ln -fs $(PWD)/python/sendStream.pyc $(PWD)/../bin/
	ln -fs $(PWD)/python/recvStream.py $(PWD)/../bin/
	ln -fs $(PWD)/python/recvStream.pyc $(PWD)/../bin/
	ln -fs $(PWD)/python/rtcgui.py $(PWD)/../bin/
	ln -fs $(PWD)/python/rtcgui.pyc $(PWD)/../bin/
	ln -fs $(PWD)/python/quickDisplay.py $(PWD)/../bin/
	ln -fs $(PWD)/python/quickDisplay.pyc $(PWD)/../bin/
	ln -fs $(PWD)/python/logread.py $(PWD)/../bin/
	ln -fs $(PWD)/python/logread.pyc $(PWD)/../bin/
	ln -fs $(PWD)/python/plot.py $(PWD)/../bin/
	ln -fs $(PWD)/python/plot.pyc $(PWD)/../bin/
	ln -fs $(PWD)/python/socketCam.py $(PWD)/../bin/
	ln -fs $(PWD)/python/socketCam.pyc $(PWD)/../bin/
	ln -fs $(PWD)/python/socketDM.py $(PWD)/../bin/
	ln -fs $(PWD)/python/socketDM.pyc $(PWD)/../bin/
	ln -fs $(PWD)/python/nodestream.py $(PWD)/../bin/
	ln -fs $(PWD)/python/nodestream.pyc $(PWD)/../bin/
	ln -fs $(PWD)/python/control.py $(PWD)/../bin/darccontrol
	ln -fs $(PWD)/python/rtcgui.py $(PWD)/../bin/darcgui
	ln -fs $(PWD)/python/plot.py $(PWD)/../bin/darcplot
	ln -fs $(PWD)/python/controlCorba.py $(PWD)/python/darc.py
	ln -fs $(PWD)/python/darcStreamTool.py $(PWD)/../bin/darcStreamTool
clean:
	rm -f python/*.pyc
	rm -f python/control_idl.py
	rm -f python/darc.py
	rm -f python/utilsmodule.so
	rm -f *.so*
