binDir=bin
srcDir=WiiAST
VPATH=$(srcDir)
objects=$(binDir)/main.o $(binDir)/sound.o
binName=WiiAST_linux

indep=-lasound,-lpthread
cxxFlag=-std=c++11 -Wall

soundImp=sound_linux.cpp

$(binName):$(objects)
	$(CXX) -o $(binDir)/$(binName) $(objects) -Wl,$(indep)

$(binDir)/main.o:makedir main.cpp sound.h
	$(CXX) -c $(srcDir)/main.cpp -o $(binDir)/main.o $(cxxFlag)

$(binDir)/sound.o:makedir $(soundImp) sound.h
	$(CXX) -c $(srcDir)/$(soundImp) -o $(binDir)/sound.o $(cxxFlag)

makedir:
	mkdir -p $(binDir)

clean:
	rm -f $(binDir)/$(binName) $(objects)


